#include "app_peer_transport.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "../identity/app_identity.h"
#include "../system/app_system.h"
#include "common/string.h"
#include "timer.h"

#define APP_PEER_RX_CONTEXT_COUNT               1
#define APP_PEER_COMPLETED_CACHE_COUNT          2
#define APP_PEER_TX_ACK_WAIT_US                 800000
#define APP_PEER_TX_TOTAL_TIMEOUT_US            10000000
#define APP_PEER_TX_MAX_RETRY_ROUNDS            4
#define APP_PEER_TX_FRAGMENT_GAP_US             600000
#define APP_PEER_RX_TOTAL_TIMEOUT_US            8000000
#define APP_PEER_RX_IDLE_TIMEOUT_US             3000000
#define APP_PEER_ACK_INTERVAL_US                200000
#define APP_PEER_COMPLETE_CACHE_TTL_US          30000000
#define APP_PEER_ACK_REPEAT_PARTIAL             1
#define APP_PEER_ACK_REPEAT_COMPLETE            3
#define APP_PEER_ACK_REPEAT_ERROR               2
#define APP_PEER_ACK_FIRST_MISSING_NONE         0xff
#define APP_PEER_INVALID_FRAGMENT_INDEX         0xff
#define APP_PEER_INVALID_FRAME_SEQ              0xffff

typedef struct {
    u8 active;
    u8 reliable;
    app_eid_t dst_eid;
    app_peer_msg_type_t type;
    u8 flags;
    u32 message_id;
    u16 len;
    u8 fragment_count;
    u32 all_bitmap;
    u32 ack_bitmap;
    u32 pending_bitmap;
    u8 retry_round;
    u32 first_tick;
    u32 last_tx_tick;
    u32 last_ack_tick;
    u8 payload[APP_PEER_TRANSPORT_MESSAGE_MAX_LEN];
} app_peer_tx_ctx_t;

typedef struct {
    u8 active;
    u8 reliable;
    app_eid_t src_eid;
    app_eid_t dst_eid;
    app_peer_msg_type_t type;
    u32 message_id;
    u8 fragment_count;
    u32 received_bitmap;
    u8 fragment_len[APP_PEER_TRANSPORT_MAX_FRAGMENTS];
    u8 buffer[APP_PEER_TRANSPORT_MAX_FRAGMENTS * APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN];
    u32 first_tick;
    u32 last_progress_tick;
    s8 last_rssi;
} app_peer_rx_ctx_t;

typedef struct {
    u8 valid;
    app_eid_t src_eid;
    app_eid_t dst_eid;
    app_peer_msg_type_t type;
    u32 message_id;
    u32 tick;
    u16 last_dup_ack_frame_seq;
} app_peer_completed_t;

typedef struct {
    u8 active;
    app_eid_t dst_eid;
    app_peer_msg_type_t type;
    u32 message_id;
    u8 fragment_count;
    u32 bitmap;
    app_peer_ack_status_t status;
    u8 reason;
    u8 repeat_left;
    u32 last_tx_tick;
} app_peer_ack_pending_t;

typedef struct {
    u8 active;
    app_eid_t src_eid;
    app_eid_t dst_eid;
    app_peer_msg_type_t type;
    u32 message_id;
    u32 mask;
    u32 armed_bitmap;
    u16 frame_seq[APP_PEER_TRANSPORT_MAX_FRAGMENTS];
} app_peer_debug_drop_t;

static u16 s_seq;
static u32 s_message_id;
static app_peer_transport_debug_t s_debug;
static app_peer_tx_ctx_t s_tx;
static app_peer_rx_ctx_t s_rx[APP_PEER_RX_CONTEXT_COUNT];
static app_peer_completed_t s_completed[APP_PEER_COMPLETED_CACHE_COUNT];
static app_peer_ack_pending_t s_ack;
static app_peer_debug_drop_t s_debug_drop;
static app_peer_transport_rx_cb_t s_rx_cb;
static app_peer_transport_tx_cb_t s_tx_cb;

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u16 next_seq(void)
{
    u16 seq = s_seq++;
    if (!s_seq) {
        s_seq = 1;
    }
    return seq;
}

static u32 next_message_id(void)
{
    u32 id = s_message_id++;
    if (!s_message_id) {
        s_message_id = 1;
    }
    if (!id) {
        id = s_message_id++;
    }
    return id;
}

static u32 bitmap_for_count(u8 count)
{
    if (count >= 32) {
        return 0xffffffff;
    }
    return ((u32)1 << count) - 1;
}

static u8 first_set_bit(u32 bitmap)
{
    u8 i;
    for (i = 0; i < APP_PEER_TRANSPORT_MAX_FRAGMENTS; i++) {
        if (bitmap & ((u32)1 << i)) {
            return i;
        }
    }
    return APP_PEER_INVALID_FRAGMENT_INDEX;
}

static u8 first_missing_index(u32 bitmap, u8 fragment_count)
{
    u8 i;
    for (i = 0; i < fragment_count; i++) {
        if (!(bitmap & ((u32)1 << i))) {
            return i;
        }
    }
    return APP_PEER_ACK_FIRST_MISSING_NONE;
}

