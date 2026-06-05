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
#include "../factory/app_factory.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../peer_table/app_peer_table.h"
#include "../motor/app_motor.h"
#include "../pm/app_pm.h"
#include "common/string.h"

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
static u8 s_rsp_buf[APP_ADV_FRAME_MAX_LEN];
static app_peer_record_t s_peer_snapshot[APP_PEER_MAX_COUNT];

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
    app_status_t st = app_adv_scheduler_build_next_adv_data(s_rsp_buf, APP_ADV_FRAME_MAX_LEN, &len);
    send_rsp(seq, HOST_CMD_GET_ADV_FRAME, host_status_from_app(st), s_rsp_buf, st == APP_OK ? len : 0);
}

static void handle_get_peer_table(u8 seq)
{
    u8 count;
    u8 i;
    u8 offset = 1;
    u8 included = 0;

    count = app_peer_table_copy(s_peer_snapshot, APP_PEER_MAX_COUNT);
    for (i = 0; i < count && offset + 8 <= APP_HOST_MESSAGE_MAX_LEN; i++) {
        wr32(&s_rsp_buf[offset], s_peer_snapshot[i].short_id);
        s_rsp_buf[offset + 4] = (u8)s_peer_snapshot[i].level;
        s_rsp_buf[offset + 5] = (u8)s_peer_snapshot[i].rssi;
        s_rsp_buf[offset + 6] = (u8)s_peer_snapshot[i].rssi_avg;
        s_rsp_buf[offset + 7] = s_peer_snapshot[i].flags;
        offset = (u8)(offset + 8);
        included++;
    }
    s_rsp_buf[0] = included;
    send_rsp(seq, HOST_CMD_GET_PEER_TABLE, HOST_STATUS_OK, s_rsp_buf, offset);
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
    app_unique_id_t id;
    app_status_t st;
    u8 lock_after_write;

    if (!payload || len < 17) {
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
}

static void handle_lock_identity(u8 seq)
{
    app_status_t st = app_factory_lock_identity();
    if (st == APP_OK) {
        app_adv_scheduler_request_beacon_update();
    }
    send_identity_rsp(seq, HOST_CMD_LOCK_IDENTITY, st);
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
    u32 test_mask = 0xffffffff;
    u32 result_mask = 0;
    app_status_t st;

    if (payload && len >= 4) {
        test_mask = rd32(payload);
    }
    st = app_factory_run_self_test(test_mask, &result_mask);
    s_rsp_buf[0] = 1;
    wr32(&s_rsp_buf[1], test_mask);
    wr32(&s_rsp_buf[5], result_mask);
    send_rsp(seq, HOST_CMD_RUN_FACTORY_TEST, host_status_from_app(st), s_rsp_buf, st == APP_OK ? 9 : 0);
}

static void handle_command(u8 seq, u8 cmd, const u8 *payload, u16 len)
{
    s_cmd_count++;
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
}

void app_host_cmd_poll(void)
{
}

u8 app_host_cmd_next_tx_seq(void)
{
    return s_tx_seq++;
}

void app_host_cmd_on_rx_frame(const u8 *data, u8 len)
{
    app_host_frame_rx_t frame;
    app_status_t st;

    st = app_host_frame_decode(data, len, &frame);
    if (st != APP_OK) {
        if (st == APP_ERR_CRC) {
            s_crc_error_count++;
        }
        return;
    }
    if (frame.type != HOST_FRAME_TYPE_CMD) {
        return;
    }

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
