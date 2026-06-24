#include "app_adv_scheduler.h"
#include "../ble/app_ble.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "../battery/app_battery.h"
#include "../charge/app_charge.h"
#include "../peer_table/app_peer_table.h"
#include "../common/app_debug_print.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

static u16 s_frame_seq;
static u8 s_adv_dirty;
static u32 s_last_update_tick;
static u8 s_data_hold_active;
static u8 s_adv_buf[APP_ADV_FRAME_MAX_LEN];
static u8 s_payload_buf[32];
static app_adv_scheduler_debug_t s_debug;

#define APP_ADV_SCHED_QUEUE_SIZE 1
#define APP_ADV_DATA_HOLD_US     3000000
#define APP_ADV_DATA_UPDATE_US   50000

typedef struct {
    app_adv_frame_t frame;
    u8 payload[APP_ADV_PAYLOAD_MAX_LEN];
} app_adv_sched_item_t;

static app_adv_sched_item_t s_frame_queue[APP_ADV_SCHED_QUEUE_SIZE];
static u8 s_q_head;
static u8 s_q_tail;
static u8 s_q_count;

void app_adv_scheduler_init(void)
{
    s_frame_seq = 0;
    s_adv_dirty = 1;
    s_last_update_tick = 0;
    s_data_hold_active = 0;
    s_q_head = 0;
    s_q_tail = 0;
    s_q_count = 0;
    memset(s_frame_queue, 0, sizeof(s_frame_queue));
    memset(&s_debug, 0, sizeof(s_debug));
}

app_status_t app_adv_scheduler_request_beacon_update(void)
{
    s_adv_dirty = 1;
    return APP_OK;
}

void app_adv_scheduler_debug_reset(void)
{
    memset(&s_debug, 0, sizeof(s_debug));
    s_debug.queue_count = s_q_count;
    s_adv_dirty = 1;
    s_data_hold_active = 0;
}

void app_adv_scheduler_get_debug(app_adv_scheduler_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
        debug->queue_count = s_q_count;
    }
}

app_status_t app_adv_scheduler_enqueue_frame(const app_adv_frame_t *frame)
{
    app_adv_sched_item_t *item;

    if (!frame) {
        return APP_ERR_PARAM;
    }
    if (frame->payload_len > APP_ADV_PAYLOAD_MAX_LEN || (frame->payload_len && !frame->payload)) {
        return APP_ERR_PARAM;
    }
    if (s_q_count >= APP_ADV_SCHED_QUEUE_SIZE) {
        s_debug.enqueue_full++;
        return APP_ERR_NO_MEM;
    }

    item = &s_frame_queue[s_q_tail];
    memset(item, 0, sizeof(*item));
    item->frame = *frame;
    if (frame->payload_len) {
        memcpy(item->payload, frame->payload, frame->payload_len);
        item->frame.payload = item->payload;
    }
    s_q_tail = (u8)((s_q_tail + 1) % APP_ADV_SCHED_QUEUE_SIZE);
    s_q_count++;
    s_debug.enqueue_ok++;
    s_debug.queue_count = s_q_count;
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
    u8 frame_len;

    if (!buf || !out_len) {
        return APP_ERR_PARAM;
    }
    if (max_len < (APP_ADV_AD_OVERHEAD_LEN + APP_ADV_HEADER_LEN + APP_ADV_FRAME_CRC_LEN)) {
        return APP_ERR_NO_MEM;
    }

    if (s_q_count) {
        app_adv_sched_item_t item = s_frame_queue[s_q_head];
        memset(&s_frame_queue[s_q_head], 0, sizeof(s_frame_queue[s_q_head]));
        s_q_head = (u8)((s_q_head + 1) % APP_ADV_SCHED_QUEUE_SIZE);
        s_q_count--;
        frame = item.frame;
        if (frame.payload_len) {
            frame.payload = item.payload;
        }
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

    s_debug.last_type = (u8)frame.type;
    s_debug.last_payload_len = frame.payload_len;
    if (app_adv_proto_encode(&frame, buf, max_len, &frame_len) != APP_OK) {
        s_debug.queue_count = s_q_count;
        return APP_ERR_NO_MEM;
    }
    s_debug.last_adv_len = frame_len;
    if (frame_len > s_debug.max_adv_len) {
        s_debug.max_adv_len = frame_len;
    }
    if (frame.type == ADV_FRAME_DATA) {
        s_debug.last_data_adv_len = frame_len;
        s_debug.last_data_payload_len = frame.payload_len;
    }
    s_debug.queue_count = s_q_count;
    *out_len = frame_len;
    return APP_OK;
}

void app_adv_scheduler_poll(void)
{
    u8 len;
    app_status_t st;
    u8 need_update = 0;

    if (s_data_hold_active && !s_q_count && !s_adv_dirty &&
        !clock_time_exceed(s_last_update_tick, APP_ADV_DATA_HOLD_US)) {
        return;
    }

    if (s_q_count) {
        need_update = !s_last_update_tick || clock_time_exceed(s_last_update_tick, APP_ADV_DATA_UPDATE_US) || s_adv_dirty;
    } else if (s_adv_dirty || !s_last_update_tick || s_data_hold_active) {
        need_update = 1;
    }

    if (need_update) {
        st = app_adv_scheduler_build_next_adv_data(s_adv_buf, sizeof(s_adv_buf), &len);
        s_debug.last_status = (u8)st;
        if (st == APP_OK) {
            s_debug.build_ok++;
            if (s_debug.last_type == ADV_FRAME_BEACON) {
                s_debug.beacon_build_ok++;
                s_data_hold_active = 0;
            } else {
                if (s_debug.last_type == ADV_FRAME_DATA) {
                    s_debug.data_build_ok++;
                }
                s_data_hold_active = 1;
            }
            app_ble_update_ext_adv_data(s_adv_buf, len);
        } else {
            s_debug.build_fail++;
            s_data_hold_active = 0;
        }
        s_last_update_tick = clock_time();
        s_adv_dirty = 0;
    }
}