static u8 fragment_count_for_len(u16 len)
{
    u16 count;

    if (!len) {
        return 1;
    }
    count = (u16)((len + APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN - 1) /
                  APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN);
    if (!count || count > APP_PEER_TRANSPORT_MAX_FRAGMENTS) {
        return 0;
    }
    return (u8)count;
}

static u8 p2p_header_valid(const u8 *payload, u8 len)
{
    if (!payload || len < APP_PEER_TRANSPORT_HEADER_LEN) {
        return 0;
    }
    return payload[0] == APP_PEER_TRANSPORT_MAGIC_LO &&
           payload[1] == APP_PEER_TRANSPORT_MAGIC_HI &&
           (payload[2] == APP_PEER_TRANSPORT_VERSION ||
            payload[2] == APP_PEER_TRANSPORT_VERSION_LEGACY);
}

static u8 p2p_header_is_v2(const u8 *payload, u8 len)
{
    return p2p_header_valid(payload, len) && payload[2] == APP_PEER_TRANSPORT_VERSION;
}

static u8 is_own_or_broadcast_dst(const app_eid_t *dst, u8 *is_broadcast)
{
    if (app_eid_is_zero(dst)) {
        if (is_broadcast) {
            *is_broadcast = 1;
        }
        return 1;
    }
    if (is_broadcast) {
        *is_broadcast = 0;
    }
    return app_eid_equal(dst, app_identity_get_eid());
}

static void fill_p2p_header(u8 *payload, u8 type, u16 seq, u8 body_len, u8 flags)
{
    payload[0] = APP_PEER_TRANSPORT_MAGIC_LO;
    payload[1] = APP_PEER_TRANSPORT_MAGIC_HI;
    payload[2] = APP_PEER_TRANSPORT_VERSION;
    payload[3] = type;
    wr16(&payload[4], seq);
    payload[6] = body_len;
    payload[7] = flags;
}

static app_status_t enqueue_adv_frame(app_adv_frame_type_t frame_type,
                                      const app_eid_t *dst_eid,
                                      u32 message_id,
                                      u8 fragment_index,
                                      u8 fragment_count,
                                      u8 adv_flags,
                                      const u8 *payload,
                                      u8 payload_len)
{
    app_adv_frame_t frame;
    app_eid_t zero_eid;

    memset(&zero_eid, 0, sizeof(zero_eid));
    memset(&frame, 0, sizeof(frame));
    frame.type = frame_type;
    frame.flags = adv_flags;
    frame.key_id = app_identity_get_key_id();
    frame.device_state = (u8)app_system_get_state();
    frame.frame_seq = next_seq();
    frame.src_eid = *app_identity_get_eid();
    frame.dst_eid = dst_eid ? *dst_eid : zero_eid;
    frame.message_id = message_id;
    frame.fragment_index = fragment_index;
    frame.fragment_count = fragment_count;
    frame.payload = payload;
    frame.payload_len = payload_len;
    return app_adv_scheduler_enqueue_frame(&frame);
}

static app_peer_completed_t *completed_cache_find(const app_eid_t *src_eid, const app_eid_t *dst_eid,
                                                  u32 message_id, app_peer_msg_type_t type)
{
    u8 i;

    for (i = 0; i < APP_PEER_COMPLETED_CACHE_COUNT; i++) {
        if (s_completed[i].valid &&
            s_completed[i].message_id == message_id &&
            s_completed[i].type == type &&
            app_eid_equal(&s_completed[i].src_eid, src_eid) &&
            app_eid_equal(&s_completed[i].dst_eid, dst_eid)) {
            return &s_completed[i];
        }
    }
    return 0;
}

static void completed_cache_add(const app_eid_t *src_eid, const app_eid_t *dst_eid,
                                u32 message_id, app_peer_msg_type_t type)
{
    u8 i;
    u8 slot = 0;

    for (i = 0; i < APP_PEER_COMPLETED_CACHE_COUNT; i++) {
        if (!s_completed[i].valid) {
            slot = i;
            break;
        }
        if (clock_time_exceed(s_completed[i].tick, APP_PEER_COMPLETE_CACHE_TTL_US)) {
            slot = i;
            break;
        }
    }

    s_completed[slot].valid = 1;
    s_completed[slot].src_eid = *src_eid;
    s_completed[slot].dst_eid = *dst_eid;
    s_completed[slot].message_id = message_id;
    s_completed[slot].type = type;
    s_completed[slot].tick = clock_time();
    s_completed[slot].last_dup_ack_frame_seq = APP_PEER_INVALID_FRAME_SEQ;
}

static app_peer_rx_ctx_t *find_rx_ctx(const app_eid_t *src_eid, const app_eid_t *dst_eid,
                                      u32 message_id, app_peer_msg_type_t type)
{
    u8 i;

    for (i = 0; i < APP_PEER_RX_CONTEXT_COUNT; i++) {
        if (s_rx[i].active &&
            s_rx[i].message_id == message_id &&
            s_rx[i].type == type &&
            app_eid_equal(&s_rx[i].src_eid, src_eid) &&
            app_eid_equal(&s_rx[i].dst_eid, dst_eid)) {
            return &s_rx[i];
        }
    }
    return 0;
}

