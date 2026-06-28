#include "app_host_cmd.h"
#include "../host_frame/app_host_frame.h"
#include "../host_transport/app_host_transport.h"
#include "../board/app_board.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "../battery/app_battery.h"
#include "../charge/app_charge.h"
#include "../config/app_config_store.h"
#include "../storage/app_storage.h"
#include "../crypto/app_crypto.h"
#include "../factory/app_factory.h"
#include "../profile/app_profile.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../peer_table/app_peer_table.h"
#include "../peer_transport/app_peer_transport.h"
#include "../motor/app_motor.h"
#include "../pm/app_pm.h"
#include "../ble/app_ble.h"
#include "../debug_shell/app_debug_shell_cmd.h"
#include "common/string.h"
#include "drivers.h"

#define HOST_SHELL_RSP_MAX_LEN 72
#define HOST_SHELL_RSP_BODY_MAX_LEN 57
#define HOST_P2P_CHAT_EVENT_HEADER_LEN APP_HOST_P2P_CHAT_EVENT_HEADER_LEN
#define HOST_P2P_CHAT_EVENT_FLAG_TRUNCATED 0x01
#define HOST_P2P_CHAT_EVENT_FLAG_DROPPED   0x02
#define HOST_P2P_CHAT_DELIVERY_TTL_US      5000000
#define HOST_P2P_CHAT_DROP_NOTICE_TTL_US   60000000
#define HOST_P2P_CHAT_PLAIN_MAX_LEN        APP_HOST_P2P_CHAT_TEXT_MAX_LEN
#define HOST_P2P_CHAT_SEND_TARGET_MODE     0x01
#define HOST_P2P_CHAT_SEND_TARGET_HDR_LEN  5
#define HOST_P2P_CHAT_REJECT_APP_CONNECTED 0x01
#define HOST_P2P_FILE_EVENT_HEADER_LEN     APP_HOST_P2P_FILE_EVENT_HEADER_LEN
#define HOST_P2P_FILE_FRAME_MAX_LEN        APP_HOST_P2P_FILE_FRAME_MAX_LEN
#define HOST_P2P_FILE_SEND_TARGET_MODE     0x01
#define HOST_P2P_FILE_SEND_TARGET_HDR_LEN  5
#define HOST_PROFILE_LIST_RSP_MAX_LEN      72

#define HOST_LOG_LEVEL_INFO  1
#define HOST_LOG_LEVEL_ERROR 2

typedef struct {
    u8 active;
    u8 seq;
    u8 cmd;
    u8 frag_count;
    u8 next_frag;
    u16 len;
    u8 buf[APP_HOST_MESSAGE_MAX_LEN];
} host_rx_assembly_t;

static host_rx_assembly_t s_rx;
static u8 s_tx_seq;
static u8 s_log_enable;
static u16 s_log_count;
static u16 s_cmd_count;
static u16 s_crc_error_count;
static u16 s_rx_frame_count;
static u16 s_rx_message_count;
static u8 s_last_rx_status;
static u8 s_last_rx_seq;
static u8 s_last_rx_cmd;
static u8 s_last_rx_frag;
static u8 s_last_rx_frag_count;
static u8 s_last_rx_frame_len;
static u8 s_rsp_buf[APP_HOST_MESSAGE_MAX_LEN];
static u8 s_evt_buf[APP_HOST_MESSAGE_MAX_LEN];
static u16 s_evt_len;
static u8 s_p2p_chat_event_pending;
static u32 s_p2p_chat_event_tick;
static u8 s_p2p_chat_event_last_status;
static u16 s_p2p_chat_event_rx_count;
static u16 s_p2p_chat_event_drop_count;
static u16 s_p2p_chat_event_sent_count;
static u16 s_p2p_chat_nonce;

typedef struct {
    u8 active;
    u8 seq;
    u8 cmd;
    u16 len;
    u32 target_short_id;
    union {
        char line[64];
        u8 payload[APP_HOST_MESSAGE_MAX_LEN];
    } data;
} host_pending_t;

typedef union {
    host_pending_t pending;
    app_peer_record_t peer_snapshot[APP_PEER_MAX_COUNT];
    app_profile_peer_record_t profile_snapshot[APP_PROFILE_PEER_CACHE_COUNT];
} host_cmd_scratch_t;

static host_cmd_scratch_t s_scratch;
#define s_pending (s_scratch.pending)

static u8 s_reboot_pending;
static u32 s_reboot_tick;
static u8 s_disconnect_pending;
static u32 s_disconnect_tick;

typedef struct {
    u8 *buf;
    u16 len;
    u16 max;
    u8 truncated;
} host_shell_capture_t;

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u8 chr_lower(u8 ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (u8)(ch + ('a' - 'A'));
    }
    return ch;
}

static u8 bytes_eq_word_ci(const u8 *payload, u16 len, const char *word)
{
    u16 start = 0;
    u16 end = len;
    u16 i = 0;

    if (!payload || !word) {
        return 0;
    }
    while (start < end && (payload[start] == ' ' || payload[start] == '\t' || payload[start] == '\r' || payload[start] == '\n')) {
        start++;
    }
    while (end > start && (payload[end - 1] == ' ' || payload[end - 1] == '\t' || payload[end - 1] == '\r' || payload[end - 1] == '\n')) {
        end--;
    }

    while (word[i]) {
        if (start + i >= end || chr_lower(payload[start + i]) != (u8)word[i]) {
            return 0;
        }
        i++;
    }
    return start + i == end;
}

static u32 p2p_chat_short_id_from_eid(const app_eid_t *eid)
{
    app_peer_record_t *peer;

    if (!eid) {
        return 0;
    }

    peer = app_peer_table_find(eid);
    return peer ? peer->short_id : 0;
}

static void notify_p2p_chat_plain(u32 short_id, s8 rssi, const u8 *text, u16 len)
{
    u16 copy_len;
    u8 flags = 0;

    if (!text || !len) {
        return;
    }

    copy_len = len;
    if (copy_len > APP_HOST_P2P_CHAT_TEXT_MAX_LEN) {
        copy_len = APP_HOST_P2P_CHAT_TEXT_MAX_LEN;
        flags |= HOST_P2P_CHAT_EVENT_FLAG_TRUNCATED;
    }

    wr32(&s_evt_buf[0], short_id);
    s_evt_buf[4] = (u8)rssi;
    s_evt_buf[5] = flags;
    wr16(&s_evt_buf[6], len);
    memcpy(&s_evt_buf[HOST_P2P_CHAT_EVENT_HEADER_LEN], text, copy_len);
    s_evt_len = (u16)(HOST_P2P_CHAT_EVENT_HEADER_LEN + copy_len);
    s_p2p_chat_event_pending = 1;
    s_p2p_chat_event_tick = clock_time();
    s_p2p_chat_event_rx_count++;
}

