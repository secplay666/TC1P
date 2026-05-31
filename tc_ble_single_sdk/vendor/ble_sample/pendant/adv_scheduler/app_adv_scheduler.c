#include "app_adv_scheduler.h"
#include "../ble/app_ble.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "../battery/app_battery.h"
#include "../charge/app_charge.h"
#include "../peer_table/app_peer_table.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

static u16 s_frame_seq;
static u8 s_adv_dirty;
static u32 s_last_update_tick;
static app_adv_frame_t s_pending_frame;
static u8 s_has_pending_frame;
static u8 s_adv_buf[APP_ADV_FRAME_MAX_LEN];
static u8 s_payload_buf[32];

#define APP_ADV_DEBUG_NAME "PENDANT"
#define AD_TYPE_FLAGS 0x01
#define AD_TYPE_COMPLETE_LOCAL_NAME 0x09

void app_adv_scheduler_init(void)
{
    s_frame_seq = 0;
    s_adv_dirty = 1;
    s_last_update_tick = 0;
    s_has_pending_frame = 0;
    memset(&s_pending_frame, 0, sizeof(s_pending_frame));
}

app_status_t app_adv_scheduler_request_beacon_update(void)
{
    s_adv_dirty = 1;
    return APP_OK;
}

app_status_t app_adv_scheduler_enqueue_frame(const app_adv_frame_t *frame)
{
    if (!frame) {
        return APP_ERR_PARAM;
    }
    s_pending_frame = *frame;
    s_has_pending_frame = 1;
    s_adv_dirty = 1;
    return APP_OK;
}

static void build_beacon_payload(u8 *payload, u8 *len)
{
    app_battery_state_t bat;
    app_charge_state_t charge;
    u32 uptime = 0;
    app_system_snapshot_t sys;

    app_battery_get_state(&bat);
    charge = app_charge_get_state();
    app_system_get_snapshot(&sys);
    uptime = sys.uptime_s;

    payload[0] = 0x01;
    payload[1] = 0x02;
    payload[2] = 0;
    payload[3] = 1;
    payload[4] = 0;
    payload[5] = bat.percent;
    payload[6] = (u8)bat.voltage_mv;
    payload[7] = (u8)(bat.voltage_mv >> 8);
    payload[8] = (u8)charge;
    payload[9] = 0;
    payload[10] = app_peer_table_count();
    payload[11] = 0x71;
    payload[12] = 0x00;
    payload[13] = (u8)sys.error_code;
    payload[14] = (u8)(sys.error_code >> 8);
    payload[15] = 0;
    payload[16] = 0;
    payload[17] = (u8)uptime;
    payload[18] = (u8)(uptime >> 8);
    payload[19] = (u8)(uptime >> 16);
    payload[20] = (u8)(uptime >> 24);
    *len = 21;
}

app_status_t app_adv_scheduler_build_next_adv_data(u8 *buf, u8 max_len, u8 *out_len)
{
    app_adv_frame_t frame;
    app_eid_t zero_eid;
    u8 payload_len;
    u8 prefix_len;
    u8 frame_len;
    const u8 name_len = sizeof(APP_ADV_DEBUG_NAME) - 1;

    if (!buf || !out_len) {
        return APP_ERR_PARAM;
    }
    if (max_len < (u8)(3 + 2 + name_len)) {
        return APP_ERR_NO_MEM;
    }

    buf[0] = 2;
    buf[1] = AD_TYPE_FLAGS;
    buf[2] = 0x06;
    buf[3] = (u8)(name_len + 1);
    buf[4] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&buf[5], APP_ADV_DEBUG_NAME, name_len);
    prefix_len = (u8)(5 + name_len);

    if (s_has_pending_frame) {
        frame = s_pending_frame;
        s_has_pending_frame = 0;
    } else {
        memset(&zero_eid, 0, sizeof(zero_eid));
        build_beacon_payload(s_payload_buf, &payload_len);
        memset(&frame, 0, sizeof(frame));
        frame.type = ADV_FRAME_BEACON;
        frame.flags = 0;
        frame.key_id = app_identity_get_key_id();
        frame.device_state = (u8)app_system_get_state();
        frame.frame_seq = s_frame_seq++;
        frame.src_eid = *app_identity_get_eid();
        frame.dst_eid = zero_eid;
        frame.message_id = 0;
        frame.fragment_index = 0;
        frame.fragment_count = 1;
        frame.payload = s_payload_buf;
        frame.payload_len = payload_len;
    }

    if (app_adv_proto_encode(&frame, &buf[prefix_len], (u8)(max_len - prefix_len), &frame_len) != APP_OK) {
        return APP_ERR_NO_MEM;
    }
    *out_len = (u8)(prefix_len + frame_len);
    return APP_OK;
}

void app_adv_scheduler_poll(void)
{
    u8 len;
    if (!s_last_update_tick || clock_time_exceed(s_last_update_tick, 200000) || s_adv_dirty) {
        if (app_adv_scheduler_build_next_adv_data(s_adv_buf, sizeof(s_adv_buf), &len) == APP_OK) {
            app_ble_update_ext_adv_data(s_adv_buf, len);
        }
        s_last_update_tick = clock_time();
        s_adv_dirty = 0;
    }
}