static app_peer_rx_ctx_t *alloc_rx_ctx(void)
{
    u8 i;

    for (i = 0; i < APP_PEER_RX_CONTEXT_COUNT; i++) {
        if (!s_rx[i].active) {
            memset(&s_rx[i], 0, sizeof(s_rx[i]));
            s_rx[i].active = 1;
            return &s_rx[i];
        }
    }
    return 0;
}

static void schedule_ack(const app_eid_t *dst_eid, u32 message_id, app_peer_msg_type_t type,
                         u8 fragment_count, u32 bitmap, app_peer_ack_status_t status,
                         u8 reason, u8 repeat)
{
    if (!dst_eid || app_eid_is_zero(dst_eid)) {
        return;
    }
    if (s_ack.active &&
        s_ack.message_id == message_id &&
        s_ack.type == type &&
        s_ack.fragment_count == fragment_count &&
        s_ack.bitmap == bitmap &&
        s_ack.status == status &&
        s_ack.reason == reason &&
        app_eid_equal(&s_ack.dst_eid, dst_eid)) {
        return;
    }

    s_ack.active = 1;
    s_ack.dst_eid = *dst_eid;
    s_ack.type = type;
    s_ack.message_id = message_id;
    s_ack.fragment_count = fragment_count;
    s_ack.bitmap = bitmap;
    s_ack.status = status;
    s_ack.reason = reason;
    s_ack.repeat_left = repeat ? repeat : 1;
    s_ack.last_tx_tick = 0;
}

static u8 debug_drop_should_drop(const app_adv_frame_t *frame, app_peer_msg_type_t type,
                                 u8 reliable, u32 bit)
{
    u8 index;

    if (!reliable || !s_debug_drop.mask || !frame ||
        frame->fragment_index >= APP_PEER_TRANSPORT_MAX_FRAGMENTS) {
        return 0;
    }

    if (!s_debug_drop.active) {
        memset(&s_debug_drop, 0, sizeof(s_debug_drop));
        s_debug_drop.active = 1;
        s_debug_drop.src_eid = frame->src_eid;
        s_debug_drop.dst_eid = frame->dst_eid;
        s_debug_drop.type = type;
        s_debug_drop.message_id = frame->message_id;
        s_debug_drop.mask = s_debug.rx_drop_mask;
    }

    if (s_debug_drop.message_id != frame->message_id ||
        s_debug_drop.type != type ||
        !app_eid_equal(&s_debug_drop.src_eid, &frame->src_eid) ||
        !app_eid_equal(&s_debug_drop.dst_eid, &frame->dst_eid)) {
        return 0;
    }

    if (!(s_debug_drop.mask & bit)) {
        return 0;
    }

    index = frame->fragment_index;
    if (!(s_debug_drop.armed_bitmap & bit)) {
        s_debug_drop.armed_bitmap |= bit;
        s_debug_drop.frame_seq[index] = frame->frame_seq;
    }

    if (s_debug_drop.frame_seq[index] == frame->frame_seq) {
        s_debug.rx_debug_drop++;
        s_debug.rx_drop++;
        return 1;
    }

    s_debug_drop.mask &= ~bit;
    s_debug.rx_drop_mask = (u8)(s_debug_drop.mask & 0xff);
    if (!s_debug_drop.mask) {
        memset(&s_debug_drop, 0, sizeof(s_debug_drop));
    }
    return 0;
}

static app_status_t send_ack_once(void)
{
    u8 ack_payload[APP_PEER_TRANSPORT_HEADER_LEN + APP_PEER_TRANSPORT_ACK_BODY_LEN];
    u16 seq;
    u8 first_missing;
    app_status_t st;

    if (!s_ack.active || !s_ack.repeat_left) {
        s_ack.active = 0;
        return APP_ERR_STATE;
    }

    if (s_ack.last_tx_tick &&
        !clock_time_exceed(s_ack.last_tx_tick, APP_PEER_ACK_INTERVAL_US)) {
        return APP_ERR_BUSY;
    }

    seq = next_seq();
    fill_p2p_header(ack_payload, (u8)s_ack.status, seq,
                    APP_PEER_TRANSPORT_ACK_BODY_LEN, 0);
    ack_payload[8] = (u8)s_ack.type;
    ack_payload[9] = s_ack.fragment_count;
    wr32(&ack_payload[10], s_ack.bitmap);
    first_missing = first_missing_index(s_ack.bitmap, s_ack.fragment_count);
    ack_payload[14] = first_missing;
    ack_payload[15] = s_ack.reason;

    st = enqueue_adv_frame(ADV_FRAME_ACK, &s_ack.dst_eid, s_ack.message_id, 0, 1, 0,
                           ack_payload,
                           (u8)(APP_PEER_TRANSPORT_HEADER_LEN + APP_PEER_TRANSPORT_ACK_BODY_LEN));
    if (st == APP_OK) {
        s_ack.repeat_left--;
        s_ack.last_tx_tick = clock_time();
        s_debug.rx_ack_tx++;
        if (!s_ack.repeat_left) {
            s_ack.active = 0;
        }
    }
    return st;
}

