#include "app_host_adv.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../crypto/app_crypto.h"
#include "../host_cmd/app_host_cmd.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define APP_HOST_ADV_RX_QUEUE_SIZE       4
#define APP_HOST_ADV_TX_REPEAT           3
#define APP_HOST_ADV_ACTIVE_TIMEOUT_US   30000000
#define APP_HOST_ADV_DUP_TIMEOUT_US      5000000

typedef struct {
    u8 in_use;
    app_eid_t src_eid;
    s8 rssi;
    u8 payload_len;
    u8 payload[APP_ADV_PAYLOAD_MAX_LEN];
} app_host_adv_rx_item_t;

typedef struct {
    u8 active;
    app_host_frame_type_t type;
    u8 seq;
    u8 cmd;
    u8 status;
    u8 frag_count;
    u8 received_mask;
    u16 total_len;
    u16 message_crc;
    u8 data[APP_HOST_MESSAGE_MAX_LEN];
} app_host_adv_rx_assembly_t;

static app_host_adv_rx_item_t s_rx_queue[APP_HOST_ADV_RX_QUEUE_SIZE];
static app_host_adv_rx_assembly_t s_rx;
static u8 s_rx_head;
static u8 s_rx_tail;
static u8 s_rx_count;
static app_eid_t s_host_eid;
static u8 s_host_ready;
static u32 s_host_last_seen_tick;
static u16 s_frame_seq;
static u32 s_message_id;
static u8 s_tx_seq;
static u8 s_last_rx_valid;
static app_host_frame_type_t s_last_rx_type;
static u8 s_last_rx_seq;
static u8 s_last_rx_cmd;
static u16 s_last_rx_len;
static u16 s_last_rx_crc;
static u32 s_last_rx_tick;

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u8 next_tx_seq(void)
{
    u8 seq = s_tx_seq;
    s_tx_seq = (u8)(s_tx_seq >= 255 ? 1 : s_tx_seq + 1);
    return seq;
}

static u8 host_adv_payload_looks_valid(const u8 *payload, u8 len)
{
    if (!payload || len < APP_HOST_ADV_HEADER_LEN) {
        return 0;
    }
    return payload[0] == APP_HOST_ADV_MAGIC_LO &&
           payload[1] == APP_HOST_ADV_MAGIC_HI &&
           payload[2] == APP_HOST_ADV_VERSION;
}

void app_host_adv_init(void)
{
    memset(s_rx_queue, 0, sizeof(s_rx_queue));
    memset(&s_rx, 0, sizeof(s_rx));
    memset(&s_host_eid, 0, sizeof(s_host_eid));
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_count = 0;
    s_host_ready = 0;
    s_host_last_seen_tick = 0;
    s_frame_seq = 0;
    s_message_id = 1;
    s_tx_seq = 1;
    s_last_rx_valid = 0;
}

u8 app_host_adv_is_ready(void)
{
    if (!s_host_ready) {
        return 0;
    }
    if (clock_time_exceed(s_host_last_seen_tick, APP_HOST_ADV_ACTIVE_TIMEOUT_US)) {
        return 0;
    }
    return 1;
}

void app_host_adv_on_adv_frame(const app_adv_frame_t *frame, s8 rssi)
{
    app_host_adv_rx_item_t *item;
    const app_eid_t *own_eid;

    if (!frame || frame->type != ADV_FRAME_DATA || !frame->payload || !frame->payload_len) {
        return;
    }
    if (!host_adv_payload_looks_valid(frame->payload, frame->payload_len)) {
        return;
    }

    own_eid = app_identity_get_eid();
    if (!app_eid_is_zero(&frame->dst_eid) && !app_eid_equal(&frame->dst_eid, own_eid)) {
        return;
    }
    if (s_rx_count >= APP_HOST_ADV_RX_QUEUE_SIZE) {
        return;
    }

    item = &s_rx_queue[s_rx_tail];
    item->in_use = 1;
    item->src_eid = frame->src_eid;
    item->rssi = rssi;
    item->payload_len = frame->payload_len;
    memcpy(item->payload, frame->payload, frame->payload_len);
    s_rx_tail = (u8)((s_rx_tail + 1) % APP_HOST_ADV_RX_QUEUE_SIZE);
    s_rx_count++;
}

static void reset_rx_assembly(void)
{
    memset(&s_rx, 0, sizeof(s_rx));
}