static void shell_capture_write(void *ctx, const char *text, u16 len)
{
    host_shell_capture_t *cap = (host_shell_capture_t *)ctx;
    u16 i;

    if (!cap || !text) {
        return;
    }

    for (i = 0; i < len; i++) {
        if (cap->len < cap->max) {
            cap->buf[cap->len++] = (u8)text[i];
        } else {
            cap->truncated = 1;
            return;
        }
    }
}

static void shell_capture_puts(host_shell_capture_t *cap, const char *text)
{
    u16 len = 0;
    if (!text) {
        return;
    }
    while (text[len]) {
        len++;
    }
    shell_capture_write(cap, text, len);
}

static u8 host_status_from_app(app_status_t st)
{
    switch (st) {
    case APP_OK:
        return HOST_STATUS_OK;
    case APP_ERR_PARAM:
        return HOST_STATUS_ERR_PARAM;
    case APP_ERR_BUSY:
        return HOST_STATUS_ERR_BUSY;
    case APP_ERR_NO_MEM:
        return HOST_STATUS_ERR_NO_MEM;
    case APP_ERR_CRC:
        return HOST_STATUS_ERR_CRC;
    case APP_ERR_UNSUPPORTED:
        return HOST_STATUS_ERR_UNSUPPORTED;
    case APP_ERR_PERMISSION:
        return HOST_STATUS_ERR_PERMISSION;
    case APP_ERR_NOT_FOUND:
        return HOST_STATUS_ERR_NOT_FOUND;
    case APP_ERR_FLASH:
        return HOST_STATUS_ERR_FLASH;
    default:
        return HOST_STATUS_ERR_STATE;
    }
}

static void send_rsp(u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    app_host_transport_send_message_with_seq(HOST_FRAME_TYPE_RSP, seq, cmd, status, payload, len);
}

static void handle_get_device_info(u8 seq)
{
    const app_board_info_t *board = app_board_get_info();
    const app_identity_info_t *id = app_identity_get_info();

    s_rsp_buf[0] = 1;
    s_rsp_buf[1] = (u8)board->hw_rev;
    s_rsp_buf[2] = 0;
    s_rsp_buf[3] = 1;
    s_rsp_buf[4] = 0;
    s_rsp_buf[5] = id->key_id;
    s_rsp_buf[6] = id->privacy_mode;
    s_rsp_buf[7] = 0;
    wr32(&s_rsp_buf[8], id->short_id);
    memcpy(&s_rsp_buf[12], id->current_eid.bytes, APP_EID_LEN);
    wr16(&s_rsp_buf[28], s_cmd_count);
    wr16(&s_rsp_buf[30], s_log_count);
    wr16(&s_rsp_buf[32], s_crc_error_count);
    s_rsp_buf[34] = APP_HOST_FRAME_VERSION;
    s_rsp_buf[35] = APP_ADV_PROTOCOL_VERSION;
    send_rsp(seq, HOST_CMD_GET_DEVICE_INFO, HOST_STATUS_OK, s_rsp_buf, 36);
}

static void handle_get_system_state(u8 seq)
{
    app_system_snapshot_t sys;
    app_battery_state_t bat;

    app_system_get_snapshot(&sys);
    app_battery_get_state(&bat);

    s_rsp_buf[0] = (u8)sys.state;
    s_rsp_buf[1] = (u8)sys.previous_state;
    wr16(&s_rsp_buf[2], sys.error_code);
    s_rsp_buf[4] = sys.wakeup_reason;
    s_rsp_buf[5] = app_host_transport_is_ready();
    wr32(&s_rsp_buf[6], sys.uptime_s);
    wr16(&s_rsp_buf[10], bat.voltage_mv);
    s_rsp_buf[12] = bat.percent;
    s_rsp_buf[13] = bat.low;
    s_rsp_buf[14] = bat.critical;
    s_rsp_buf[15] = (u8)app_charge_get_state();
    s_rsp_buf[16] = app_peer_table_count();
    s_rsp_buf[17] = app_motor_is_busy();
    s_rsp_buf[18] = s_log_enable;
    s_rsp_buf[19] = 0;
    send_rsp(seq, HOST_CMD_GET_SYSTEM_STATE, HOST_STATUS_OK, s_rsp_buf, 20);
}

static void handle_get_adv_frame(u8 seq)
{
    u8 len = 0;
    app_status_t st = app_adv_scheduler_build_next_adv_data(s_rsp_buf, APP_HOST_MESSAGE_MAX_LEN, &len);
    send_rsp(seq, HOST_CMD_GET_ADV_FRAME, host_status_from_app(st), s_rsp_buf, st == APP_OK ? len : 0);
}

static void handle_get_peer_table(u8 seq)
{
    u8 count;
    u8 i;
    u8 offset = 1;
    u8 included = 0;

    count = app_peer_table_copy(s_scratch.peer_snapshot, APP_PEER_MAX_COUNT);
    for (i = 0; i < count && offset + 8 <= APP_HOST_MESSAGE_MAX_LEN; i++) {
        wr32(&s_rsp_buf[offset], s_scratch.peer_snapshot[i].short_id);
        s_rsp_buf[offset + 4] = (u8)s_scratch.peer_snapshot[i].level;
        s_rsp_buf[offset + 5] = (u8)s_scratch.peer_snapshot[i].rssi;
        s_rsp_buf[offset + 6] = (u8)s_scratch.peer_snapshot[i].rssi_avg;
        s_rsp_buf[offset + 7] = s_scratch.peer_snapshot[i].flags;
        offset = (u8)(offset + 8);
        included++;
    }
    s_rsp_buf[0] = included;
    send_rsp(seq, HOST_CMD_GET_PEER_TABLE, HOST_STATUS_OK, s_rsp_buf, offset);
}

static void handle_get_profile_summary(u8 seq)
{
    u16 len = app_profile_build_host_payload(s_rsp_buf, APP_HOST_MESSAGE_MAX_LEN);
    if (!len) {
        send_rsp(seq, HOST_CMD_GET_PROFILE_SUMMARY, HOST_STATUS_ERR_STATE, 0, 0);
        return;
    }
    send_rsp(seq, HOST_CMD_GET_PROFILE_SUMMARY, HOST_STATUS_OK, s_rsp_buf, len);
}