static app_status_t send_tx_fragment(u8 index)
{
    u8 frame_payload[APP_PEER_TRANSPORT_HEADER_LEN + APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN];
    u16 offset;
    u16 remain;
    u8 body_len;
    u16 seq;
    app_status_t st;
    u8 adv_flags = 0;
    u8 p2p_flags = s_tx.flags;

    if (!s_tx.active || index >= s_tx.fragment_count) {
        return APP_ERR_PARAM;
    }

    offset = (u16)index * APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN;
    remain = offset < s_tx.len ? (u16)(s_tx.len - offset) : 0;
    body_len = remain > APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN ?
               APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN : (u8)remain;

    if (s_tx.reliable) {
        adv_flags |= APP_PEER_TRANSPORT_ADV_FLAG_ACK_REQ;
        p2p_flags |= APP_PEER_TRANSPORT_FRAME_FLAG_RELIABLE;
    }

    seq = next_seq();
    fill_p2p_header(frame_payload, (u8)s_tx.type, seq, body_len, p2p_flags);
    if (body_len) {
        memcpy(&frame_payload[APP_PEER_TRANSPORT_HEADER_LEN],
               &s_tx.payload[offset], body_len);
    }

    st = enqueue_adv_frame(ADV_FRAME_DATA, &s_tx.dst_eid, s_tx.message_id, index,
                           s_tx.fragment_count, adv_flags, frame_payload,
                           (u8)(APP_PEER_TRANSPORT_HEADER_LEN + body_len));
    if (st == APP_OK) {
        s_debug.tx_frag_sent++;
        s_debug.last_status = (u8)st;
        s_debug.last_tx_seq = seq;
        s_debug.last_tx_type = (u8)s_tx.type;
        s_debug.last_tx_len = body_len;
        s_debug.last_tx_msg_len = s_tx.len;
        s_tx.last_tx_tick = clock_time();
    }
    return st;
}

static void tx_finish_success(void)
{
    s_debug.tx_msg_ok++;
    if ((s_tx.flags & APP_PEER_TRANSPORT_FRAME_FLAG_NOTIFY) && s_tx_cb) {
        s_tx_cb(&s_tx.dst_eid, s_tx.type, s_tx.message_id, s_tx.len, APP_OK, s_tx.flags);
    }
    memset(&s_tx, 0, sizeof(s_tx));
}

static void tx_finish_fail(app_status_t status)
{
    s_debug.tx_msg_fail++;
    s_debug.tx_fail++;
    s_debug.last_status = (u8)status;
    if ((s_tx.flags & APP_PEER_TRANSPORT_FRAME_FLAG_NOTIFY) && s_tx_cb) {
        s_tx_cb(&s_tx.dst_eid, s_tx.type, s_tx.message_id, s_tx.len, status, s_tx.flags);
    }
    memset(&s_tx, 0, sizeof(s_tx));
}

static void tx_poll(void)
{
    u8 index;
    app_status_t st;
    u32 missing;

    if (!s_tx.active) {
        return;
    }

    if (clock_time_exceed(s_tx.first_tick, APP_PEER_TX_TOTAL_TIMEOUT_US)) {
        tx_finish_fail(APP_ERR_TIMEOUT);
        return;
    }

    s_tx.pending_bitmap &= ~s_tx.ack_bitmap;
    if (s_tx.pending_bitmap) {
        if (s_tx.last_tx_tick &&
            !clock_time_exceed(s_tx.last_tx_tick, APP_PEER_TX_FRAGMENT_GAP_US)) {
            return;
        }
        index = first_set_bit(s_tx.pending_bitmap);
        if (index == APP_PEER_INVALID_FRAGMENT_INDEX) {
            return;
        }
        st = send_tx_fragment(index);
        if (st == APP_OK) {
            s_tx.pending_bitmap &= ~((u32)1 << index);
        }
        return;
    }

    if (!s_tx.reliable) {
        tx_finish_success();
        return;
    }

    if ((s_tx.ack_bitmap & s_tx.all_bitmap) == s_tx.all_bitmap) {
        tx_finish_success();
        return;
    }

    if (!s_tx.last_tx_tick ||
        !clock_time_exceed(s_tx.last_tx_tick, APP_PEER_TX_ACK_WAIT_US)) {
        return;
    }

    s_debug.tx_ack_timeout++;
    if (s_tx.retry_round >= APP_PEER_TX_MAX_RETRY_ROUNDS) {
        tx_finish_fail(APP_ERR_TIMEOUT);
        return;
    }

    s_tx.retry_round++;
    missing = s_tx.all_bitmap & ~s_tx.ack_bitmap;
    s_tx.pending_bitmap = missing;
    s_debug.tx_frag_retx++;
}

static u16 rx_compact_message(app_peer_rx_ctx_t *ctx)
{
    u8 i;
    u16 len = 0;

    for (i = 0; i < ctx->fragment_count; i++) {
        u8 frag_len = ctx->fragment_len[i];
        u16 src_off = (u16)i * APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN;
        u16 j;
        if ((u16)len + frag_len > APP_PEER_TRANSPORT_MESSAGE_MAX_LEN) {
            return 0xffff;
        }
        if (frag_len && src_off != len) {
            for (j = 0; j < frag_len; j++) {
                ctx->buffer[len + j] = ctx->buffer[src_off + j];
            }
        }
        len = (u16)(len + frag_len);
    }
    return len;
}