static void process_rx_item(const app_host_adv_rx_item_t *item)
{
    const u8 *p;
    app_host_frame_type_t type;
    u8 seq;
    u8 cmd;
    u8 status;
    u8 frag_index;
    u8 frag_count;
    u16 total_len;
    u16 message_crc;
    u8 chunk_len;
    u16 offset;
    u8 complete_mask;

    if (!item || !item->in_use || !host_adv_payload_looks_valid(item->payload, item->payload_len)) {
        return;
    }

    p = item->payload;
    type = (app_host_frame_type_t)p[3];
    seq = p[4];
    cmd = p[5];
    status = p[6];
    frag_index = p[7];
    frag_count = p[8];
    total_len = rd16(&p[9]);
    message_crc = rd16(&p[11]);
    chunk_len = p[13];

    if (!frag_count || frag_count > 8 || frag_index >= frag_count) {
        return;
    }
    if (item->payload_len != (u8)(APP_HOST_ADV_HEADER_LEN + chunk_len)) {
        return;
    }
    if (total_len > APP_HOST_MESSAGE_MAX_LEN) {
        return;
    }

    offset = (u16)frag_index * APP_HOST_ADV_CHUNK_MAX_LEN;
    if ((u32)offset + chunk_len > total_len) {
        return;
    }

    if (!s_rx.active || s_rx.type != type || s_rx.seq != seq || s_rx.cmd != cmd ||
        s_rx.frag_count != frag_count || s_rx.total_len != total_len ||
        s_rx.message_crc != message_crc) {
        reset_rx_assembly();
        s_rx.active = 1;
        s_rx.type = type;
        s_rx.seq = seq;
        s_rx.cmd = cmd;
        s_rx.status = status;
        s_rx.frag_count = frag_count;
        s_rx.total_len = total_len;
        s_rx.message_crc = message_crc;
    }

    if (chunk_len) {
        memcpy(&s_rx.data[offset], &p[APP_HOST_ADV_HEADER_LEN], chunk_len);
    }
    s_rx.received_mask |= (u8)(1 << frag_index);

    complete_mask = (u8)((1 << frag_count) - 1);
    if ((s_rx.received_mask & complete_mask) != complete_mask) {
        return;
    }

    if (app_crc16(s_rx.data, s_rx.total_len) != s_rx.message_crc) {
        reset_rx_assembly();
        return;
    }

    s_host_eid = item->src_eid;
    s_host_ready = 1;
    s_host_last_seen_tick = clock_time();

    if (s_last_rx_valid &&
        s_last_rx_type == s_rx.type &&
        s_last_rx_seq == s_rx.seq &&
        s_last_rx_cmd == s_rx.cmd &&
        s_last_rx_len == s_rx.total_len &&
        s_last_rx_crc == s_rx.message_crc &&
        !clock_time_exceed(s_last_rx_tick, APP_HOST_ADV_DUP_TIMEOUT_US)) {
        reset_rx_assembly();
        return;
    }

    s_last_rx_valid = 1;
    s_last_rx_type = s_rx.type;
    s_last_rx_seq = s_rx.seq;
    s_last_rx_cmd = s_rx.cmd;
    s_last_rx_len = s_rx.total_len;
    s_last_rx_crc = s_rx.message_crc;
    s_last_rx_tick = clock_time();

    app_host_cmd_on_rx_message(s_rx.type, s_rx.seq, s_rx.cmd, s_rx.status, s_rx.data, s_rx.total_len);
    reset_rx_assembly();
}

void app_host_adv_poll(void)
{
    app_host_adv_rx_item_t item;

    if (s_host_ready && clock_time_exceed(s_host_last_seen_tick, APP_HOST_ADV_ACTIVE_TIMEOUT_US)) {
        s_host_ready = 0;
    }

    while (s_rx_count) {
        item = s_rx_queue[s_rx_head];
        memset(&s_rx_queue[s_rx_head], 0, sizeof(s_rx_queue[s_rx_head]));
        s_rx_head = (u8)((s_rx_head + 1) % APP_HOST_ADV_RX_QUEUE_SIZE);
        s_rx_count--;
        process_rx_item(&item);
    }
}

app_status_t app_host_adv_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    u8 frag_count;
    u8 frag_index;
    u8 repeat;
    u16 offset = 0;
    u16 message_crc;
    app_status_t st;

    if (len && !payload) {
        return APP_ERR_PARAM;
    }
    if (len > APP_HOST_MESSAGE_MAX_LEN) {
        return APP_ERR_NO_MEM;
    }
    if (!app_host_adv_is_ready()) {
        return APP_ERR_STATE;
    }

    message_crc = app_crc16(payload, len);
    frag_count = (u8)((len + APP_HOST_ADV_CHUNK_MAX_LEN - 1) / APP_HOST_ADV_CHUNK_MAX_LEN);
    if (!frag_count) {
        frag_count = 1;
    }

    for (repeat = 0; repeat < APP_HOST_ADV_TX_REPEAT; repeat++) {
        offset = 0;
        for (frag_index = 0; frag_index < frag_count; frag_index++) {
            u8 adv_payload[APP_ADV_PAYLOAD_MAX_LEN];
            u8 chunk_len = (u8)((len - offset) > APP_HOST_ADV_CHUNK_MAX_LEN ? APP_HOST_ADV_CHUNK_MAX_LEN : (len - offset));
            app_adv_frame_t frame;

            adv_payload[0] = APP_HOST_ADV_MAGIC_LO;
            adv_payload[1] = APP_HOST_ADV_MAGIC_HI;
            adv_payload[2] = APP_HOST_ADV_VERSION;
            adv_payload[3] = (u8)type;
            adv_payload[4] = seq;
            adv_payload[5] = cmd;
            adv_payload[6] = status;
            adv_payload[7] = frag_index;
            adv_payload[8] = frag_count;
            wr16(&adv_payload[9], len);
            wr16(&adv_payload[11], message_crc);
            adv_payload[13] = chunk_len;
            if (chunk_len) {
                memcpy(&adv_payload[APP_HOST_ADV_HEADER_LEN], &payload[offset], chunk_len);
            }

            memset(&frame, 0, sizeof(frame));
            frame.type = ADV_FRAME_DATA;
            frame.flags = 0;
            frame.key_id = app_identity_get_key_id();
            frame.device_state = (u8)app_system_get_state();
            frame.frame_seq = s_frame_seq++;
            frame.src_eid = *app_identity_get_eid();
            frame.dst_eid = s_host_eid;
            frame.message_id = s_message_id++;
            frame.fragment_index = frag_index;
            frame.fragment_count = frag_count;
            frame.payload = adv_payload;
            frame.payload_len = (u8)(APP_HOST_ADV_HEADER_LEN + chunk_len);

            st = app_adv_scheduler_enqueue_frame(&frame);
            if (st != APP_OK) {
                return st;
            }
            offset = (u16)(offset + chunk_len);
        }
    }

    return APP_OK;
}

app_status_t app_host_adv_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    return app_host_adv_send_message_with_seq(type, next_tx_seq(), cmd, status, payload, len);
}
