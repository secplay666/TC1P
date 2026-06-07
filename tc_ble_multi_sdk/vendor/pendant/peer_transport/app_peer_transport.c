#include "app_peer_transport.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "../common/app_debug_print.h"
#include "common/string.h"
#include "timer.h"

#define APP_PEER_TRANSPORT_LOG_MAX              20
#define APP_PEER_TRANSPORT_DUP_TIMEOUT_US       1500000
#define APP_PEER_TRANSPORT_RX_LOG_ENABLE        0

static u16 s_seq;
static u32 s_message_id;
static u8 s_tx_payload[APP_ADV_PAYLOAD_MAX_LEN];
static app_peer_transport_debug_t s_debug;
static u8 s_log_count;
static u8 s_last_rx_valid;
static app_eid_t s_last_rx_src;
static u16 s_last_rx_seq;
static u8 s_last_rx_type;
static u8 s_last_rx_len;
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

static u8 payload_looks_valid(const u8 *payload, u8 len)
{
    if (!payload || len < APP_PEER_TRANSPORT_HEADER_LEN) {
        return 0;
    }
    return payload[0] == APP_PEER_TRANSPORT_MAGIC_LO &&
           payload[1] == APP_PEER_TRANSPORT_MAGIC_HI &&
           payload[2] == APP_PEER_TRANSPORT_VERSION;
}

#if APP_PEER_TRANSPORT_RX_LOG_ENABLE
static void p2p_debug_u8(const char *label, u8 value)
{
    u_printf(label);
    u_printf("%x\r\n", value);
}

static void p2p_debug_s8(const char *label, s8 value)
{
    u_printf(label);
    u_printf("%d\r\n", value);
}
#endif

void app_peer_transport_init(void)
{
    s_seq = 1;
    s_message_id = 1;
    app_peer_transport_debug_reset();
}

void app_peer_transport_poll(void)
{
}

void app_peer_transport_debug_reset(void)
{
    memset(&s_debug, 0, sizeof(s_debug));
    s_debug.max_payload_len = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    s_log_count = 0;
    s_last_rx_valid = 0;
}

void app_peer_transport_get_debug(app_peer_transport_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
        debug->max_payload_len = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    }
}

app_status_t app_peer_transport_send(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                     const u8 *payload, u8 len)
{
    app_adv_frame_t frame;
    app_eid_t zero_eid;
    u16 seq;
    app_status_t st;

    if (len > APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN || (len && !payload)) {
        s_debug.tx_fail++;
        s_debug.last_status = APP_ERR_PARAM;
        return APP_ERR_PARAM;
    }

    memset(&zero_eid, 0, sizeof(zero_eid));
    seq = s_seq++;
    if (!s_seq) {
        s_seq = 1;
    }

    s_tx_payload[0] = APP_PEER_TRANSPORT_MAGIC_LO;
    s_tx_payload[1] = APP_PEER_TRANSPORT_MAGIC_HI;
    s_tx_payload[2] = APP_PEER_TRANSPORT_VERSION;
    s_tx_payload[3] = (u8)type;
    wr16(&s_tx_payload[4], seq);
    s_tx_payload[6] = len;
    s_tx_payload[7] = 0;
    if (len) {
        memcpy(&s_tx_payload[APP_PEER_TRANSPORT_HEADER_LEN], payload, len);
    }

    memset(&frame, 0, sizeof(frame));
    frame.type = ADV_FRAME_DATA;
    frame.flags = 0;
    frame.key_id = app_identity_get_key_id();
    frame.device_state = (u8)app_system_get_state();
    frame.frame_seq = seq;
    frame.src_eid = *app_identity_get_eid();
    frame.dst_eid = dst_eid ? *dst_eid : zero_eid;
    frame.message_id = s_message_id++;
    frame.fragment_index = 0;
    frame.fragment_count = 1;
    frame.payload = s_tx_payload;
    frame.payload_len = (u8)(APP_PEER_TRANSPORT_HEADER_LEN + len);

    st = app_adv_scheduler_enqueue_frame(&frame);
    s_debug.last_status = (u8)st;
    s_debug.last_tx_seq = seq;
    s_debug.last_tx_type = (u8)type;
    s_debug.last_tx_len = len;
    if (st == APP_OK) {
        s_debug.tx_ok++;
    } else {
        s_debug.tx_fail++;
    }
    return st;
}

u8 app_peer_transport_on_adv_frame(const app_adv_frame_t *frame, s8 rssi)
{
    const u8 *payload;
    const app_eid_t *own_eid;
    u8 user_len;
    u8 is_broadcast;

    if (!frame || frame->type != ADV_FRAME_DATA || !payload_looks_valid(frame->payload, frame->payload_len)) {
        return 0;
    }

    s_debug.rx_total++;
    payload = frame->payload;
    user_len = payload[6];
    if ((u8)(APP_PEER_TRANSPORT_HEADER_LEN + user_len) != frame->payload_len) {
        s_debug.rx_drop++;
        return 1;
    }

    own_eid = app_identity_get_eid();
    is_broadcast = app_eid_is_zero(&frame->dst_eid);
    if (!is_broadcast && !app_eid_equal(&frame->dst_eid, own_eid)) {
        s_debug.rx_drop++;
        return 1;
    }

    s_debug.last_rx_seq = rd16(&payload[4]);
    s_debug.last_rx_type = payload[3];
    s_debug.last_rx_len = user_len;
    s_debug.last_rx_src0 = frame->src_eid.bytes[0];
    s_debug.last_rx_src1 = frame->src_eid.bytes[1];
    s_debug.last_rssi = rssi;

    if (s_last_rx_valid &&
        s_last_rx_seq == s_debug.last_rx_seq &&
        s_last_rx_type == s_debug.last_rx_type &&
        s_last_rx_len == s_debug.last_rx_len &&
        app_eid_equal(&s_last_rx_src, &frame->src_eid) &&
        !clock_time_exceed(s_last_rx_tick, APP_PEER_TRANSPORT_DUP_TIMEOUT_US)) {
        s_debug.rx_dup++;
        return 1;
    }

    s_last_rx_valid = 1;
    s_last_rx_src = frame->src_eid;
    s_last_rx_seq = s_debug.last_rx_seq;
    s_last_rx_type = s_debug.last_rx_type;
    s_last_rx_len = s_debug.last_rx_len;
    s_last_rx_tick = clock_time();

    s_debug.rx_accept++;
    if (is_broadcast) {
        s_debug.rx_broadcast++;
    } else {
        s_debug.rx_direct++;
    }

#if APP_PEER_TRANSPORT_RX_LOG_ENABLE
    if (s_log_count < APP_PEER_TRANSPORT_LOG_MAX) {
        u_printf("[P2P] rx\r\n");
        p2p_debug_u8(" type=", s_debug.last_rx_type);
        p2p_debug_u8(" len=", s_debug.last_rx_len);
        p2p_debug_u8(" seq=", (u8)s_debug.last_rx_seq);
        p2p_debug_s8(" rssi=", rssi);
        s_log_count++;
    }
#else
    (void)s_log_count;
#endif
    return 1;
}