static void rx_complete(app_peer_rx_ctx_t *ctx)
{
    u16 len = rx_compact_message(ctx);
    u8 repeat;

    if (len == 0xffff) {
        s_debug.rx_drop++;
        s_debug.rx_rejected++;
        if (ctx->reliable) {
            schedule_ack(&ctx->src_eid, ctx->message_id, ctx->type, ctx->fragment_count,
                         ctx->received_bitmap, APP_PEER_ACK_REJECTED, APP_ERR_NO_MEM,
                         APP_PEER_ACK_REPEAT_ERROR);
        }
        memset(ctx, 0, sizeof(*ctx));
        return;
    }

    s_debug.rx_accept++;
    s_debug.rx_msg_ok++;
    s_debug.last_rx_msg_len = len;
    completed_cache_add(&ctx->src_eid, &ctx->dst_eid, ctx->message_id, ctx->type);
    if (s_rx_cb) {
        s_rx_cb(&ctx->src_eid, ctx->type, ctx->buffer, len, ctx->last_rssi);
    }

    if (ctx->reliable) {
        repeat = APP_PEER_ACK_REPEAT_COMPLETE;
        schedule_ack(&ctx->src_eid, ctx->message_id, ctx->type, ctx->fragment_count,
                     ctx->received_bitmap, APP_PEER_ACK_COMPLETE, 0, repeat);
    }
    memset(ctx, 0, sizeof(*ctx));
}

static void rx_poll_timeouts(void)
{
    u8 i;

    for (i = 0; i < APP_PEER_RX_CONTEXT_COUNT; i++) {
        app_peer_rx_ctx_t *ctx = &s_rx[i];
        if (!ctx->active) {
            continue;
        }
        if (clock_time_exceed(ctx->first_tick, APP_PEER_RX_TOTAL_TIMEOUT_US) ||
            clock_time_exceed(ctx->last_progress_tick, APP_PEER_RX_IDLE_TIMEOUT_US)) {
            s_debug.rx_msg_timeout++;
            if (ctx->reliable) {
                schedule_ack(&ctx->src_eid, ctx->message_id, ctx->type, ctx->fragment_count,
                             ctx->received_bitmap, APP_PEER_ACK_TIMEOUT, 0,
                             APP_PEER_ACK_REPEAT_ERROR);
            }
            memset(ctx, 0, sizeof(*ctx));
        }
    }
}

static u8 handle_ack_frame(const app_adv_frame_t *frame)
{
    const u8 *payload;
    u8 body_len;
    app_peer_ack_status_t status;
    app_peer_msg_type_t ack_type;
    u8 fragment_count;
    u32 bitmap;
    u8 match_flags = 0;

    if (!frame || frame->type != ADV_FRAME_ACK ||
        !p2p_header_is_v2(frame->payload, frame->payload_len)) {
        return 0;
    }

    payload = frame->payload;
    body_len = payload[6];
    if ((u8)(APP_PEER_TRANSPORT_HEADER_LEN + body_len) != frame->payload_len ||
        body_len != APP_PEER_TRANSPORT_ACK_BODY_LEN) {
        return 1;
    }
    if (!app_eid_equal(&frame->dst_eid, app_identity_get_eid())) {
        return 1;
    }

    status = (app_peer_ack_status_t)payload[3];
    ack_type = (app_peer_msg_type_t)payload[8];
    fragment_count = payload[9];
    bitmap = rd32(&payload[10]);
    s_debug.tx_ack_rx++;
    s_debug.last_ack_status = (u8)status;
    s_debug.last_ack_type = (u8)ack_type;
    s_debug.last_ack_frag_count = fragment_count;
    s_debug.last_ack_bitmap = (u8)(bitmap & 0xff);
    s_debug.last_ack_src0 = frame->src_eid.bytes[0];
    s_debug.last_ack_src1 = frame->src_eid.bytes[1];

    if (s_tx.active) {
        match_flags |= 0x01;
    }
    if (s_tx.message_id == frame->message_id) {
        match_flags |= 0x02;
    }
    if (s_tx.type == ack_type) {
        match_flags |= 0x04;
    }
    if (app_eid_equal(&s_tx.dst_eid, &frame->src_eid)) {
        match_flags |= 0x08;
    }
    if (fragment_count == s_tx.fragment_count) {
        match_flags |= 0x10;
    }
    s_debug.last_ack_match_flags = match_flags;

    if (!s_tx.active ||
        s_tx.message_id != frame->message_id ||
        s_tx.type != ack_type ||
        !app_eid_equal(&s_tx.dst_eid, &frame->src_eid)) {
        return 1;
    }

    if (fragment_count != s_tx.fragment_count) {
        return 1;
    }

    s_debug.tx_ack_match++;
    s_tx.last_ack_tick = clock_time();
    if (status == APP_PEER_ACK_COMPLETE ||
        status == APP_PEER_ACK_DUP_COMPLETE) {
        s_tx.ack_bitmap = s_tx.all_bitmap;
        tx_finish_success();
        return 1;
    }
    if (status == APP_PEER_ACK_PARTIAL) {
        s_tx.ack_bitmap |= (bitmap & s_tx.all_bitmap);
        s_tx.pending_bitmap &= ~s_tx.ack_bitmap;
        return 1;
    }
    if (status == APP_PEER_ACK_BUSY) {
        s_tx.last_tx_tick = clock_time();
        return 1;
    }
    if (status == APP_PEER_ACK_REJECTED ||
        status == APP_PEER_ACK_TIMEOUT ||
        status == APP_PEER_ACK_CANCELED) {
        tx_finish_fail(APP_ERR_STATE);
    }
    return 1;
}

