#include "app_debug_shell_cmd.h"
#include "../adv_proto/app_adv_proto.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../identity/app_identity.h"
#include "../peer_table/app_peer_table.h"
#include "../scan/app_scan.h"
#include "../ble/app_ble.h"
#include "../system/app_system.h"
#include "../board/app_board.h"
#include "../build_info/app_build_info.h"
#include "../app.h"
#include "../app_config.h"
#include "common/string.h"
#include "drivers.h"
#include "uart.h"

#define APP_DEBUG_SHELL_CMD_MAX_COUNT 24
#define APP_DEBUG_SHELL_ARG_MAX 5

static app_debug_shell_cmd_entry_t s_cmds[APP_DEBUG_SHELL_CMD_MAX_COUNT];
static u8 s_cmd_count;
static u32 s_dbg_msg_id;
static u16 s_dbg_frame_seq;
static u8 s_dbg_payload[APP_ADV_PAYLOAD_MAX_LEN];
static app_debug_shell_cmd_write_fn_t s_writer;
static void *s_writer_ctx;

static void uart_writer(void *ctx, const char *text, u16 len)
{
    u16 i;
    (void)ctx;

    for (i = 0; i < len; i++) {
        uart_ndma_send_byte((u8)text[i]);
    }
}

static u16 str_len16(const char *text)
{
    u16 len = 0;

    if (!text) {
        return 0;
    }
    while (text[len]) {
        len++;
    }
    return len;
}

void app_debug_shell_cmd_puts(const char *text)
{
    if (!s_writer) {
        s_writer = uart_writer;
    }
    s_writer(s_writer_ctx, text, str_len16(text));
}

static void print_hex_u32(u32 value)
{
    char buf[9];
    u8 started = 0;
    u8 i;
    u8 nibble;
    u8 out = 0;

    for (i = 0; i < 8; i++) {
        nibble = (u8)((value >> ((7 - i) * 4)) & 0x0f);
        if (nibble || started || i == 7) {
            started = 1;
            buf[out++] = (char)(nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10));
        }
    }
    buf[out] = 0;
    app_debug_shell_cmd_puts(buf);
}

static void print_dec_s32(s32 value)
{
    char buf[12];
    u8 i = 0;
    u8 j;
    u32 v;

    if (value < 0) {
        app_debug_shell_cmd_puts("-");
        v = (u32)(-value);
    } else {
        v = (u32)value;
    }

    do {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && i < sizeof(buf));

    for (j = 0; j < i; j++) {
        char c[2];
        c[0] = buf[i - 1 - j];
        c[1] = 0;
        app_debug_shell_cmd_puts(c);
    }
}

void app_debug_shell_cmd_print_u8(const char *label, u8 value)
{
    app_debug_shell_cmd_puts(label);
    print_hex_u32(value);
    app_debug_shell_cmd_puts("\r\n");
}

void app_debug_shell_cmd_print_s8(const char *label, s8 value)
{
    app_debug_shell_cmd_puts(label);
    print_dec_s32(value);
    app_debug_shell_cmd_puts("\r\n");
}

void app_debug_shell_cmd_print_u32(const char *label, u32 value)
{
    app_debug_shell_cmd_puts(label);
    print_hex_u32(value);
    app_debug_shell_cmd_puts("\r\n");
}

static void print_kv_text(const char *label, const char *value)
{
    app_debug_shell_cmd_puts(label);
    app_debug_shell_cmd_puts(value);
    app_debug_shell_cmd_puts("\r\n");
}

void app_debug_shell_cmd_print_boot_info(void)
{
    const app_board_info_t *board = app_board_get_info();

    app_debug_shell_cmd_puts("[DBG] build\r\n");
    print_kv_text(" fw=", APP_BUILD_FW_NAME);
    print_kv_text(" ver=", APP_BUILD_FW_VERSION);
    print_kv_text(" board=", APP_BUILD_BOARD_NAME);
    print_kv_text(" chip=", APP_BUILD_CHIP_NAME);
    print_kv_text(" git=", APP_BUILD_GIT_DESC);
    print_kv_text(" branch=", APP_BUILD_GIT_BRANCH);
    print_kv_text(" date=", APP_BUILD_COMPILE_DATE);
    print_kv_text(" time=", APP_BUILD_COMPILE_TIME);
    app_debug_shell_cmd_print_u8(" dirty=", (u8)APP_BUILD_GIT_DIRTY);
    app_debug_shell_cmd_print_u8(" hw_rev=", (u8)board->hw_rev);
    app_debug_shell_cmd_print_u8(" usb=", (u8)PENDANT_USB_ENABLE);
    app_debug_shell_cmd_print_u8(" ext_adv=", (u8)PENDANT_EXT_ADV_ENABLE);
    app_debug_shell_cmd_print_u8(" scan=", (u8)APP_BLE_ENABLE_DISCOVERY_SCAN);
    app_debug_shell_cmd_print_u32(" adv_max=", APP_ADV_FRAME_MAX_LEN);
    app_debug_shell_cmd_print_u32(" payload_max=", APP_ADV_PAYLOAD_MAX_LEN);
}

