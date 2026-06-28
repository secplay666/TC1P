#include "app_debug_shell_cmd.h"
#include "../adv_proto/app_adv_proto.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../identity/app_identity.h"
#include "../peer_table/app_peer_table.h"
#include "../peer_transport/app_peer_transport.h"
#include "../crypto/app_crypto.h"
#include "../host_cmd/app_host_cmd.h"
#include "../profile/app_profile.h"
#include "../scan/app_scan.h"
#include "../discovery/app_discovery.h"
#include "../ble/app_ble.h"
#include "../system/app_system.h"
#include "../board/app_board.h"
#include "../build_info/app_build_info.h"
#include "../app_pendant.h"
#include "../pm/app_pm.h"
#include "../app.h"
#include "../app_config.h"
#include "common/string.h"
#include "drivers.h"
#include "uart.h"

#define APP_DEBUG_SHELL_CMD_MAX_COUNT 24
#define APP_DEBUG_SHELL_ARG_MAX 5
#define APP_DEBUG_SHELL_ENABLE_ADV_TEST_CMDS 0
#define APP_DEBUG_SHELL_SENDMAX_PAYLOAD_MAX 64

static app_debug_shell_cmd_entry_t s_cmds[APP_DEBUG_SHELL_CMD_MAX_COUNT];
static u8 s_cmd_count;
static u32 s_dbg_msg_id;
static u16 s_dbg_frame_seq;
static u16 s_dbg_chat_nonce;
#if APP_DEBUG_SHELL_ENABLE_ADV_TEST_CMDS
static u8 s_dbg_payload[APP_DEBUG_SHELL_SENDMAX_PAYLOAD_MAX];
#endif
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

static void shell_write_bytes(const u8 *data, u16 len)
{
    if (!data || !len) {
        return;
    }
    if (!s_writer) {
        s_writer = uart_writer;
    }
    s_writer(s_writer_ctx, (const char *)data, len);
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
    app_debug_shell_cmd_print_u8(" wdt=", (u8)PENDANT_WATCHDOG_ENABLE);
    app_debug_shell_cmd_print_u32(" wdt_ms=", PENDANT_WATCHDOG_TIMEOUT_MS);
    app_debug_shell_cmd_print_u8(" wake_src=", app_pm_get_raw_wakeup_src());
    app_debug_shell_cmd_print_u8(" wake_wd=", (u8)((app_pm_get_raw_wakeup_src() & WAKEUP_STATUS_WD) ? 1 : 0));
    app_debug_shell_cmd_print_u8(" trace=", app_pendant_trace_get());
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

static s8 hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return (s8)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (s8)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (s8)(c - 'A' + 10);
    }
    return -1;
}