static void handle_get_peer_profiles(u8 seq, const u8 *payload, u16 len)
{
    u8 count;
    u8 i;
    u8 included = 0;
    u8 start_index = 0;
    u8 active_index = 0;
    u8 next_index = 0xff;
    u16 offset = 1;

    if (payload && len) {
        start_index = payload[0];
    }

    count = app_profile_copy_peers(s_scratch.profile_snapshot, APP_PROFILE_PEER_CACHE_COUNT);
    for (i = 0; i < count; i++) {
        const app_profile_peer_record_t *cached = &s_scratch.profile_snapshot[i];
        const app_peer_record_t *peer = app_peer_table_find(&cached->eid);
        const app_peer_profile_t *profile = &cached->profile;
        u32 short_id;
        u8 level = PEER_LEVEL_NONE;
        s8 rssi = cached->rssi;
        s8 rssi_avg = cached->rssi;
        u8 peer_flags = 0;
        u16 need;

        if (!cached->in_use || !(profile->flags & APP_PROFILE_PEER_FLAG_VALID)) {
            continue;
        }
        if (!peer || peer->level == PEER_LEVEL_NONE) {
            continue;
        }
        if (active_index < start_index) {
            active_index++;
            continue;
        }
        need = (u16)(18 + APP_PROFILE_TAG_MAX_COUNT + profile->nickname_len + profile->signature_len);
        if (profile->nickname_len > APP_PROFILE_NICKNAME_MAX_LEN ||
            profile->signature_len > APP_PROFILE_SIGNATURE_MAX_LEN ||
            profile->tag_count > APP_PROFILE_TAG_MAX_COUNT ||
            offset + need + 1 > HOST_PROFILE_LIST_RSP_MAX_LEN) {
            next_index = active_index;
            break;
        }

        short_id = ((u32)cached->eid.bytes[0]) |
                   ((u32)cached->eid.bytes[1] << 8) |
                   ((u32)cached->eid.bytes[2] << 16) |
                   ((u32)cached->eid.bytes[3] << 24);
        if (peer) {
            level = (u8)peer->level;
            rssi = peer->rssi;
            rssi_avg = peer->rssi_avg;
            peer_flags = peer->flags;
        }

        wr32(&s_rsp_buf[offset], short_id); offset = (u16)(offset + 4);
        s_rsp_buf[offset++] = level;
        s_rsp_buf[offset++] = (u8)rssi;
        s_rsp_buf[offset++] = (u8)rssi_avg;
        s_rsp_buf[offset++] = peer_flags;
        s_rsp_buf[offset++] = profile->flags;
        wr16(&s_rsp_buf[offset], profile->seq); offset = (u16)(offset + 2);
        wr32(&s_rsp_buf[offset], profile->avatar_seed); offset = (u16)(offset + 4);
        s_rsp_buf[offset++] = profile->tag_count;
        s_rsp_buf[offset++] = profile->nickname_len;
        s_rsp_buf[offset++] = profile->signature_len;
        memcpy(&s_rsp_buf[offset], profile->tags, APP_PROFILE_TAG_MAX_COUNT); offset = (u16)(offset + APP_PROFILE_TAG_MAX_COUNT);
        if (profile->nickname_len) {
            memcpy(&s_rsp_buf[offset], profile->nickname, profile->nickname_len);
            offset = (u16)(offset + profile->nickname_len);
        }
        if (profile->signature_len) {
            memcpy(&s_rsp_buf[offset], profile->signature, profile->signature_len);
            offset = (u16)(offset + profile->signature_len);
        }
        included++;
        active_index++;
    }

    s_rsp_buf[0] = included;
    s_rsp_buf[offset++] = next_index;
    send_rsp(seq, HOST_CMD_GET_PEER_PROFILES, HOST_STATUS_OK, s_rsp_buf, offset);
}

static void handle_set_rssi_config(u8 seq, const u8 *payload, u16 len)
{
    app_runtime_config_t cfg;
    app_status_t st;

    if (!payload || len < 7) {
        send_rsp(seq, HOST_CMD_SET_RSSI_CONFIG, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    cfg = *app_config_get();
    cfg.rssi_t1 = (s8)payload[0];
    cfg.rssi_t2 = (s8)payload[1];
    cfg.rssi_t3 = (s8)payload[2];
    cfg.tin_ms = rd16(&payload[3]);
    cfg.tout_ms = rd16(&payload[5]);
    st = app_config_set(&cfg);
    app_adv_scheduler_request_beacon_update();
    send_rsp(seq, HOST_CMD_SET_RSSI_CONFIG, host_status_from_app(st), 0, 0);
}

static void handle_motor_test(u8 seq, const u8 *payload, u16 len)
{
    app_motor_pattern_t pattern;
    app_status_t st;

    if (!payload || !len) {
        send_rsp(seq, HOST_CMD_MOTOR_TEST, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    pattern = (app_motor_pattern_t)payload[0];
    st = app_motor_play(pattern);
    send_rsp(seq, HOST_CMD_MOTOR_TEST, host_status_from_app(st), 0, 0);
}

static void handle_log_enable(u8 seq, const u8 *payload, u16 len)
{
    if (!payload || !len) {
        send_rsp(seq, HOST_CMD_LOG_ENABLE, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    s_log_enable = payload[0] ? 1 : 0;
    send_rsp(seq, HOST_CMD_LOG_ENABLE, HOST_STATUS_OK, &s_log_enable, 1);
}

static void handle_debug_reset_stats(u8 seq)
{
    s_cmd_count = 0;
    s_crc_error_count = 0;
    s_log_count = 0;
    app_peer_table_clear();
    send_rsp(seq, HOST_CMD_DEBUG_RESET_STATS, HOST_STATUS_OK, 0, 0);
}

static void handle_enter_sleep(u8 seq)
{
    send_rsp(seq, HOST_CMD_ENTER_SLEEP, HOST_STATUS_OK, 0, 0);
    app_system_request_sleep(PM_SLEEP_REASON_APP_IDLE);
}

static void handle_get_flash_map(u8 seq)
{
    app_storage_flash_info_t info;
    const app_storage_partition_t *part;
    u8 i;
    u8 offset = 0;

    app_storage_get_flash_info(&info);
    wr32(&s_rsp_buf[offset], info.flash_mid); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.flash_vendor); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.flash_size); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.sdk_reserved_start); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.sdk_mac_addr); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.sdk_calibration_addr); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.sdk_smp_pairing_addr); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.sdk_master_pairing_addr); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.app_base_addr); offset = (u8)(offset + 4);
    wr32(&s_rsp_buf[offset], info.app_total_size); offset = (u8)(offset + 4);
    s_rsp_buf[offset++] = APP_STORAGE_PART_COUNT;

    for (i = 0; i < APP_STORAGE_PART_COUNT; i++) {
        part = app_storage_get_partition((app_storage_part_t)i);
        if (!part || offset + 9 > APP_HOST_MESSAGE_MAX_LEN) {
            break;
        }
        s_rsp_buf[offset++] = (u8)part->part;
        wr32(&s_rsp_buf[offset], part->addr); offset = (u8)(offset + 4);
        wr32(&s_rsp_buf[offset], part->size); offset = (u8)(offset + 4);
    }

    send_rsp(seq, HOST_CMD_GET_FLASH_MAP, HOST_STATUS_OK, s_rsp_buf, offset);
}

