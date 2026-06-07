#include "app_debug_shell.h"
#include "app_debug_shell_cmd.h"
#include "common/string.h"
#include "drivers.h"
#include "uart.h"

#define DBG_LINE_MAX 40
#define DBG_PROMPT "pendant> "
#define DBG_BOOT_PROMPT_DELAY_US 1200000

static char s_line[DBG_LINE_MAX];
static char s_last_line[DBG_LINE_MAX];
static u8 s_line_len;
static u8 s_last_line_len;
static u8 s_skip_lf;
static u8 s_esc_state;
static u8 s_prompt_pending;
static u32 s_prompt_tick;

static void dbg_puts(const char *text)
{
    while (text && *text) {
        uart_ndma_send_byte((u8)*text);
        text++;
    }
}

static void shell_prompt(void)
{
    s_prompt_pending = 0;
    dbg_puts(DBG_PROMPT);
}

static void clear_line(void)
{
    memset(s_line, 0, sizeof(s_line));
    s_line_len = 0;
}

static void erase_input_on_terminal(void)
{
    while (s_line_len) {
        dbg_puts("\b \b");
        s_line_len--;
        s_line[s_line_len] = 0;
    }
}

static void save_history(void)
{
    if (!s_line_len) {
        return;
    }
    memcpy(s_last_line, s_line, sizeof(s_last_line));
    s_last_line_len = s_line_len;
}

static void recall_history(void)
{
    if (!s_last_line_len) {
        return;
    }

    erase_input_on_terminal();
    memcpy(s_line, s_last_line, sizeof(s_line));
    s_line_len = s_last_line_len;
    dbg_puts(s_line);
}

static void handle_line(void)
{
    if (!s_line_len) {
        return;
    }

    save_history();
    app_debug_shell_cmd_execute(s_line);
}

static void push_char(u8 ch)
{
    u8 echo_ch;

    if (s_esc_state) {
        if (s_esc_state == 1) {
            s_esc_state = (ch == '[') ? 2 : 0;
            return;
        }

        s_esc_state = 0;
        if (ch == 'A') {
            recall_history();
        }
        return;
    }

    if (ch == 0x1b) {
        s_esc_state = 1;
        return;
    }

    if (s_skip_lf && ch == '\n') {
        s_skip_lf = 0;
        return;
    }
    s_skip_lf = 0;

    if (ch == '\r' || ch == '\n') {
        if (ch == '\r') {
            s_skip_lf = 1;
        }
        dbg_puts("\r\n");
        s_line[s_line_len] = 0;
        handle_line();
        clear_line();
        shell_prompt();
        return;
    }

    if (ch == 0x03) {
        dbg_puts("^C\r\n");
        clear_line();
        shell_prompt();
        return;
    }

    if (ch == 0x15) {
        erase_input_on_terminal();
        return;
    }

    echo_ch = ch;
    if (ch >= 'A' && ch <= 'Z') {
        ch = (u8)(ch + ('a' - 'A'));
    }

    if (ch == 0x08 || ch == 0x7f) {
        if (s_line_len) {
            s_line_len--;
            s_line[s_line_len] = 0;
            dbg_puts("\b \b");
        }
        return;
    }

    if (ch < 0x20 || ch > 0x7e) {
        return;
    }

    if (s_line_len < DBG_LINE_MAX - 1) {
        s_line[s_line_len++] = (char)ch;
        uart_ndma_send_byte(echo_ch);
    } else {
        dbg_puts("\r\n[DBG] line overflow\r\n");
        clear_line();
        shell_prompt();
    }
}

void app_debug_shell_init(void)
{
    clear_line();
    memset(s_last_line, 0, sizeof(s_last_line));
    s_last_line_len = 0;
    s_skip_lf = 0;
    s_esc_state = 0;
    s_prompt_pending = 1;
    s_prompt_tick = clock_time();
    app_debug_shell_cmd_init();
    dbg_puts("[DBG] shell ready\r\n");
    app_debug_shell_cmd_print_boot_info();
}

void app_debug_shell_poll(void)
{
    u8 guard = 16;

    if (s_prompt_pending && clock_time_exceed(s_prompt_tick, DBG_BOOT_PROMPT_DELAY_US)) {
        shell_prompt();
    }

    while ((reg_uart_buf_cnt & FLD_UART_RX_BUF_CNT) && guard) {
        push_char((u8)uart_ndma_read_byte());
        guard--;
    }
}