static u8 parse_mac_arg(const char *text, u8 *addr)
{
    u8 nibbles[12];
    u8 count = 0;
    u8 i;

    if (!text || !addr) {
        return 0;
    }

    while (*text) {
        s8 v;
        if (*text == ':' || *text == '-') {
            text++;
            continue;
        }
        v = hex_value(*text++);
        if (v < 0 || count >= sizeof(nibbles)) {
            return 0;
        }
        nibbles[count++] = (u8)v;
    }

    if (count != sizeof(nibbles)) {
        return 0;
    }

    for (i = 0; i < 6; i++) {
        u8 hi = nibbles[i * 2];
        u8 lo = nibbles[i * 2 + 1];
        addr[5 - i] = (u8)((hi << 4) | lo);
    }
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

static void cmd_profile(u8 argc, char **argv)
{
    app_profile_summary_t summary;
    const app_profile_summary_t *current;
    u16 nick_len;
    u16 sig_len;
    app_status_t st;

    if (argc == 1) {
        current = app_profile_get();
        app_debug_shell_cmd_puts("[DBG] profile\r\n");
        app_debug_shell_cmd_print_u8(" flags=", current->flags);
        app_debug_shell_cmd_print_u32(" seq=", current->seq);
        app_debug_shell_cmd_print_u32(" avatar=", current->avatar_seed);
        app_debug_shell_cmd_print_u8(" tags=", current->tag_count);
        app_debug_shell_cmd_puts(" nick=");
        shell_write_bytes(current->nickname, current->nickname_len);
        app_debug_shell_cmd_puts("\r\n sig=");
        shell_write_bytes(current->signature, current->signature_len);
        app_debug_shell_cmd_puts("\r\n");
        return;
    }

    if (argc != 4 || !str_eq(argv[1], "set")) {
        app_debug_shell_cmd_puts("[DBG] usage: profile set <nick> <signature>\r\n");
        app_debug_shell_cmd_print_u32(" nick_max=", APP_PROFILE_NICKNAME_MAX_LEN);
        app_debug_shell_cmd_print_u32(" sig_max=", APP_PROFILE_SIGNATURE_MAX_LEN);
        return;
    }

    nick_len = str_len16(argv[2]);
    sig_len = str_len16(argv[3]);
    if (!nick_len || !sig_len ||
        nick_len > APP_PROFILE_NICKNAME_MAX_LEN ||
        sig_len > APP_PROFILE_SIGNATURE_MAX_LEN) {
        app_debug_shell_cmd_puts("[DBG] profile len\r\n");
        app_debug_shell_cmd_print_u32(" nick=", nick_len);
        app_debug_shell_cmd_print_u32(" sig=", sig_len);
        app_debug_shell_cmd_print_u32(" nick_max=", APP_PROFILE_NICKNAME_MAX_LEN);
        app_debug_shell_cmd_print_u32(" sig_max=", APP_PROFILE_SIGNATURE_MAX_LEN);
        return;
    }

    current = app_profile_get();
    memset(&summary, 0, sizeof(summary));
    summary.flags = APP_PROFILE_FLAG_VISIBLE;
    summary.key_id = current->key_id;
    summary.seq = current->seq;
    summary.avatar_seed = clock_time() ^ app_identity_get_short_id();
    summary.tag_count = 3;
    summary.tags[0] = 1;
    summary.tags[1] = 2;
    summary.tags[2] = 3;
    summary.nickname_len = (u8)nick_len;
    summary.signature_len = (u8)sig_len;
    memcpy(summary.nickname, argv[2], nick_len);
    memcpy(summary.signature, argv[3], sig_len);

    st = app_profile_set_summary(&summary);
    if (st == APP_OK) {
        app_adv_scheduler_request_beacon_update();
    }
    app_debug_shell_cmd_puts("[DBG] profile set\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
    if (st == APP_OK) {
        cmd_profile(1, 0);
    }
}

static void cmd_peers(u8 argc, char **argv)
{
    app_peer_record_t peers[APP_PEER_MAX_COUNT];
    app_discovery_debug_t disc;
    u8 verbose;
    u8 count;
    u8 i;

    count = app_peer_table_copy(peers, APP_PEER_MAX_COUNT);
    app_debug_shell_cmd_puts("[DBG] peers\r\n");
    app_debug_shell_cmd_print_u8(" count=", count);
    verbose = (argc > 1 && argv && argv[1] && argv[1][0] == 'v');
    if (verbose) {
        app_discovery_get_debug(&disc);
        app_debug_shell_cmd_print_u32(" disc_beacon=", disc.beacon_rx);
        app_debug_shell_cmd_print_u32(" disc_alloc_ok=", disc.alloc_ok);
        app_debug_shell_cmd_print_u32(" disc_alloc_fail=", disc.alloc_fail);
        app_debug_shell_cmd_print_u8(" disc_count=", disc.peer_count);
        app_debug_shell_cmd_print_u8(" disc_eid0=", disc.last_eid0);
        app_debug_shell_cmd_print_u8(" disc_eid1=", disc.last_eid1);
        app_debug_shell_cmd_print_s8(" disc_rssi=", disc.last_rssi);
        app_debug_shell_cmd_print_s8(" disc_avg=", disc.last_avg);
        app_debug_shell_cmd_print_u8(" disc_target=", disc.last_target_level);
        app_debug_shell_cmd_print_u8(" disc_level=", disc.last_peer_level);
    }
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

#if APP_DEBUG_SHELL_ENABLE_ADV_TEST_CMDS
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
    u16 len = APP_DEBUG_SHELL_SENDMAX_PAYLOAD_MAX;
    u16 i;

    if (argc > 2 || (argc == 2 && !parse_u16_arg(argv[1], &len)) ||
        len == 0 || len > APP_DEBUG_SHELL_SENDMAX_PAYLOAD_MAX) {
        app_debug_shell_cmd_puts("[DBG] usage: sendmax [len]\r\n");
        app_debug_shell_cmd_print_u32(" max_payload=", APP_DEBUG_SHELL_SENDMAX_PAYLOAD_MAX);
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
#endif

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
    app_debug_shell_cmd_print_u32(" defer_full=", scan.defer_full);
    app_debug_shell_cmd_print_u8(" defer_q=", scan.defer_count);
    app_debug_shell_cmd_print_u8(" adv_len=", scan.last_adv_len);
    app_debug_shell_cmd_print_u8(" type=", scan.last_type);
    app_debug_shell_cmd_print_u8(" payload=", scan.last_payload_len);
    app_debug_shell_cmd_print_s8(" rssi=", scan.last_rssi);
    app_debug_shell_cmd_print_u8(" src0=", scan.last_src0);
    app_debug_shell_cmd_print_u8(" src1=", scan.last_src1);
}

static void cmd_p2pstat(u8 argc, char **argv)
{
    app_peer_transport_debug_t debug;
    (void)argc;
    (void)argv;
    app_peer_transport_get_debug(&debug);
    app_debug_shell_cmd_puts("[DBG] p2pstat\r\n");
    app_debug_shell_cmd_print_u32(" tx_ok=", debug.tx_ok);
    app_debug_shell_cmd_print_u32(" tx_fail=", debug.tx_fail);
    app_debug_shell_cmd_print_u32(" tx_msg_ok=", debug.tx_msg_ok);
    app_debug_shell_cmd_print_u32(" tx_msg_fail=", debug.tx_msg_fail);
    app_debug_shell_cmd_print_u32(" tx_frag=", debug.tx_frag_sent);
    app_debug_shell_cmd_print_u32(" tx_retx=", debug.tx_frag_retx);
    app_debug_shell_cmd_print_u32(" tx_ack=", debug.tx_ack_rx);
    app_debug_shell_cmd_print_u32(" tx_ack_match=", debug.tx_ack_match);
    app_debug_shell_cmd_print_u32(" tx_ack_to=", debug.tx_ack_timeout);
    app_debug_shell_cmd_print_u32(" rx_total=", debug.rx_total);
    app_debug_shell_cmd_print_u32(" rx_accept=", debug.rx_accept);
    app_debug_shell_cmd_print_u32(" rx_drop=", debug.rx_drop);
    app_debug_shell_cmd_print_u32(" rx_dup=", debug.rx_dup);
    app_debug_shell_cmd_print_u32(" rx_bc=", debug.rx_broadcast);
    app_debug_shell_cmd_print_u32(" rx_dir=", debug.rx_direct);
    app_debug_shell_cmd_print_u32(" rx_msg_ok=", debug.rx_msg_ok);
    app_debug_shell_cmd_print_u32(" rx_msg_to=", debug.rx_msg_timeout);
    app_debug_shell_cmd_print_u32(" rx_frag=", debug.rx_frag_new);
    app_debug_shell_cmd_print_u32(" rx_ack_tx=", debug.rx_ack_tx);
    app_debug_shell_cmd_print_u32(" rx_busy=", debug.rx_busy);
    app_debug_shell_cmd_print_u32(" rx_rej=", debug.rx_rejected);
    app_debug_shell_cmd_print_u32(" rx_dbg_drop=", debug.rx_debug_drop);
    app_debug_shell_cmd_print_u32(" tx_seq=", debug.last_tx_seq);
    app_debug_shell_cmd_print_u32(" rx_seq=", debug.last_rx_seq);
    app_debug_shell_cmd_print_u32(" tx_msg_len=", debug.last_tx_msg_len);
    app_debug_shell_cmd_print_u32(" rx_msg_len=", debug.last_rx_msg_len);
    app_debug_shell_cmd_print_u8(" st=", debug.last_status);
    app_debug_shell_cmd_print_u8(" tx_type=", debug.last_tx_type);
    app_debug_shell_cmd_print_u8(" tx_len=", debug.last_tx_len);
    app_debug_shell_cmd_print_u8(" rx_type=", debug.last_rx_type);
    app_debug_shell_cmd_print_u8(" rx_len=", debug.last_rx_len);
    app_debug_shell_cmd_print_s8(" rssi=", debug.last_rssi);
    app_debug_shell_cmd_print_u8(" src0=", debug.last_rx_src0);
    app_debug_shell_cmd_print_u8(" src1=", debug.last_rx_src1);
    app_debug_shell_cmd_print_u8(" max=", debug.max_payload_len);
    app_debug_shell_cmd_print_u8(" max_frag=", debug.max_fragments);
    app_debug_shell_cmd_print_u8(" tx_active=", debug.tx_active);
    app_debug_shell_cmd_print_u8(" tx_frag_cnt=", debug.tx_frag_count);
    app_debug_shell_cmd_print_u8(" tx_pending=", debug.tx_pending);
    app_debug_shell_cmd_print_u8(" tx_ack_bits=", debug.tx_ack_bits);
    app_debug_shell_cmd_print_u8(" tx_retry=", debug.tx_retry_round);
    app_debug_shell_cmd_print_u8(" ack_st=", debug.last_ack_status);
    app_debug_shell_cmd_print_u8(" ack_type=", debug.last_ack_type);
    app_debug_shell_cmd_print_u8(" ack_frag=", debug.last_ack_frag_count);
    app_debug_shell_cmd_print_u8(" ack_bits=", debug.last_ack_bitmap);
    app_debug_shell_cmd_print_u8(" ack_match=", debug.last_ack_match_flags);
    app_debug_shell_cmd_print_u8(" ack_src0=", debug.last_ack_src0);
    app_debug_shell_cmd_print_u8(" ack_src1=", debug.last_ack_src1);
    app_debug_shell_cmd_print_u8(" rx_active=", debug.rx_active);
    app_debug_shell_cmd_print_u8(" rx_frag_cnt=", debug.rx_frag_count);
    app_debug_shell_cmd_print_u8(" rx_bitmap=", debug.rx_bitmap);
    app_debug_shell_cmd_print_u8(" rx_drop_mask=", debug.rx_drop_mask);
}

static void cmd_p2psend(u8 argc, char **argv)
{
    app_peer_record_t peer;
    app_eid_t *dst = 0;
    app_peer_send_mode_t mode = APP_PEER_SEND_UNRELIABLE;
    u16 len = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    u16 i;
    app_status_t st;

    if (app_ble_is_app_connected()) {
        app_debug_shell_cmd_puts("[DBG] p2psend blocked while app connected\r\n");
        return;
    }

    if (argc > 1) {
        if (!parse_u16_arg(argv[1], &len)) {
            app_debug_shell_cmd_puts("[DBG] usage: p2psend [len] [u|r]\r\n");
            app_debug_shell_cmd_print_u8(" max=", APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN);
            app_debug_shell_cmd_print_u32(" max_msg=", APP_PEER_TRANSPORT_MESSAGE_MAX_LEN);
            return;
        }
    }
    if (argc > 2) {
        if (str_eq(argv[2], "r")) {
            mode = APP_PEER_SEND_RELIABLE;
            if (app_peer_table_copy(&peer, 1) != 1) {
                app_debug_shell_cmd_puts("[DBG] no peer\r\n");
                return;
            }
            dst = &peer.eid;
        } else if (str_eq(argv[2], "u")) {
            mode = APP_PEER_SEND_UNRELIABLE;
        } else {
            app_debug_shell_cmd_puts("[DBG] usage: p2psend [len] [u|r]\r\n");
            return;
        }
    }
    if (len > APP_PEER_TRANSPORT_MESSAGE_MAX_LEN) {
        app_debug_shell_cmd_puts("[DBG] usage: p2psend [len] [u|r]\r\n");
        app_debug_shell_cmd_print_u8(" max=", APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN);
        app_debug_shell_cmd_print_u32(" max_msg=", APP_PEER_TRANSPORT_MESSAGE_MAX_LEN);
        return;
    }

    (void)i;
    st = app_peer_transport_send_test_pattern(dst, APP_PEER_MSG_TEST, len, mode, 0);
    app_debug_shell_cmd_puts("[DBG] p2psend\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
    app_debug_shell_cmd_print_u32(" payload=", len);
    app_debug_shell_cmd_print_u8(" mode=", (u8)mode);
    app_debug_shell_cmd_print_u32(" frag_payload=", APP_PEER_TRANSPORT_HEADER_LEN + APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN);
}

static void cmd_p2pchat(u8 argc, char **argv)
{
    app_peer_record_t peer;
    u8 payload[APP_HOST_P2P_CHAT_TEXT_MAX_LEN + APP_CHAT_CRYPTO_HEADER_LEN];
    u8 *plain = &payload[APP_CHAT_CRYPTO_HEADER_LEN];
    u16 len = 0;
    u16 encrypted_len = 0;
    u32 nonce;
    u8 i;
    u8 j;
    u8 truncated = 0;
    app_status_t st;

    if (app_ble_is_app_connected()) {
        app_debug_shell_cmd_puts("[DBG] p2pchat blocked while app connected\r\n");
        return;
    }

    if (argc < 2) {
        app_debug_shell_cmd_puts("[DBG] usage: p2pchat <text>\r\n");
        return;
    }
    if (app_peer_table_copy(&peer, 1) != 1) {
        app_debug_shell_cmd_puts("[DBG] no peer\r\n");
        return;
    }

    for (i = 1; i < argc; i++) {
        if (i > 1) {
            if (len < APP_HOST_P2P_CHAT_TEXT_MAX_LEN) {
                plain[len++] = ' ';
            } else {
                truncated = 1;
            }
        }
        j = 0;
        while (argv[i][j] && len < APP_HOST_P2P_CHAT_TEXT_MAX_LEN) {
            plain[len++] = (u8)argv[i][j++];
        }
        if (argv[i][j]) {
            truncated = 1;
        }
    }

    if (!len) {
        app_debug_shell_cmd_puts("[DBG] usage: p2pchat <text>\r\n");
        return;
    }

    if (truncated) {
        app_debug_shell_cmd_puts("[DBG] p2pchat too long\r\n");
        app_debug_shell_cmd_print_u32(" max=", APP_HOST_P2P_CHAT_TEXT_MAX_LEN);
        return;
    }

    s_dbg_chat_nonce++;
    if (!s_dbg_chat_nonce) {
        s_dbg_chat_nonce = 1;
    }
    nonce = clock_time() ^ rand() ^ app_identity_get_short_id() ^ ((u32)s_dbg_chat_nonce << 16);
    st = app_crypto_chat_encrypt(app_identity_get_eid(), &peer.eid, nonce,
                                 plain, len,
                                 payload, sizeof(payload),
                                 &encrypted_len);
    if (st != APP_OK) {
        app_debug_shell_cmd_puts("[DBG] p2pchat encrypt\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    st = app_peer_transport_send_message(&peer.eid, APP_PEER_MSG_USER, payload, encrypted_len,
                                         APP_PEER_SEND_RELIABLE,
                                         APP_PEER_TRANSPORT_FRAME_FLAG_NOTIFY);
    app_debug_shell_cmd_puts("[DBG] p2pchat\r\n");
    app_debug_shell_cmd_print_u8(" st=", (u8)st);
    app_debug_shell_cmd_print_u32(" len=", len);
    app_debug_shell_cmd_print_u32(" enc=", encrypted_len);
}

static void cmd_p2pclear(u8 argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_peer_transport_debug_reset();
    app_debug_shell_cmd_puts("[DBG] p2p cleared\r\n");
}

static void cmd_p2pdrop(u8 argc, char **argv)
{
    u16 index;
    u32 mask;

    if (argc != 2 || !parse_u16_arg(argv[1], &index) ||
        index >= APP_PEER_TRANSPORT_MAX_FRAGMENTS) {
        app_debug_shell_cmd_puts("[DBG] usage: p2pdrop <frag_index>\r\n");
        app_debug_shell_cmd_print_u8(" max_frag=", APP_PEER_TRANSPORT_MAX_FRAGMENTS);
        return;
    }

    mask = ((u32)1 << index);
    app_peer_transport_debug_drop_next_rx(mask);
    app_debug_shell_cmd_puts("[DBG] p2pdrop armed\r\n");
    app_debug_shell_cmd_print_u8(" frag=", (u8)index);
    app_debug_shell_cmd_print_u8(" mask=", (u8)mask);
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

static void cmd_hoststat(u8 argc, char **argv)
{
    app_host_cmd_debug_t host;
    (void)argc;
    (void)argv;

    app_host_cmd_get_debug(&host);
    app_debug_shell_cmd_puts("[DBG] hoststat\r\n");
    app_debug_shell_cmd_print_u8(" ready=", host.host_ready);
    app_debug_shell_cmd_print_u32(" rx_frame=", host.rx_frame_count);
    app_debug_shell_cmd_print_u32(" rx_msg=", host.rx_message_count);
    app_debug_shell_cmd_print_u32(" cmd_count=", host.cmd_count);
    app_debug_shell_cmd_print_u32(" crc_err=", host.crc_error_count);
    app_debug_shell_cmd_print_u8(" last_st=", host.last_rx_status);
    app_debug_shell_cmd_print_u8(" last_seq=", host.last_rx_seq);
    app_debug_shell_cmd_print_u8(" last_cmd=", host.last_rx_cmd);
    app_debug_shell_cmd_print_u8(" last_frag=", host.last_rx_frag);
    app_debug_shell_cmd_print_u8(" last_frag_n=", host.last_rx_frag_count);
    app_debug_shell_cmd_print_u8(" last_len=", host.last_rx_frame_len);
    app_debug_shell_cmd_print_u8(" chat_pending=", host.p2p_chat_event_pending);
    app_debug_shell_cmd_print_u8(" chat_flags=", host.p2p_chat_event_flags);
    app_debug_shell_cmd_print_u8(" chat_last_st=", host.p2p_chat_event_last_status);
    app_debug_shell_cmd_print_u32(" evt_len=", host.p2p_chat_event_len);
    app_debug_shell_cmd_print_u32(" text_len=", host.p2p_chat_event_text_len);
    app_debug_shell_cmd_print_u32(" chat_rx=", host.p2p_chat_event_rx_count);
    app_debug_shell_cmd_print_u32(" chat_drop=", host.p2p_chat_event_drop_count);
    app_debug_shell_cmd_print_u32(" chat_sent=", host.p2p_chat_event_sent_count);
}

static void cmd_radio(u8 argc, char **argv)
{
    app_ble_debug_t ble;
    (void)argc;
    (void)argv;

    app_ble_get_debug(&ble);
    app_debug_shell_cmd_puts("[DBG] radio\r\n");
    app_debug_shell_cmd_print_u8(" conn=", ble.connected);
    app_debug_shell_cmd_print_u32(" handle=", ble.conn_handle);
    app_debug_shell_cmd_print_u32(" last_handle=", ble.last_conn_handle);
    app_debug_shell_cmd_print_u32(" ci=", ble.conn_interval);
    app_debug_shell_cmd_print_u32(" lat=", ble.conn_latency);
    app_debug_shell_cmd_print_u32(" to=", ble.conn_timeout);
    app_debug_shell_cmd_print_u8(" disc_r=", ble.last_disconnect_reason);
    app_debug_shell_cmd_print_u32(" disc_n=", ble.disconnect_count);
    app_debug_shell_cmd_print_u32(" stop_n=", ble.stop_count);
    app_debug_shell_cmd_print_u8(" started=", ble.started);
    app_debug_shell_cmd_print_u8(" adv0_en=", ble.adv0_enabled);
    app_debug_shell_cmd_print_u8(" adv1_en=", ble.adv1_enabled);
    app_debug_shell_cmd_print_u8(" scan_en=", ble.scan_enabled);
    app_debug_shell_cmd_print_u8(" adv0=", ble.adv0_status);
    app_debug_shell_cmd_print_u8(" adv1=", ble.adv1_status);
    app_debug_shell_cmd_print_u8(" adv1_param=", ble.adv1_param_status);
    app_debug_shell_cmd_print_u8(" adv1_sid=", ble.adv1_sid);
    app_debug_shell_cmd_print_u8(" adv1_a0=", ble.adv1_addr0);
    app_debug_shell_cmd_print_u8(" adv1_a5=", ble.adv1_addr5);
    app_debug_shell_cmd_print_u8(" scan=", ble.scan_status);
    app_debug_shell_cmd_print_u8(" scan_param=", ble.scan_param_status);
    app_debug_shell_cmd_print_u8(" scan_fp=", ble.scan_filter_policy);
    app_debug_shell_cmd_print_u8(" rx_decode=", app_radio_debug_rx_decode_enabled());
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

static void cmd_ble(u8 argc, char **argv)
{
    app_status_t st;

    if (argc == 2 && str_eq(argv[1], "start")) {
        st = app_ble_start_adv_scan(0);
        app_debug_shell_cmd_puts("[DBG] ble start\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "stop")) {
        st = app_ble_stop_adv_scan();
        app_debug_shell_cmd_puts("[DBG] ble stop\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "adv0-on")) {
        st = app_ble_set_adv0_enabled(1);
        app_debug_shell_cmd_puts("[DBG] adv0 on\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "adv0-off")) {
        st = app_ble_set_adv0_enabled(0);
        app_debug_shell_cmd_puts("[DBG] adv0 off\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "adv1-on")) {
        st = app_ble_set_adv1_enabled(1);
        app_debug_shell_cmd_puts("[DBG] adv1 on\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "adv1-off")) {
        st = app_ble_set_adv1_enabled(0);
        app_debug_shell_cmd_puts("[DBG] adv1 off\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "scan-on")) {
        st = app_ble_set_scan_enabled(1);
        app_debug_shell_cmd_puts("[DBG] scan on\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    if (argc == 2 && str_eq(argv[1], "scan-off")) {
        st = app_ble_set_scan_enabled(0);
        app_debug_shell_cmd_puts("[DBG] scan off\r\n");
        app_debug_shell_cmd_print_u8(" st=", (u8)st);
        return;
    }

    app_debug_shell_cmd_puts("[DBG] usage: ble [start|stop|adv0-on|adv0-off|adv1-on|adv1-off|scan-on|scan-off]\r\n");
}

static void cmd_rx(u8 argc, char **argv)
{
    if (argc == 2 && str_eq(argv[1], "on")) {
        app_radio_debug_set_rx_decode_enabled(1);
        app_debug_shell_cmd_puts("[DBG] rx on\r\n");
        return;
    }

    if (argc == 2 && str_eq(argv[1], "off")) {
        app_radio_debug_set_rx_decode_enabled(0);
        app_debug_shell_cmd_puts("[DBG] rx off\r\n");
        return;
    }

    app_debug_shell_cmd_puts("[DBG] rx\r\n");
    app_debug_shell_cmd_print_u8(" decode=", app_radio_debug_rx_decode_enabled());
}

static void cmd_wl(u8 argc, char **argv)
{
    app_ble_debug_t ble;
    app_status_t st_clear;
    app_status_t st_add = APP_OK;
    u8 addr[6];

    if (argc == 1) {
        app_ble_get_debug(&ble);
        app_debug_shell_cmd_puts("[DBG] wl\r\n");
        app_debug_shell_cmd_print_u8(" fp=", ble.scan_filter_policy);
        return;
    }

    app_ble_get_debug(&ble);
    if (ble.scan_enabled) {
        app_ble_set_scan_enabled(0);
    }

    if (argc == 2 && (str_eq(argv[1], "off") || str_eq(argv[1], "clear"))) {
        st_clear = app_ble_whitelist_clear();
        app_ble_set_scan_whitelist_enabled(0);
        app_debug_shell_cmd_puts("[DBG] wl off\r\n");
        app_debug_shell_cmd_print_u8(" clear=", (u8)st_clear);
        app_debug_shell_cmd_puts("[DBG] run: ble scan-on\r\n");
        return;
    }

    if (argc == 3 && str_eq(argv[1], "add")) {
        if (!parse_mac_arg(argv[2], addr)) {
            app_debug_shell_cmd_puts("[DBG] usage: wl add a4c1389e9882\r\n");
            return;
        }
        st_clear = app_ble_whitelist_clear();
        if (st_clear == APP_OK) {
            st_add = app_ble_whitelist_add_public(addr);
        }
        if (st_clear == APP_OK && st_add == APP_OK) {
            app_ble_set_scan_whitelist_enabled(1);
        }
        app_debug_shell_cmd_puts("[DBG] wl add\r\n");
        app_debug_shell_cmd_print_u8(" clear=", (u8)st_clear);
        app_debug_shell_cmd_print_u8(" add=", (u8)st_add);
        app_debug_shell_cmd_puts("[DBG] run: ble scan-on\r\n");
        return;
    }

    app_debug_shell_cmd_puts("[DBG] usage: wl [off|clear|add <mac>]\r\n");
}

static void cmd_mac(u8 argc, char **argv)
{
    u8 i;
    (void)argc;
    (void)argv;

    app_debug_shell_cmd_puts("[DBG] mac public\r\n");
    for (i = 0; i < 6; i++) {
        app_debug_shell_cmd_print_u8(" b=", app_radio_debug_public_mac_byte(i));
    }
    app_debug_shell_cmd_puts("[DBG] mac random\r\n");
    for (i = 0; i < 6; i++) {
        app_debug_shell_cmd_print_u8(" b=", app_radio_debug_random_mac_byte(i));
    }
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

    if (str_eq(argv[0], "hoststat")) {
        cmd_hoststat(argc, argv);
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
    app_debug_shell_cmd_register("profile", "profile [set <nick> <signature>]", "show or update profile card", cmd_profile);
    app_debug_shell_cmd_register("peers", "peers", "show peer table", cmd_peers);
    app_debug_shell_cmd_register("clear", "clear", "clear peer table", cmd_clear);
    app_debug_shell_cmd_register("logs", "logs", "reset debug counters", cmd_logs);
    app_debug_shell_cmd_register("rxstat", "rxstat", "show scan decode counters", cmd_rxstat);
    app_debug_shell_cmd_register("host", "host", "show host transport counters", cmd_hoststat);
    app_debug_shell_cmd_register("hoststat", "hoststat", "show host transport counters", cmd_hoststat);
    app_debug_shell_cmd_register("p2pstat", "p2pstat", "show peer transport counters", cmd_p2pstat);
    app_debug_shell_cmd_register("p2psend", "p2psend [len]", "send peer test message", cmd_p2psend);
    app_debug_shell_cmd_register("p2pchat", "p2pchat <text>", "send reliable peer chat text", cmd_p2pchat);
    app_debug_shell_cmd_register("p2pclear", "p2pclear", "clear peer transport counters", cmd_p2pclear);
    app_debug_shell_cmd_register("p2pdrop", "p2pdrop <frag>", "drop first RX round for one fragment", cmd_p2pdrop);
    app_debug_shell_cmd_register("txstat", "txstat", "show adv scheduler counters", cmd_txstat);
    app_debug_shell_cmd_register("radio", "radio", "show BLE radio state", cmd_radio);
    app_debug_shell_cmd_register("ble", "ble [start|stop|adv0-on|adv1-on|scan-on]", "control BLE radio", cmd_ble);
    app_debug_shell_cmd_register("rx", "rx [on|off]", "control scan decode", cmd_rx);
    app_debug_shell_cmd_register("wl", "wl [off|add <mac>]", "control scan whitelist", cmd_wl);
    app_debug_shell_cmd_register("mac", "mac", "show BLE mac", cmd_mac);
    app_debug_shell_cmd_register("disc", "disc", "disconnect GATT", cmd_disc);
    app_debug_shell_cmd_register("reset", "reset", "software reset", cmd_reset);
}