static u8 build_identity_payload(u8 *buf)
{
    const app_identity_info_t *id = app_identity_get_info();
    app_status_t st = app_identity_self_check();
    u8 flags = id->flags;

    if (st == APP_OK) {
        flags |= APP_IDENTITY_FLAG_VALID;
    } else {
        flags &= (u8)~APP_IDENTITY_FLAG_VALID;
    }

    buf[0] = 1;
    buf[1] = flags;
    wr16(&buf[2], id->crc16);
    memcpy(&buf[4], id->unique_id.bytes, APP_UNIQUE_ID_LEN);
    wr32(&buf[20], rd32(&id->unique_id.bytes[0]));
    wr32(&buf[24], rd32(&id->unique_id.bytes[4]));
    wr32(&buf[28], rd32(&id->unique_id.bytes[8]));
    wr32(&buf[32], rd32(&id->unique_id.bytes[12]));
    wr32(&buf[36], id->short_id);
    memcpy(&buf[40], id->current_eid.bytes, APP_EID_LEN);
    return 56;
}

static void send_identity_rsp(u8 seq, u8 cmd, app_status_t st)
{
    u8 len = 0;
    if (st == APP_OK) {
        len = build_identity_payload(s_rsp_buf);
    }
    send_rsp(seq, cmd, host_status_from_app(st), s_rsp_buf, len);
}

static void handle_get_identity(u8 seq)
{
    send_identity_rsp(seq, HOST_CMD_GET_IDENTITY, APP_OK);
}