static u8 handle_data_v1(const app_adv_frame_t *frame, s8 rssi)
{
    const u8 *payload;
    u8 user_len;
    u8 is_broadcast;
    static u8 last_valid;
    static app_eid_t last_src;
    static u16 last_seq;
    static u8 last_type;
    static u8 last_len;
    static u32 last_tick;

    if (!frame || frame->type != ADV_FRAME_DATA ||
        !p2p_header_valid(frame->payload, frame->payload_len) ||
        frame->payload[2] != APP_PEER_TRANSPORT_VERSION_LEGACY) {
        return 0;
    }

    payload = frame->payload;
    user_len = payload[6];
    if ((u8)(APP_PEER_TRANSPORT_HEADER_LEN + user_len) != frame->payload_len) {
        s_debug.rx_drop++;
        return 1;
    }

    if (!is_own_or_broadcast_dst(&frame->dst_eid, &is_broadcast)) {
        s_debug.rx_drop++;
        return 1;
    }

    s_debug.rx_total++;
    s_debug.last_rx_seq = rd16(&payload[4]);
    s_debug.last_rx_type = payload[3];
    s_debug.last_rx_len = user_len;
    s_debug.last_rx_msg_len = user_len;
    s_debug.last_rx_src0 = frame->src_eid.bytes[0];
    s_debug.last_rx_src1 = frame->src_eid.bytes[1];
    s_debug.last_rssi = rssi;

    if (last_valid &&
        last_seq == s_debug.last_rx_seq &&
        last_type == s_debug.last_rx_type &&
        last_len == s_debug.last_rx_len &&
        app_eid_equal(&last_src, &frame->src_eid) &&
        !clock_time_exceed(last_tick, 5000000)) {
        s_debug.rx_dup++;
        return 1;
    }

    last_valid = 1;
    last_src = frame->src_eid;
    last_seq = s_debug.last_rx_seq;
    last_type = s_debug.last_rx_type;
    last_len = s_debug.last_rx_len;
    last_tick = clock_time();

    s_debug.rx_accept++;
    s_debug.rx_msg_ok++;
    if (is_broadcast) {
        s_debug.rx_broadcast++;
    } else {
        s_debug.rx_direct++;
    }
    return 1;
}

