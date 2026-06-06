#include "app_debug_shell.h"
#include "../adv_proto/app_adv_proto.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../identity/app_identity.h"
#include "../peer_table/app_peer_table.h"
#include "../scan/app_scan.h"
#include "../system/app_system.h"
#include "../../app.h"
#include "common/string.h"
#include "drivers.h"
#include "uart.h"

#define DBG_LINE_MAX 40

static char s_line[DBG_LINE_MAX];
static u8 s_line_len;
static u32 s_dbg_msg_id;
static u16 s_dbg_frame_seq;

static void dbg_puts(const char *text)
{
    while (text && *text) {
        uart_ndma_send_byte((u8)*text);
        text++;
    }
}

static void dbg_print_u8(const char *label, u8 value)
{
    u_printf(label);
    u_printf("%x\r\n", value);
}

static void dbg_print_s8(const char *label, s8 value)
{
    u_printf(label);
    u_printf("%d\r\n", value);
}

static void dbg_print_u32(const char *label, u32 value)
{
    u_printf(label);
    u_printf("%x\r\n", value);
}

static u8 cmd_eq(const char *cmd)
{
    u8 i = 0;
    while (cmd[i] && s_line[i]) {
        if (cmd[i] != s_line[i]) {
            return 0;
        }
        i++;
    }
    return cmd[i] == 0 && s_line[i] == 0;
}

static void print_help(void)
{
    u_printf("[DBG] commands\r\n");
    u_printf(" help\r\n");
    u_printf(" ping\r\n");
    u_printf(" info\r\n");
    u_printf(" peers\r\n");
    u_printf(" beacon\r\n");
    u_printf(" send\r\n");
    u_printf(" clear\r\n");
    u_printf(" logs\r\n");
}

static void print_info(void)
{
    const app_eid_t *eid = app_identity_get_eid();
    u_printf("[DBG] info\r\n");
    dbg_print_u8(" state=", (u8)app_system_get_state());
    dbg_print_u32(" short=", app_identity_get_short_id());
    dbg_print_u8(" peer_count=", app_peer_table_count());
    dbg_print_u8(" eid0=", eid->bytes[0]);
    dbg_print_u8(" eid1=", eid->bytes[1]);
    dbg_print_u8(" eid2=", eid->bytes[2]);
    dbg_print_u8(" eid3=", eid->bytes[3]);
}

static void print_peers(void)
{
    app_peer_record_t peers[APP_PEER_MAX_COUNT];
    u8 count;
    u8 i;

    count = app_peer_table_copy(peers, APP_PEER_MAX_COUNT);
    u_printf("[DBG] peers\r\n");
    dbg_print_u8(" count=", count);
    for (i = 0; i < count; i++) {
        u_printf("[DBG] peer\r\n");
        dbg_print_u8(" idx=", i);
        dbg_print_u8(" eid0=", peers[i].eid.bytes[0]);
        dbg_print_u8(" eid1=", peers[i].eid.bytes[1]);
        dbg_print_s8(" rssi=", peers[i].rssi);
        dbg_print_s8(" avg=", peers[i].rssi_avg);
        dbg_print_u8(" level=", (u8)peers[i].level);
    }
}

static void send_test_frame(void)
{
    static const u8 payload[] = {
        'B', '8', '5', 'D', 'B', 'G', 0x01, 0x02,
    };
    app_adv_frame_t frame;
    app_eid_t zero;
    app_status_t st;

    memset(&zero, 0, sizeof(zero));
    memset(&frame, 0, sizeof(frame));
    frame.type = ADV_FRAME_DATA;
    frame.flags = 0;
    frame.key_id = app_identity_get_key_id();
    frame.device_state = (u8)app_system_get_state();
    frame.frame_seq = s_dbg_frame_seq++;
    frame.src_eid = *app_identity_get_eid();
    frame.dst_eid = zero;
    frame.message_id = s_dbg_msg_id++;
    frame.fragment_index = 0;
    frame.fragment_count = 1;
    frame.payload = payload;
    frame.payload_len = (u8)sizeof(payload);

    st = app_adv_scheduler_enqueue_frame(&frame);
    u_printf("[DBG] send\r\n");
    dbg_print_u8(" st=", (u8)st);
    dbg_print_u8(" payload=", frame.payload_len);
}

static void handle_line(void)
{
    if (!s_line_len) {
        return;
    }

    u_printf("[DBG] cmd ");
    dbg_puts(s_line);
    u_printf("\r\n");

    if (cmd_eq("help") || cmd_eq("?")) {
        print_help();
    } else if (cmd_eq("ping")) {
        u_printf("[DBG] pong\r\n");
    } else if (cmd_eq("info")) {
        print_info();
    } else if (cmd_eq("peers")) {
        print_peers();
    } else if (cmd_eq("beacon")) {
        app_status_t st = app_adv_scheduler_request_beacon_update();
        u_printf("[DBG] beacon\r\n");
        dbg_print_u8(" st=", (u8)st);
    } else if (cmd_eq("send")) {
        send_test_frame();
    } else if (cmd_eq("clear")) {
        app_peer_table_clear();
        u_printf("[DBG] peers cleared\r\n");
    } else if (cmd_eq("logs")) {
        app_debug_reset_adv_report_log();
        app_scan_debug_reset();
        app_adv_scheduler_debug_reset();
        u_printf("[DBG] logs reset\r\n");
    } else {
        u_printf("[DBG] unknown\r\n");
        print_help();
    }
}

static void push_char(u8 ch)
{
    if (ch == '\r' || ch == '\n') {
        s_line[s_line_len] = 0;
        handle_line();
        s_line_len = 0;
        memset(s_line, 0, sizeof(s_line));
        return;
    }

    if (ch >= 'A' && ch <= 'Z') {
        ch = (u8)(ch + ('a' - 'A'));
    }

    if (ch == 0x08 || ch == 0x7f) {
        if (s_line_len) {
            s_line_len--;
            s_line[s_line_len] = 0;
        }
        return;
    }

    if (ch < 0x20 || ch > 0x7e) {
        return;
    }

    if (s_line_len < DBG_LINE_MAX - 1) {
        s_line[s_line_len++] = (char)ch;
    } else {
        s_line_len = 0;
        memset(s_line, 0, sizeof(s_line));
        u_printf("[DBG] line overflow\r\n");
    }
}

void app_debug_shell_init(void)
{
    memset(s_line, 0, sizeof(s_line));
    s_line_len = 0;
    s_dbg_msg_id = 1;
    s_dbg_frame_seq = 1;
}

void app_debug_shell_poll(void)
{
    u8 guard = 16;
    while ((reg_uart_buf_cnt & FLD_UART_RX_BUF_CNT) && guard) {
        push_char((u8)uart_ndma_read_byte());
        guard--;
    }
}