static void handle_write_identity(u8 seq, const u8 *payload, u16 len)
{
    if (!payload || len < 17) {
        send_rsp(seq, HOST_CMD_WRITE_IDENTITY, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    if (len > sizeof(s_pending.data.payload)) {
        send_rsp(seq, HOST_CMD_WRITE_IDENTITY, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    if (s_pending.active) {
        send_rsp(seq, HOST_CMD_WRITE_IDENTITY, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_WRITE_IDENTITY;
    s_pending.len = len;
    memcpy(s_pending.data.payload, payload, len);
}

static void handle_lock_identity(u8 seq)
{
    if (s_pending.active) {
        send_rsp(seq, HOST_CMD_LOCK_IDENTITY, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_LOCK_IDENTITY;
    s_pending.len = 0;
}

static void handle_get_factory_info(u8 seq)
{
    app_factory_info_t info;
    app_status_t st;

    st = app_factory_get_info(&info);
    if (st != APP_OK) {
        send_rsp(seq, HOST_CMD_GET_FACTORY_INFO, host_status_from_app(st), 0, 0);
        return;
    }

    s_rsp_buf[0] = info.version;
    s_rsp_buf[1] = info.flags;
    wr16(&s_rsp_buf[2], info.crc16);
    wr32(&s_rsp_buf[4], info.write_count);
    wr32(&s_rsp_buf[8], info.lock_count);
    wr32(&s_rsp_buf[12], info.test_mask);
    wr32(&s_rsp_buf[16], info.result_mask);
    wr32(&s_rsp_buf[20], info.last_error);
    memcpy(&s_rsp_buf[24], info.last_unique_id.bytes, APP_UNIQUE_ID_LEN);
    send_rsp(seq, HOST_CMD_GET_FACTORY_INFO, HOST_STATUS_OK, s_rsp_buf, 40);
}

static void handle_run_factory_test(u8 seq, const u8 *payload, u16 len)
{
    if (len > sizeof(s_pending.data.payload)) {
        send_rsp(seq, HOST_CMD_RUN_FACTORY_TEST, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    if (s_pending.active) {
        send_rsp(seq, HOST_CMD_RUN_FACTORY_TEST, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_RUN_FACTORY_TEST;
    s_pending.len = len;
    if (payload && len) {
        memcpy(s_pending.data.payload, payload, len);
    }
}

static void handle_shell_exec(u8 seq, const u8 *payload, u16 len)
{
    u16 i;

    if (len >= sizeof(s_pending.data.line)) {
        send_rsp(seq, HOST_CMD_SHELL_EXEC, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    if (s_pending.active) {
        send_rsp(seq, HOST_CMD_SHELL_EXEC, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_SHELL_EXEC;

    for (i = 0; i < len; i++) {
        if (payload[i] == '\r' || payload[i] == '\n') {
            break;
        }
        s_pending.data.line[i] = (char)payload[i];
    }
    s_pending.data.line[i] = 0;
    s_pending.len = i;
}

static void handle_p2p_chat_send(u8 seq, const u8 *payload, u16 len)
{
    const u8 *text;
    u16 text_len;
    u32 target_short_id = 0;

    if (!payload || !len) {
        send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    text = payload;
    text_len = len;
    if (len > HOST_P2P_CHAT_SEND_TARGET_HDR_LEN &&
        payload[0] == HOST_P2P_CHAT_SEND_TARGET_MODE) {
        target_short_id = rd32(&payload[1]);
        text = &payload[HOST_P2P_CHAT_SEND_TARGET_HDR_LEN];
        text_len = (u16)(len - HOST_P2P_CHAT_SEND_TARGET_HDR_LEN);
        if (!target_short_id) {
            send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_PARAM, 0, 0);
            return;
        }
    }

    if (!text_len || text_len > HOST_P2P_CHAT_PLAIN_MAX_LEN ||
        text_len > sizeof(s_pending.data.payload)) {
        send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_P2P_CHAT_SEND;
    s_pending.len = text_len;
    s_pending.target_short_id = target_short_id;
    memcpy(s_pending.data.payload, text, text_len);
}

static void handle_p2p_file_send(u8 seq, const u8 *payload, u16 len)
{
    const u8 *frame;
    u16 frame_len;
    u32 target_short_id;

    if (!payload || len <= HOST_P2P_FILE_SEND_TARGET_HDR_LEN ||
        payload[0] != HOST_P2P_FILE_SEND_TARGET_MODE) {
        send_rsp(seq, HOST_CMD_P2P_FILE_SEND, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    target_short_id = rd32(&payload[1]);
    frame = &payload[HOST_P2P_FILE_SEND_TARGET_HDR_LEN];
    frame_len = (u16)(len - HOST_P2P_FILE_SEND_TARGET_HDR_LEN);
    if (!target_short_id || !frame_len || frame_len > HOST_P2P_FILE_FRAME_MAX_LEN ||
        frame_len > APP_PEER_TRANSPORT_MESSAGE_MAX_LEN) {
        send_rsp(seq, HOST_CMD_P2P_FILE_SEND, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_P2P_FILE_SEND;
    s_pending.len = frame_len;
    s_pending.target_short_id = target_short_id;
    memcpy(s_pending.data.payload, frame, frame_len);
}

static void handle_p2p_file_pending(void)
{
    app_peer_record_t *peer;
    app_status_t st;
    u8 seq;
    u16 frame_len;

    if (!s_pending.active || s_pending.cmd != HOST_CMD_P2P_FILE_SEND) {
        return;
    }

    seq = s_pending.seq;
    frame_len = s_pending.len;
    peer = app_peer_table_find_by_short_id(s_pending.target_short_id);
    if (!peer) {
        wr32(&s_rsp_buf[0], s_pending.target_short_id);
        s_rsp_buf[4] = app_peer_table_count();
        s_pending.active = 0;
        send_rsp(seq, HOST_CMD_P2P_FILE_SEND, HOST_STATUS_ERR_NOT_FOUND, s_rsp_buf, 5);
        return;
    }

    st = app_peer_transport_send_message(&peer->eid, APP_PEER_MSG_FILE,
                                         s_pending.data.payload, frame_len,
                                         APP_PEER_SEND_UNRELIABLE, 0);
    s_pending.active = 0;
    if (st != APP_OK) {
        send_rsp(seq, HOST_CMD_P2P_FILE_SEND, host_status_from_app(st), 0, 0);
        return;
    }

    s_rsp_buf[0] = 1;
    s_rsp_buf[1] = app_peer_table_count();
    wr16(&s_rsp_buf[2], frame_len);
    wr16(&s_rsp_buf[4], APP_PEER_TRANSPORT_MESSAGE_MAX_LEN);
    s_rsp_buf[6] = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    s_rsp_buf[7] = APP_PEER_TRANSPORT_MAX_FRAGMENTS;
    wr32(&s_rsp_buf[8], peer->short_id);
    send_rsp(seq, HOST_CMD_P2P_FILE_SEND, HOST_STATUS_OK, s_rsp_buf, 12);
}

static void handle_set_profile_summary(u8 seq, const u8 *payload, u16 len)
{
    if (!payload || !len || len > sizeof(s_pending.data.payload)) {
        send_rsp(seq, HOST_CMD_SET_PROFILE_SUMMARY, HOST_STATUS_ERR_PARAM, 0, 0);
        return;
    }
    if (s_pending.active) {
        send_rsp(seq, HOST_CMD_SET_PROFILE_SUMMARY, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.active = 1;
    s_pending.seq = seq;
    s_pending.cmd = HOST_CMD_SET_PROFILE_SUMMARY;
    s_pending.len = len;
    memcpy(s_pending.data.payload, payload, len);
}

static void handle_p2p_chat_pending(void)
{
    app_peer_record_t peer;
    app_status_t st;
    u8 encrypted[APP_HOST_P2P_CHAT_TEXT_MAX_LEN + APP_CHAT_CRYPTO_HEADER_LEN];
    u32 nonce;
    u8 seq;
    u16 len;
    u16 encrypted_len = 0;
    u8 peer_count;
    app_peer_record_t *target_peer;

    if (!s_pending.active || s_pending.cmd != HOST_CMD_P2P_CHAT_SEND) {
        return;
    }

    seq = s_pending.seq;
    len = s_pending.len;

    peer_count = app_peer_table_count();
    if (!peer_count) {
        s_pending.active = 0;
        send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_NOT_FOUND, 0, 0);
        return;
    }
    if (s_pending.target_short_id) {
        target_peer = app_peer_table_find_by_short_id(s_pending.target_short_id);
        if (!target_peer) {
            s_pending.active = 0;
            wr32(&s_rsp_buf[0], s_pending.target_short_id);
            s_rsp_buf[4] = peer_count;
            send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_NOT_FOUND, s_rsp_buf, 5);
            return;
        }
        peer = *target_peer;
    } else {
        if (peer_count != 1) {
            s_pending.active = 0;
            s_rsp_buf[0] = peer_count;
            send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_STATE, s_rsp_buf, 1);
            return;
        }
        if (app_peer_table_copy(&peer, 1) != 1) {
            s_pending.active = 0;
            send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_ERR_STATE, 0, 0);
            return;
        }
    }

    s_p2p_chat_nonce++;
    if (!s_p2p_chat_nonce) {
        s_p2p_chat_nonce = 1;
    }
    nonce = clock_time() ^ rand() ^ app_identity_get_short_id() ^ ((u32)s_p2p_chat_nonce << 16);
    st = app_crypto_chat_encrypt(app_identity_get_eid(), &peer.eid, nonce,
                                 s_pending.data.payload, len,
                                 encrypted, sizeof(encrypted),
                                 &encrypted_len);
    if (st != APP_OK) {
        s_pending.active = 0;
        send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, host_status_from_app(st), 0, 0);
        return;
    }

    st = app_peer_transport_send_message(&peer.eid, APP_PEER_MSG_USER, encrypted, encrypted_len,
                                         APP_PEER_SEND_RELIABLE,
                                         APP_PEER_TRANSPORT_FRAME_FLAG_NOTIFY);
    s_pending.active = 0;
    if (st != APP_OK) {
        send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, host_status_from_app(st), 0, 0);
        return;
    }

    s_rsp_buf[0] = 1;
    s_rsp_buf[1] = app_peer_table_count();
    wr16(&s_rsp_buf[2], len);
    wr16(&s_rsp_buf[4], HOST_P2P_CHAT_PLAIN_MAX_LEN);
    s_rsp_buf[6] = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    s_rsp_buf[7] = APP_PEER_TRANSPORT_MAX_FRAGMENTS;
    wr32(&s_rsp_buf[8], peer.short_id);
    send_rsp(seq, HOST_CMD_P2P_CHAT_SEND, HOST_STATUS_OK, s_rsp_buf, 12);
}

static void handle_shell_pending(void)
{
    host_shell_capture_t cap;
    u8 seq;

    if (!s_pending.active || s_pending.cmd != HOST_CMD_SHELL_EXEC) {
        return;
    }

    seq = s_pending.seq;
    s_pending.active = 0;

    if (bytes_eq_word_ci((const u8 *)s_pending.data.line, s_pending.len, "reset")) {
        s_rsp_buf[0] = '[';
        s_rsp_buf[1] = 'D';
        s_rsp_buf[2] = 'B';
        s_rsp_buf[3] = 'G';
        s_rsp_buf[4] = ']';
        s_rsp_buf[5] = ' ';
        s_rsp_buf[6] = 'r';
        s_rsp_buf[7] = 'e';
        s_rsp_buf[8] = 's';
        s_rsp_buf[9] = 'e';
        s_rsp_buf[10] = 't';
        s_rsp_buf[11] = '\r';
        s_rsp_buf[12] = '\n';
        send_rsp(seq, HOST_CMD_SHELL_EXEC, HOST_STATUS_OK, s_rsp_buf, 13);
        s_reboot_pending = 1;
        s_reboot_tick = clock_time();
        return;
    }

    if (bytes_eq_word_ci((const u8 *)s_pending.data.line, s_pending.len, "disc")) {
        s_rsp_buf[0] = '[';
        s_rsp_buf[1] = 'D';
        s_rsp_buf[2] = 'B';
        s_rsp_buf[3] = 'G';
        s_rsp_buf[4] = ']';
        s_rsp_buf[5] = ' ';
        s_rsp_buf[6] = 'd';
        s_rsp_buf[7] = 'i';
        s_rsp_buf[8] = 's';
        s_rsp_buf[9] = 'c';
        s_rsp_buf[10] = 'o';
        s_rsp_buf[11] = 'n';
        s_rsp_buf[12] = 'n';
        s_rsp_buf[13] = 'e';
        s_rsp_buf[14] = 'c';
        s_rsp_buf[15] = 't';
        s_rsp_buf[16] = '\r';
        s_rsp_buf[17] = '\n';
        send_rsp(seq, HOST_CMD_SHELL_EXEC, HOST_STATUS_OK, s_rsp_buf, 18);
        s_disconnect_pending = 1;
        s_disconnect_tick = clock_time();
        return;
    }

    cap.buf = s_rsp_buf;
    cap.len = 0;
    cap.max = HOST_SHELL_RSP_BODY_MAX_LEN;
    cap.truncated = 0;
    memset(s_rsp_buf, 0, APP_HOST_MESSAGE_MAX_LEN);

    app_debug_shell_cmd_execute_with_writer(s_pending.data.line, shell_capture_write, &cap);
    if (cap.truncated) {
        cap.max = HOST_SHELL_RSP_MAX_LEN;
        shell_capture_puts(&cap, "\r\n[truncated]\r\n");
    }
    send_rsp(seq, HOST_CMD_SHELL_EXEC, HOST_STATUS_OK, s_rsp_buf, cap.len);
}

static void handle_flash_pending(void)
{
    app_unique_id_t id;
    app_profile_summary_t profile;
    app_status_t st;
    u32 test_mask;
    u32 result_mask = 0;
    u8 payload[20];
    u16 payload_len;
    u8 seq;
    u8 cmd;
    u8 lock_after_write;

    if (!s_pending.active || s_pending.cmd == HOST_CMD_SHELL_EXEC) {
        return;
    }

    if (s_pending.cmd == HOST_CMD_P2P_CHAT_SEND) {
        handle_p2p_chat_pending();
        return;
    }

    if (s_pending.cmd == HOST_CMD_P2P_FILE_SEND) {
        handle_p2p_file_pending();
        return;
    }

    if (s_pending.cmd == HOST_CMD_SET_PROFILE_SUMMARY) {
        seq = s_pending.seq;
        st = app_profile_parse_host_payload(s_pending.data.payload, s_pending.len, &profile);
        if (st == APP_OK) {
            st = app_profile_set_summary(&profile);
        }
        s_pending.active = 0;
        if (st == APP_OK) {
            app_adv_scheduler_request_beacon_update();
            handle_get_profile_summary(seq);
        } else {
            send_rsp(seq, HOST_CMD_SET_PROFILE_SUMMARY, host_status_from_app(st), 0, 0);
        }
        return;
    }

    seq = s_pending.seq;
    cmd = s_pending.cmd;
    payload_len = s_pending.len;
    if (payload_len > sizeof(payload)) {
        payload_len = sizeof(payload);
    }
    if (payload_len) {
        memcpy(payload, s_pending.data.payload, payload_len);
    }
    s_pending.active = 0;

    if (cmd == HOST_CMD_WRITE_IDENTITY) {
        if (payload_len < 17) {
            send_rsp(seq, HOST_CMD_WRITE_IDENTITY, HOST_STATUS_ERR_PARAM, 0, 0);
            return;
        }

        lock_after_write = payload[0] & 0x01;
        memcpy(id.bytes, &payload[1], APP_UNIQUE_ID_LEN);
        st = app_factory_write_unique_id(&id);
        if (st == APP_OK && lock_after_write) {
            st = app_factory_lock_identity();
        }
        if (st == APP_OK) {
            app_adv_scheduler_request_beacon_update();
        }
        send_identity_rsp(seq, HOST_CMD_WRITE_IDENTITY, st);
        return;
    }

    if (cmd == HOST_CMD_LOCK_IDENTITY) {
        st = app_factory_lock_identity();
        if (st == APP_OK) {
            app_adv_scheduler_request_beacon_update();
        }
        send_identity_rsp(seq, HOST_CMD_LOCK_IDENTITY, st);
        return;
    }

    if (cmd == HOST_CMD_RUN_FACTORY_TEST) {
        test_mask = 0xffffffff;
        if (payload_len >= 4) {
            test_mask = rd32(payload);
        }
        st = app_factory_run_self_test(test_mask, &result_mask);
        s_rsp_buf[0] = 1;
        wr32(&s_rsp_buf[1], test_mask);
        wr32(&s_rsp_buf[5], result_mask);
        send_rsp(seq, HOST_CMD_RUN_FACTORY_TEST, host_status_from_app(st), s_rsp_buf, st == APP_OK ? 9 : 0);
        return;
    }
}

static void handle_p2p_chat_event_pending(void)
{
    app_status_t st;

    if (!s_p2p_chat_event_pending) {
        return;
    }

    if (s_evt_buf[5] & HOST_P2P_CHAT_EVENT_FLAG_DROPPED) {
        if (clock_time_exceed(s_p2p_chat_event_tick, HOST_P2P_CHAT_DROP_NOTICE_TTL_US)) {
            s_p2p_chat_event_pending = 0;
            s_evt_len = 0;
            return;
        }
    } else if (clock_time_exceed(s_p2p_chat_event_tick, HOST_P2P_CHAT_DELIVERY_TTL_US)) {
        s_evt_buf[5] |= HOST_P2P_CHAT_EVENT_FLAG_DROPPED;
        s_evt_len = HOST_P2P_CHAT_EVENT_HEADER_LEN;
        s_p2p_chat_event_tick = clock_time();
        s_p2p_chat_event_drop_count++;
    }

    st = app_host_transport_send_message(HOST_FRAME_TYPE_EVENT, HOST_EVENT_P2P_CHAT,
                                         HOST_STATUS_OK, s_evt_buf, s_evt_len);
    s_p2p_chat_event_last_status = (u8)st;
    if (st == APP_OK) {
        s_p2p_chat_event_pending = 0;
        s_evt_len = 0;
        s_p2p_chat_event_sent_count++;
    }
}

static void handle_command(u8 seq, u8 cmd, const u8 *payload, u16 len)
{
    s_cmd_count++;
    if (s_pending.active) {
        send_rsp(seq, cmd, HOST_STATUS_ERR_BUSY, 0, 0);
        return;
    }

    switch (cmd) {
    case HOST_CMD_GET_DEVICE_INFO:
        handle_get_device_info(seq);
        break;
    case HOST_CMD_GET_SYSTEM_STATE:
        handle_get_system_state(seq);
        break;
    case HOST_CMD_GET_ADV_FRAME:
        handle_get_adv_frame(seq);
        break;
    case HOST_CMD_GET_PEER_TABLE:
        handle_get_peer_table(seq);
        break;
    case HOST_CMD_GET_PROFILE_SUMMARY:
        handle_get_profile_summary(seq);
        break;
    case HOST_CMD_GET_PEER_PROFILES:
        handle_get_peer_profiles(seq, payload, len);
        break;
    case HOST_CMD_SET_RSSI_CONFIG:
        handle_set_rssi_config(seq, payload, len);
        break;
    case HOST_CMD_MOTOR_TEST:
        handle_motor_test(seq, payload, len);
        break;
    case HOST_CMD_LOG_ENABLE:
        handle_log_enable(seq, payload, len);
        break;
    case HOST_CMD_DEBUG_RESET_STATS:
        handle_debug_reset_stats(seq);
        break;
    case HOST_CMD_ENTER_SLEEP:
        handle_enter_sleep(seq);
        break;
    case HOST_CMD_GET_FLASH_MAP:
        handle_get_flash_map(seq);
        break;
    case HOST_CMD_GET_IDENTITY:
        handle_get_identity(seq);
        break;
    case HOST_CMD_WRITE_IDENTITY:
        handle_write_identity(seq, payload, len);
        break;
    case HOST_CMD_LOCK_IDENTITY:
        handle_lock_identity(seq);
        break;
    case HOST_CMD_GET_FACTORY_INFO:
        handle_get_factory_info(seq);
        break;
    case HOST_CMD_RUN_FACTORY_TEST:
        handle_run_factory_test(seq, payload, len);
        break;
    case HOST_CMD_SHELL_EXEC:
        handle_shell_exec(seq, payload, len);
        break;
    case HOST_CMD_P2P_CHAT_SEND:
        handle_p2p_chat_send(seq, payload, len);
        break;
    case HOST_CMD_P2P_FILE_SEND:
        handle_p2p_file_send(seq, payload, len);
        break;
    case HOST_CMD_SET_PROFILE_SUMMARY:
        handle_set_profile_summary(seq, payload, len);
        break;
    default:
        send_rsp(seq, cmd, HOST_STATUS_ERR_UNSUPPORTED, 0, 0);
        break;
    }
}

void app_host_cmd_init(void)
{
    memset(&s_rx, 0, sizeof(s_rx));
    s_tx_seq = 1;
    s_log_enable = 1;
    s_log_count = 0;
    s_cmd_count = 0;
    s_crc_error_count = 0;
    s_rx_frame_count = 0;
    s_rx_message_count = 0;
    s_last_rx_status = APP_OK;
    s_last_rx_seq = 0;
    s_last_rx_cmd = 0;
    s_last_rx_frag = 0;
    s_last_rx_frag_count = 0;
    s_last_rx_frame_len = 0;
    s_evt_len = 0;
    s_p2p_chat_event_pending = 0;
    s_p2p_chat_event_tick = 0;
    s_p2p_chat_event_last_status = APP_OK;
    s_p2p_chat_event_rx_count = 0;
    s_p2p_chat_event_drop_count = 0;
    s_p2p_chat_event_sent_count = 0;
    memset(&s_pending, 0, sizeof(s_pending));
    s_reboot_pending = 0;
    s_reboot_tick = 0;
    s_disconnect_pending = 0;
    s_disconnect_tick = 0;
}

void app_host_cmd_poll(void)
{
    handle_flash_pending();
    handle_shell_pending();
    handle_p2p_chat_event_pending();
    if (s_disconnect_pending && clock_time_exceed(s_disconnect_tick, 300000)) {
        s_disconnect_pending = 0;
        app_ble_disconnect_app(0x13);
    }
    if (s_reboot_pending && clock_time_exceed(s_reboot_tick, 300000)) {
        start_reboot();
    }
}

u8 app_host_cmd_next_tx_seq(void)
{
    return s_tx_seq++;
}

void app_host_cmd_get_debug(app_host_cmd_debug_t *debug)
{
    if (!debug) {
        return;
    }

    memset(debug, 0, sizeof(*debug));
    debug->p2p_chat_event_pending = s_p2p_chat_event_pending;
    debug->p2p_chat_event_flags = s_p2p_chat_event_pending ? s_evt_buf[5] : 0;
    debug->p2p_chat_event_last_status = s_p2p_chat_event_last_status;
    debug->host_ready = app_host_transport_is_ready();
    debug->last_rx_status = s_last_rx_status;
    debug->last_rx_seq = s_last_rx_seq;
    debug->last_rx_cmd = s_last_rx_cmd;
    debug->last_rx_frag = s_last_rx_frag;
    debug->last_rx_frag_count = s_last_rx_frag_count;
    debug->last_rx_frame_len = s_last_rx_frame_len;
    debug->p2p_chat_event_len = s_evt_len;
    debug->p2p_chat_event_text_len = s_p2p_chat_event_pending ? rd16(&s_evt_buf[6]) : 0;
    debug->p2p_chat_event_rx_count = s_p2p_chat_event_rx_count;
    debug->p2p_chat_event_drop_count = s_p2p_chat_event_drop_count;
    debug->p2p_chat_event_sent_count = s_p2p_chat_event_sent_count;
    debug->rx_frame_count = s_rx_frame_count;
    debug->rx_message_count = s_rx_message_count;
    debug->cmd_count = s_cmd_count;
    debug->crc_error_count = s_crc_error_count;
}

void app_host_cmd_on_rx_frame(const u8 *data, u8 len)
{
    app_host_frame_rx_t frame;
    app_status_t st;

    s_rx_frame_count++;
    s_last_rx_frame_len = len;
    st = app_host_frame_decode(data, len, &frame);
    s_last_rx_status = (u8)st;
    if (st != APP_OK) {
        if (st == APP_ERR_CRC) {
            s_crc_error_count++;
        }
        return;
    }
    if (frame.type != HOST_FRAME_TYPE_CMD) {
        return;
    }
    s_last_rx_seq = frame.seq;
    s_last_rx_cmd = frame.cmd;
    s_last_rx_frag = frame.frag_index;
    s_last_rx_frag_count = frame.frag_count;

    if (frame.frag_index == 0) {
        memset(&s_rx, 0, sizeof(s_rx));
        s_rx.active = 1;
        s_rx.seq = frame.seq;
        s_rx.cmd = frame.cmd;
        s_rx.frag_count = frame.frag_count;
        s_rx.next_frag = 0;
        s_rx.len = 0;
    }

    if (!s_rx.active || s_rx.seq != frame.seq || s_rx.cmd != frame.cmd ||
        s_rx.frag_count != frame.frag_count || s_rx.next_frag != frame.frag_index) {
        send_rsp(frame.seq, frame.cmd, HOST_STATUS_ERR_PARAM, 0, 0);
        memset(&s_rx, 0, sizeof(s_rx));
        return;
    }

    if (s_rx.len + frame.payload_len > sizeof(s_rx.buf)) {
        send_rsp(frame.seq, frame.cmd, HOST_STATUS_ERR_NO_MEM, 0, 0);
        memset(&s_rx, 0, sizeof(s_rx));
        return;
    }

    if (frame.payload_len) {
        memcpy(&s_rx.buf[s_rx.len], frame.payload, frame.payload_len);
        s_rx.len = (u16)(s_rx.len + frame.payload_len);
    }
    s_rx.next_frag++;

    if (s_rx.next_frag >= s_rx.frag_count) {
        s_rx_message_count++;
        app_host_cmd_on_rx_message(HOST_FRAME_TYPE_CMD, s_rx.seq, s_rx.cmd, HOST_STATUS_OK, s_rx.buf, s_rx.len);
        memset(&s_rx, 0, sizeof(s_rx));
    }
}

void app_host_cmd_on_rx_message(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    (void)status;
    if (type != HOST_FRAME_TYPE_CMD) {
        return;
    }
    handle_command(seq, cmd, payload, len);
}

void app_host_cmd_log_text(u8 level, const char *tag, const char *msg)
{
    u8 payload[64];
    u8 offset = 4;
    u8 i = 0;

    if (!s_log_enable || !tag || !msg) {
        return;
    }

    payload[0] = level;
    wr16(&payload[1], ++s_log_count);
    payload[3] = 0;
    while (tag[i] && offset < sizeof(payload) - 2) {
        payload[offset++] = (u8)tag[i++];
    }
    payload[offset++] = ':';
    payload[offset++] = ' ';
    i = 0;
    while (msg[i] && offset < sizeof(payload)) {
        payload[offset++] = (u8)msg[i++];
    }
    app_host_transport_send_message(HOST_FRAME_TYPE_LOG, 0, HOST_STATUS_OK, payload, offset);
}

void app_host_cmd_notify_p2p_chat(const app_eid_t *src_eid, s8 rssi, const u8 *text, u16 len)
{
    notify_p2p_chat_plain(p2p_chat_short_id_from_eid(src_eid), rssi, text, len);
}

void app_host_cmd_notify_p2p_chat_encrypted(const app_eid_t *src_eid, s8 rssi, const u8 *payload, u16 len)
{
    u16 plain_len = 0;

    if (!payload || !len) {
        return;
    }

    if (app_crypto_chat_decrypt(app_identity_get_eid(), src_eid,
                                payload, len,
                                &s_evt_buf[HOST_P2P_CHAT_EVENT_HEADER_LEN],
                                APP_HOST_P2P_CHAT_TEXT_MAX_LEN,
                                &plain_len) != APP_OK ||
        !plain_len) {
        return;
    }

    wr32(&s_evt_buf[0], p2p_chat_short_id_from_eid(src_eid));
    s_evt_buf[4] = (u8)rssi;
    s_evt_buf[5] = 0;
    wr16(&s_evt_buf[6], plain_len);
    s_evt_len = (u16)(HOST_P2P_CHAT_EVENT_HEADER_LEN + plain_len);
    s_p2p_chat_event_pending = 1;
    s_p2p_chat_event_tick = clock_time();
    s_p2p_chat_event_rx_count++;
}

void app_host_cmd_notify_p2p_file(const app_eid_t *src_eid, s8 rssi, const u8 *payload, u16 len)
{
    u16 copy_len;

    if (!payload || !len) {
        return;
    }

    copy_len = len;
    if (copy_len > HOST_P2P_FILE_FRAME_MAX_LEN) {
        copy_len = HOST_P2P_FILE_FRAME_MAX_LEN;
    }

    wr32(&s_rsp_buf[0], p2p_chat_short_id_from_eid(src_eid));
    s_rsp_buf[4] = (u8)rssi;
    s_rsp_buf[5] = copy_len == len ? 0 : 0x01;
    wr16(&s_rsp_buf[6], len);
    memcpy(&s_rsp_buf[HOST_P2P_FILE_EVENT_HEADER_LEN], payload, copy_len);
    app_host_transport_send_message(HOST_FRAME_TYPE_EVENT, HOST_EVENT_P2P_FILE,
                                    HOST_STATUS_OK, s_rsp_buf,
                                    (u16)(HOST_P2P_FILE_EVENT_HEADER_LEN + copy_len));
}

void app_host_cmd_notify_p2p_chat_tx_result(const app_eid_t *dst_eid, u32 message_id,
                                            u16 len, app_status_t status, u8 flags)
{
    app_peer_record_t *peer;
    u32 short_id = 0;

    if (dst_eid) {
        peer = app_peer_table_find(dst_eid);
        if (peer) {
            short_id = peer->short_id;
        }
    }

    wr32(&s_rsp_buf[0], short_id);
    s_rsp_buf[4] = host_status_from_app(status);
    s_rsp_buf[5] = (u8)status;
    s_rsp_buf[6] = flags;
    s_rsp_buf[7] = 0;
    wr16(&s_rsp_buf[8], len);
    wr32(&s_rsp_buf[10], message_id);
    app_host_transport_send_message(HOST_FRAME_TYPE_EVENT, HOST_EVENT_P2P_CHAT_TX_RESULT,
                                    HOST_STATUS_OK, s_rsp_buf, 14);
}

void app_host_cmd_notify_peer_level(const app_eid_t *eid, u8 old_level, u8 new_level, s8 rssi_avg, u8 reason)
{
    u8 payload[24];

    if (!eid) {
        return;
    }

    memcpy(payload, eid->bytes, APP_EID_LEN);
    payload[16] = old_level;
    payload[17] = new_level;
    payload[18] = (u8)rssi_avg;
    payload[19] = reason;
    app_host_transport_send_message(HOST_FRAME_TYPE_EVENT, HOST_EVENT_PEER_LEVEL, HOST_STATUS_OK, payload, 20);
}

void app_host_cmd_notify_error(u16 error_code, u16 detail)
{
    u8 payload[4];
    wr16(&payload[0], error_code);
    wr16(&payload[2], detail);
    app_host_transport_send_message(HOST_FRAME_TYPE_EVENT, HOST_EVENT_ERROR, HOST_STATUS_OK, payload, sizeof(payload));
    app_host_cmd_log_text(HOST_LOG_LEVEL_ERROR, "ERR", "system error");
}