static u8 handle_data_v2(const app_adv_frame_t *frame, s8 rssi)
{
    const u8 *payload;
    const u8 *body;
    u8 body_len;
    u8 p2p_flags;
    u8 is_broadcast;
    u8 reliable;
    u32 bit;
    app_peer_msg_type_t type;
    app_peer_rx_ctx_t *ctx;
    app_peer_completed_t *completed;
    u32 all_bitmap;
    u16 slot_off;

    if (!frame || frame->type != ADV_FRAME_DATA ||
        !p2p_header_is_v2(frame->payload, frame->payload_len)) {
        return 0;
    }

    payload = frame->payload;
    body_len = payload[6];
    p2p_flags = payload[7];
    type = (app_peer_msg_type_t)payload[3];
    if ((u8)(APP_PEER_TRANSPORT_HEADER_LEN + body_len) != frame->payload_len ||
        body_len > APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN) {
        s_debug.rx_drop++;
        s_debug.rx_rejected++;
        return 1;
    }

    if (!is_own_or_broadcast_dst(&frame->dst_eid, &is_broadcast)) {
        s_debug.rx_drop++;
        return 1;
    }

    reliable = (p2p_flags & APP_PEER_TRANSPORT_FRAME_FLAG_RELIABLE) &&
               (frame->flags & APP_PEER_TRANSPORT_ADV_FLAG_ACK_REQ);
    if (reliable && is_broadcast) {
        s_debug.rx_drop++;
        s_debug.rx_rejected++;
        return 1;
    }

    s_debug.rx_total++;
    s_debug.last_rx_seq = rd16(&payload[4]);
    s_debug.last_rx_type = (u8)type;
    s_debug.last_rx_len = body_len;
    s_debug.last_rx_src0 = frame->src_eid.bytes[0];
    s_debug.last_rx_src1 = frame->src_eid.bytes[1];
    s_debug.last_rssi = rssi;

    if (!frame->fragment_count ||
        frame->fragment_count > APP_PEER_TRANSPORT_MAX_FRAGMENTS ||
        frame->fragment_index >= frame->fragment_count) {
        s_debug.rx_drop++;
        s_debug.rx_rejected++;
        if (reliable) {
            schedule_ack(&frame->src_eid, frame->message_id, type, frame->fragment_count,
                         0, APP_PEER_ACK_REJECTED, APP_ERR_PARAM,
                         APP_PEER_ACK_REPEAT_ERROR);
        }
        return 1;
    }

    all_bitmap = bitmap_for_count(frame->fragment_count);
    completed = completed_cache_find(&frame->src_eid, &frame->dst_eid, frame->message_id, type);
    if (completed) {
        s_debug.rx_dup++;
        if (reliable && completed->last_dup_ack_frame_seq != frame->frame_seq) {
            schedule_ack(&frame->src_eid, frame->message_id, type, frame->fragment_count,
                         all_bitmap, APP_PEER_ACK_DUP_COMPLETE, 0,
                         APP_PEER_ACK_REPEAT_COMPLETE);
            completed->last_dup_ack_frame_seq = frame->frame_seq;
        }
        return 1;
    }

    ctx = find_rx_ctx(&frame->src_eid, &frame->dst_eid, frame->message_id, type);
    if (!ctx) {
        ctx = alloc_rx_ctx();
        if (!ctx) {
            s_debug.rx_drop++;
            s_debug.rx_busy++;
            if (reliable) {
                schedule_ack(&frame->src_eid, frame->message_id, type, frame->fragment_count,
                             0, APP_PEER_ACK_BUSY, 1, APP_PEER_ACK_REPEAT_ERROR);
            }
            return 1;
        }
        ctx->reliable = reliable;
        ctx->src_eid = frame->src_eid;
        ctx->dst_eid = frame->dst_eid;
        ctx->type = type;
        ctx->message_id = frame->message_id;
        ctx->fragment_count = frame->fragment_count;
        ctx->first_tick = clock_time();
        ctx->last_progress_tick = ctx->first_tick;
    } else if (ctx->fragment_count != frame->fragment_count ||
               ctx->reliable != reliable) {
        s_debug.rx_drop++;
        s_debug.rx_rejected++;
        if (reliable) {
            schedule_ack(&frame->src_eid, frame->message_id, type, frame->fragment_count,
                         ctx->received_bitmap, APP_PEER_ACK_REJECTED, APP_ERR_PARAM,
                         APP_PEER_ACK_REPEAT_ERROR);
        }
        return 1;
    }

    bit = ((u32)1 << frame->fragment_index);
    if (debug_drop_should_drop(frame, type, reliable, bit)) {
        return 1;
    }

    if (ctx->received_bitmap & bit) {
        s_debug.rx_dup++;
        if (reliable) {
            schedule_ack(&ctx->src_eid, ctx->message_id, ctx->type, ctx->fragment_count,
                         ctx->received_bitmap,
                         (ctx->received_bitmap & all_bitmap) == all_bitmap ?
                         APP_PEER_ACK_COMPLETE : APP_PEER_ACK_PARTIAL,
                         0, APP_PEER_ACK_REPEAT_PARTIAL);
        }
        return 1;
    }

    body = &payload[APP_PEER_TRANSPORT_HEADER_LEN];
    slot_off = (u16)frame->fragment_index * APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN;
    if (body_len) {
        memcpy(&ctx->buffer[slot_off], body, body_len);
    }
    ctx->fragment_len[frame->fragment_index] = body_len;
    ctx->received_bitmap |= bit;
    ctx->last_progress_tick = clock_time();
    ctx->last_rssi = rssi;
    s_debug.rx_frag_new++;

    if (is_broadcast) {
        s_debug.rx_broadcast++;
    } else {
        s_debug.rx_direct++;
    }

    if ((ctx->received_bitmap & all_bitmap) == all_bitmap) {
        rx_complete(ctx);
    } else if (reliable) {
        schedule_ack(&ctx->src_eid, ctx->message_id, ctx->type, ctx->fragment_count,
                     ctx->received_bitmap, APP_PEER_ACK_PARTIAL, 0,
                     APP_PEER_ACK_REPEAT_PARTIAL);
    }
    return 1;
}

void app_peer_transport_init(void)
{
    s_seq = 1;
    s_message_id = 1;
    s_rx_cb = 0;
    s_tx_cb = 0;
    app_peer_transport_debug_reset();
}

void app_peer_transport_poll(void)
{
    rx_poll_timeouts();
    if (s_ack.active) {
        if (send_ack_once() == APP_OK) {
            return;
        }
    }
    tx_poll();
}

void app_peer_transport_debug_reset(void)
{
    memset(&s_debug, 0, sizeof(s_debug));
    memset(&s_tx, 0, sizeof(s_tx));
    memset(s_rx, 0, sizeof(s_rx));
    memset(s_completed, 0, sizeof(s_completed));
    memset(&s_ack, 0, sizeof(s_ack));
    memset(&s_debug_drop, 0, sizeof(s_debug_drop));
    s_debug.max_payload_len = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    s_debug.max_fragments = APP_PEER_TRANSPORT_MAX_FRAGMENTS;
}