static u8 str_eq(const char *a, const char *b)
{
    u8 i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static u8 parse_u16_arg(const char *text, u16 *value)
{
    u32 v = 0;
    u8 base = 10;
    u8 i = 0;

    if (!text || !text[0] || !value) {
        return 0;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        i = 2;
        if (!text[i]) {
            return 0;
        }
    }

    while (text[i]) {
        u8 digit;
        char c = text[i++];

        if (c >= '0' && c <= '9') {
            digit = (u8)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = (u8)(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = (u8)(c - 'A' + 10);
        } else {
            return 0;
        }

        if (digit >= base) {
            return 0;
        }
        v = v * base + digit;
        if (v > 0xffff) {
            return 0;
        }
    }

    *value = (u16)v;
    return 1;
}

u8 app_debug_shell_cmd_register(const char *name, const char *usage, const char *help,
                                app_debug_shell_cmd_handler_t handler)
{
    u8 i;

    if (!name || !handler || s_cmd_count >= APP_DEBUG_SHELL_CMD_MAX_COUNT) {
        return 0;
    }

    for (i = 0; i < s_cmd_count; i++) {
        if (str_eq(name, s_cmds[i].name)) {
            return 0;
        }
    }

    s_cmds[s_cmd_count].name = name;
    s_cmds[s_cmd_count].usage = usage;
    s_cmds[s_cmd_count].help = help;
    s_cmds[s_cmd_count].handler = handler;
    s_cmd_count++;
    return 1;
}

static u8 split_args(char *line, char **argv, u8 max_argc)
{
    u8 argc = 0;
    char *p = line;

    while (*p && argc < max_argc) {
        while (*p == ' ' || *p == '\t') {
            *p = 0;
            p++;
        }
        if (!*p) {
            break;
        }

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    if (*p) {
        *p = 0;
    }

    return argc;
}

static void cmd_help(u8 argc, char **argv)
{
    u8 i;
    (void)argc;
    (void)argv;

    app_debug_shell_cmd_puts("[DBG] commands\r\n");
    for (i = 0; i < s_cmd_count; i++) {
        app_debug_shell_cmd_puts(" ");
        app_debug_shell_cmd_puts(s_cmds[i].usage ? s_cmds[i].usage : s_cmds[i].name);
        if (s_cmds[i].help) {
            app_debug_shell_cmd_puts(" - ");
            app_debug_shell_cmd_puts(s_cmds[i].help);
        }
        app_debug_shell_cmd_puts("\r\n");
    }
}

static void cmd_ping(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_debug_shell_cmd_puts("[DBG] pong\r\n");
}

static void cmd_info(u8 argc, char **argv)
{
    const app_eid_t *eid;
    (void)argc;
    (void)argv;

    eid = app_identity_get_eid();
    app_debug_shell_cmd_puts("[DBG] info\r\n");
    app_debug_shell_cmd_print_u8(" state=", (u8)app_system_get_state());
    app_debug_shell_cmd_print_u32(" short=", app_identity_get_short_id());
    app_debug_shell_cmd_print_u8(" peer_count=", app_peer_table_count());
    app_debug_shell_cmd_print_u8(" eid0=", eid->bytes[0]);
    app_debug_shell_cmd_print_u8(" eid1=", eid->bytes[1]);
    app_debug_shell_cmd_print_u8(" eid2=", eid->bytes[2]);
    app_debug_shell_cmd_print_u8(" eid3=", eid->bytes[3]);
}

static void cmd_build(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_debug_shell_cmd_print_boot_info();
}

static void cmd_peers(u8 argc, char **argv)
{
    app_peer_record_t peers[APP_PEER_MAX_COUNT];
    u8 count;
    u8 i;
    (void)argc;
    (void)argv;

    count = app_peer_table_copy(peers, APP_PEER_MAX_COUNT);
    app_debug_shell_cmd_puts("[DBG] peers\r\n");
    app_debug_shell_cmd_print_u8(" count=", count);
    for (i = 0; i < count; i++) {
        app_debug_shell_cmd_puts("[DBG] peer\r\n");
        app_debug_shell_cmd_print_u8(" idx=", i);
        app_debug_shell_cmd_print_u8(" eid0=", peers[i].eid.bytes[0]);
        app_debug_shell_cmd_print_u8(" eid1=", peers[i].eid.bytes[1]);
        app_debug_shell_cmd_print_s8(" rssi=", peers[i].rssi);
        app_debug_shell_cmd_print_s8(" avg=", peers[i].rssi_avg);
        app_debug_shell_cmd_print_u8(" level=", (u8)peers[i].level);
    }
}

static void cmd_beacon(u8 argc, char **argv)
{
    app_status_t st;
    (void)argc;
    (void)argv;

    st = app_adv_scheduler_request_beacon_update();
    app_debug_shell_cmd_puts("[DBG] beacon\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
}

static void cmd_send(u8 argc, char **argv)
{
    static const u8 payload[] = {
        'B', '8', '5', 'D', 'B', 'G', 0x01, 0x02,
    };
    app_adv_frame_t frame;
    app_eid_t zero;
    app_status_t st;
    (void)argc;
    (void)argv;

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
    app_debug_shell_cmd_puts("[DBG] send\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
    app_debug_shell_cmd_print_u8(" payload=", frame.payload_len);
}

static void cmd_sendmax(u8 argc, char **argv)
{
    app_adv_frame_t frame;
    app_eid_t zero;
    app_status_t st;
    u16 len = APP_ADV_PAYLOAD_MAX_LEN;
    u16 i;

    if (argc > 2 || (argc == 2 && !parse_u16_arg(argv[1], &len)) ||
        len == 0 || len > APP_ADV_PAYLOAD_MAX_LEN) {
        app_debug_shell_cmd_puts("[DBG] usage: sendmax [len]\r\n");
        app_debug_shell_cmd_print_u32(" max_payload=", APP_ADV_PAYLOAD_MAX_LEN);
        app_debug_shell_cmd_print_u32(" max_adv=", APP_ADV_FRAME_MAX_LEN);
        return;
    }

    for (i = 0; i < len; i++) {
        s_dbg_payload[i] = (u8)(i ^ 0x5a);
    }

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
    frame.payload = s_dbg_payload;
    frame.payload_len = (u8)len;

    st = app_adv_scheduler_enqueue_frame(&frame);
    app_debug_shell_cmd_puts("[DBG] sendmax\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
    app_debug_shell_cmd_print_u8(" payload=", frame.payload_len);
    app_debug_shell_cmd_print_u32(" adv_len=", APP_ADV_AD_OVERHEAD_LEN + APP_ADV_HEADER_LEN + frame.payload_len + APP_ADV_FRAME_CRC_LEN);
}

static void cmd_clear(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_peer_table_clear();
    app_debug_shell_cmd_puts("[DBG] peers cleared\r\n");
}

static void cmd_logs(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_debug_reset_adv_report_log();
    app_scan_debug_reset();
    app_adv_scheduler_debug_reset();
    app_debug_shell_cmd_puts("[DBG] logs reset\r\n");
}

static void cmd_rxstat(u8 argc, char **argv)
{
    app_scan_debug_t scan;
    (void)argc;
    (void)argv;

    app_scan_get_debug(&scan);
    app_debug_shell_cmd_puts("[DBG] rxstat\r\n");
    app_debug_shell_cmd_print_u32(" reports=", scan.reports);
    app_debug_shell_cmd_print_u32(" ok=", scan.decode_ok);
    app_debug_shell_cmd_print_u32(" fail=", scan.decode_fail);
    app_debug_shell_cmd_print_u32(" vendor_fail=", scan.vendor_decode_fail);
    app_debug_shell_cmd_print_u32(" self=", scan.self_ignored);
    app_debug_shell_cmd_print_u32(" beacon=", scan.beacon_rx);
    app_debug_shell_cmd_print_u32(" data=", scan.data_rx);
    app_debug_shell_cmd_print_u32(" other=", scan.other_rx);
    app_debug_shell_cmd_print_u8(" adv_len=", scan.last_adv_len);
    app_debug_shell_cmd_print_u8(" type=", scan.last_type);
    app_debug_shell_cmd_print_u8(" payload=", scan.last_payload_len);
    app_debug_shell_cmd_print_s8(" rssi=", scan.last_rssi);
    app_debug_shell_cmd_print_u8(" src0=", scan.last_src0);
    app_debug_shell_cmd_print_u8(" src1=", scan.last_src1);
}

static void cmd_txstat(u8 argc, char **argv)
{
    app_adv_scheduler_debug_t adv;
    (void)argc;
    (void)argv;

    app_adv_scheduler_get_debug(&adv);
    app_debug_shell_cmd_puts("[DBG] txstat\r\n");
    app_debug_shell_cmd_print_u32(" build_ok=", adv.build_ok);
    app_debug_shell_cmd_print_u32(" build_fail=", adv.build_fail);
    app_debug_shell_cmd_print_u32(" beacon_ok=", adv.beacon_build_ok);
    app_debug_shell_cmd_print_u32(" data_ok=", adv.data_build_ok);
    app_debug_shell_cmd_print_u32(" enq_ok=", adv.enqueue_ok);
    app_debug_shell_cmd_print_u32(" enq_full=", adv.enqueue_full);
    app_debug_shell_cmd_print_u8(" queue=", adv.queue_count);
    app_debug_shell_cmd_print_u8(" st=", adv.last_status);
    app_debug_shell_cmd_print_u8(" adv_len=", adv.last_adv_len);
    app_debug_shell_cmd_print_u8(" max_len=", adv.max_adv_len);
    app_debug_shell_cmd_print_u8(" type=", adv.last_type);
    app_debug_shell_cmd_print_u8(" payload=", adv.last_payload_len);
    app_debug_shell_cmd_print_u8(" data_len=", adv.last_data_adv_len);
    app_debug_shell_cmd_print_u8(" data_payload=", adv.last_data_payload_len);
}

static void cmd_radio(u8 argc, char **argv)
{
    app_ble_debug_t ble;
    (void)argc;
    (void)argv;

    app_ble_get_debug(&ble);
    app_debug_shell_cmd_puts("[DBG] radio\r\n");
    app_debug_shell_cmd_print_u8(" conn=", ble.connected);
    app_debug_shell_cmd_print_u8(" adv0=", ble.adv0_status);
    app_debug_shell_cmd_print_u8(" adv1=", ble.adv1_status);
    app_debug_shell_cmd_print_u8(" scan=", ble.scan_status);
    app_debug_shell_cmd_print_u8(" upd_st=", ble.last_adv_update_status);
    app_debug_shell_cmd_print_u32(" upd_ok=", ble.adv_update_ok);
    app_debug_shell_cmd_print_u32(" upd_fail=", ble.adv_update_fail);
    app_debug_shell_cmd_print_u32(" rpt_leg=", app_radio_debug_legacy_reports());
    app_debug_shell_cmd_print_u32(" rpt_ext=", app_radio_debug_ext_reports());
    app_debug_shell_cmd_print_u32(" rpt_aux=", app_radio_debug_aux_reports());
    app_debug_shell_cmd_print_u32(" aux_max=", app_radio_debug_aux_max_len());
    app_debug_shell_cmd_print_u32(" aux_len=", app_radio_debug_aux_last_len());
    app_debug_shell_cmd_print_u32(" aux_evt=", app_radio_debug_aux_last_evt());
    app_debug_shell_cmd_print_u32(" aux_st=", app_radio_debug_aux_last_status());
    app_debug_shell_cmd_print_u32(" aux_evt_len=", app_radio_debug_aux_evt_len());
    app_debug_shell_cmd_print_u32(" aux_avail=", app_radio_debug_aux_avail_len());
    app_debug_shell_cmd_print_u32(" aux_vstart=", app_radio_debug_aux_vendor_start());
    app_debug_shell_cmd_print_u32(" aux_svc=", app_radio_debug_aux_service_start());
    app_debug_shell_cmd_print_u32(" aux_reasm=", app_radio_debug_aux_reasm_ok());
    app_debug_shell_cmd_print_u32(" aux_drop=", app_radio_debug_aux_reasm_drop());
    app_debug_shell_cmd_print_u8(" aux_b0=", (u8)app_radio_debug_aux_last_b0());
    app_debug_shell_cmd_print_u8(" aux_b1=", (u8)app_radio_debug_aux_last_b1());
    app_debug_shell_cmd_print_u8(" aux_b2=", (u8)app_radio_debug_aux_last_b2());
    app_debug_shell_cmd_print_u8(" aux_b3=", (u8)app_radio_debug_aux_last_b3());
    app_debug_shell_cmd_print_u8(" aux_vb0=", (u8)app_radio_debug_aux_vendor_b0());
    app_debug_shell_cmd_print_u8(" aux_vb1=", (u8)app_radio_debug_aux_vendor_b1());
    app_debug_shell_cmd_print_u8(" aux_vb2=", (u8)app_radio_debug_aux_vendor_b2());
    app_debug_shell_cmd_print_u8(" aux_vb3=", (u8)app_radio_debug_aux_vendor_b3());
}

static void cmd_disc(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_ble_disconnect_app(0x13);
    app_debug_shell_cmd_puts("[DBG] disconnect\r\n");
}

static void cmd_reset(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_debug_shell_cmd_puts("[DBG] reset\r\n");
    sleep_ms(20);
    start_reboot();
}

static void cmd_usb(u8 argc, char **argv)
{
    if (argc == 1) {
        app_debug_shell_cmd_puts("[DBG] usb\r\n");
        app_debug_shell_cmd_print_u8(" en=", app_usb_download_is_enabled());
        return;
    }

    if (argc == 2 && str_eq(argv[1], "on")) {
        app_usb_download_set_enabled(1);
        app_debug_shell_cmd_puts("[DBG] usb on\r\n");
        return;
    }

    if (argc == 2 && str_eq(argv[1], "off")) {
        app_usb_download_set_enabled(0);
        app_debug_shell_cmd_puts("[DBG] usb off\r\n");
        return;
    }

    app_debug_shell_cmd_puts("[DBG] usage: usb [on|off]\r\n");
}

static void lower_command_name(char *text)
{
    while (*text && *text != ' ' && *text != '\t') {
        if (*text >= 'A' && *text <= 'Z') {
            *text = (char)(*text + ('a' - 'A'));
        }
        text++;
    }
}

static void execute_line(char *line)
{
    char *argv[APP_DEBUG_SHELL_ARG_MAX];
    u8 argc;
    u8 i;

    lower_command_name(line);
    argc = split_args(line, argv, APP_DEBUG_SHELL_ARG_MAX);
    if (!argc) {
        return;
    }

    for (i = 0; i < s_cmd_count; i++) {
        if (str_eq(argv[0], s_cmds[i].name)) {
            s_cmds[i].handler(argc, argv);
            return;
        }
    }

    app_debug_shell_cmd_puts("[DBG] unknown\r\n");
    cmd_help(0, 0);
}

void app_debug_shell_cmd_execute(char *line)
{
    execute_line(line);
}

void app_debug_shell_cmd_execute_with_writer(char *line, app_debug_shell_cmd_write_fn_t writer, void *ctx)
{
    app_debug_shell_cmd_write_fn_t old_writer = s_writer;
    void *old_ctx = s_writer_ctx;

    s_writer = writer ? writer : uart_writer;
    s_writer_ctx = ctx;
    execute_line(line);
    s_writer = old_writer;
    s_writer_ctx = old_ctx;
}

void app_debug_shell_cmd_init(void)
{
    memset(s_cmds, 0, sizeof(s_cmds));
    s_cmd_count = 0;
    s_dbg_msg_id = 1;
    s_dbg_frame_seq = 1;
    s_writer = uart_writer;
    s_writer_ctx = 0;

    app_debug_shell_cmd_register("help", "help", "show commands", cmd_help);
    app_debug_shell_cmd_register("?", "?", "show commands", cmd_help);
    app_debug_shell_cmd_register("ping", "ping", "check shell", cmd_ping);
    app_debug_shell_cmd_register("build", "build", "show build info", cmd_build);
    app_debug_shell_cmd_register("info", "info", "show device info", cmd_info);
    app_debug_shell_cmd_register("peers", "peers", "show peer table", cmd_peers);
    app_debug_shell_cmd_register("beacon", "beacon", "request beacon update", cmd_beacon);
    app_debug_shell_cmd_register("send", "send", "enqueue test frame", cmd_send);
    app_debug_shell_cmd_register("sendmax", "sendmax [len]", "enqueue max-size test frame", cmd_sendmax);
    app_debug_shell_cmd_register("clear", "clear", "clear peer table", cmd_clear);
    app_debug_shell_cmd_register("logs", "logs", "reset debug counters", cmd_logs);
    app_debug_shell_cmd_register("rxstat", "rxstat", "show scan decode counters", cmd_rxstat);
    app_debug_shell_cmd_register("txstat", "txstat", "show adv scheduler counters", cmd_txstat);
    app_debug_shell_cmd_register("radio", "radio", "show BLE radio state", cmd_radio);
    app_debug_shell_cmd_register("disc", "disc", "disconnect GATT", cmd_disc);
    app_debug_shell_cmd_register("reset", "reset", "software reset", cmd_reset);
    app_debug_shell_cmd_register("usb", "usb [on|off]", "show or switch USB", cmd_usb);
}