void app_peer_transport_get_debug(app_peer_transport_debug_t *debug)
{
    u8 i;

    if (!debug) {
        return;
    }

    *debug = s_debug;
    debug->max_payload_len = APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN;
    debug->max_fragments = APP_PEER_TRANSPORT_MAX_FRAGMENTS;
    debug->tx_active = s_tx.active;
    debug->tx_frag_count = s_tx.fragment_count;
    debug->tx_pending = (u8)(s_tx.pending_bitmap & 0xff);
    debug->tx_ack_bits = (u8)(s_tx.ack_bitmap & 0xff);
    debug->tx_retry_round = s_tx.retry_round;
    debug->rx_active = 0;
    debug->rx_frag_count = 0;
    debug->rx_bitmap = 0;
    debug->rx_drop_mask = (u8)(s_debug_drop.mask & 0xff);
    for (i = 0; i < APP_PEER_RX_CONTEXT_COUNT; i++) {
        if (s_rx[i].active) {
            debug->rx_active++;
            debug->rx_frag_count = s_rx[i].fragment_count;
            debug->rx_bitmap = (u8)(s_rx[i].received_bitmap & 0xff);
        }
    }
}

void app_peer_transport_debug_drop_next_rx(u32 fragment_bitmap)
{
    fragment_bitmap &= bitmap_for_count(APP_PEER_TRANSPORT_MAX_FRAGMENTS);
    memset(&s_debug_drop, 0, sizeof(s_debug_drop));
    s_debug_drop.mask = fragment_bitmap;
    s_debug.rx_drop_mask = (u8)(fragment_bitmap & 0xff);
}

app_status_t app_peer_transport_send(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                     const u8 *payload, u8 len)
{
    return app_peer_transport_send_message(dst_eid, type, payload, len,
                                           APP_PEER_SEND_UNRELIABLE, 0);
}

static app_status_t start_tx_message(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                     const u8 *payload, u16 len,
                                     app_peer_send_mode_t mode, u8 flags, u8 fill_pattern)
{
    app_eid_t zero_eid;
    u8 fragment_count;
    u16 i;

    if ((len && !payload && !fill_pattern) || len > APP_PEER_TRANSPORT_MESSAGE_MAX_LEN) {
        s_debug.tx_fail++;
        s_debug.last_status = APP_ERR_PARAM;
        return APP_ERR_PARAM;
    }
    if (s_tx.active) {
        s_debug.tx_fail++;
        s_debug.last_status = APP_ERR_BUSY;
        return APP_ERR_BUSY;
    }

    memset(&zero_eid, 0, sizeof(zero_eid));
    if (mode == APP_PEER_SEND_RELIABLE && (!dst_eid || app_eid_is_zero(dst_eid))) {
        s_debug.tx_fail++;
        s_debug.last_status = APP_ERR_UNSUPPORTED;
        return APP_ERR_UNSUPPORTED;
    }

    fragment_count = fragment_count_for_len(len);
    if (!fragment_count) {
        s_debug.tx_fail++;
        s_debug.last_status = APP_ERR_PARAM;
        return APP_ERR_PARAM;
    }

    memset(&s_tx, 0, sizeof(s_tx));
    s_tx.active = 1;
    s_tx.reliable = mode == APP_PEER_SEND_RELIABLE;
    s_tx.dst_eid = dst_eid ? *dst_eid : zero_eid;
    s_tx.type = type;
    s_tx.flags = flags;
    s_tx.message_id = next_message_id();
    s_tx.len = len;
    s_tx.fragment_count = fragment_count;
    s_tx.all_bitmap = bitmap_for_count(fragment_count);
    s_tx.pending_bitmap = s_tx.all_bitmap;
    s_tx.first_tick = clock_time();
    s_tx.last_tx_tick = 0;
    s_tx.last_ack_tick = 0;
    if (fill_pattern) {
        for (i = 0; i < len; i++) {
            s_tx.payload[i] = (u8)(i + 0x30);
        }
    } else if (len) {
        memcpy(s_tx.payload, payload, len);
    }

    s_debug.tx_ok++;
    s_debug.last_status = APP_OK;
    s_debug.last_tx_type = (u8)type;
    s_debug.last_tx_msg_len = len;
    app_peer_transport_poll();
    return APP_OK;
}

app_status_t app_peer_transport_send_message(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                             const u8 *payload, u16 len,
                                             app_peer_send_mode_t mode, u8 flags)
{
    return start_tx_message(dst_eid, type, payload, len, mode, flags, 0);
}

app_status_t app_peer_transport_send_test_pattern(const app_eid_t *dst_eid,
                                                  app_peer_msg_type_t type,
                                                  u16 len,
                                                  app_peer_send_mode_t mode,
                                                  u8 flags)
{
    return start_tx_message(dst_eid, type, 0, len, mode, flags, 1);
}

void app_peer_transport_set_rx_callback(app_peer_transport_rx_cb_t cb)
{
    s_rx_cb = cb;
}

void app_peer_transport_set_tx_callback(app_peer_transport_tx_cb_t cb)
{
    s_tx_cb = cb;
}

u8 app_peer_transport_on_adv_frame(const app_adv_frame_t *frame, s8 rssi)
{
    if (!frame) {
        return 0;
    }
    if (frame->type == ADV_FRAME_ACK) {
        return handle_ack_frame(frame);
    }
    if (frame->type != ADV_FRAME_DATA) {
        return 0;
    }
    if (handle_data_v2(frame, rssi)) {
        return 1;
    }
    return handle_data_v1(frame, rssi);
}
