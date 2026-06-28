#include "app_scan.h"
#include "../app_config.h"
#include "../adv_proto/app_adv_proto.h"
#include "../discovery/app_discovery.h"
#include "../host_adv/app_host_adv.h"
#include "../identity/app_identity.h"
#include "../peer_table/app_peer_table.h"
#include "../profile/app_profile.h"
#include "../peer_transport/app_peer_transport.h"
#include "../common/app_debug_print.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

static u8 s_vendor_decode_log_count;
static u8 s_beacon_log_count;
static u8 s_data_log_count;
static app_scan_debug_t s_debug;

#define APP_SCAN_RX_LOG_ENABLE 0
#define APP_SCAN_DEFER_QUEUE_SIZE 1
#define APP_SCAN_DEFER_PAYLOAD_MAX_LEN (APP_PEER_TRANSPORT_HEADER_LEN + APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN)

typedef struct {
    app_adv_frame_t frame;
    u8 payload[APP_SCAN_DEFER_PAYLOAD_MAX_LEN];
    s8 rssi;
} app_scan_deferred_frame_t;

static app_scan_deferred_frame_t s_defer_q[APP_SCAN_DEFER_QUEUE_SIZE];
static u8 s_defer_head;
static u8 s_defer_tail;
static u8 s_defer_count;
static u8 s_beacon_pending;
static app_eid_t s_beacon_eid;
static s8 s_beacon_rssi;

static void refresh_peer_activity(const app_adv_frame_t *frame, s8 rssi)
{
    app_peer_record_t *peer;

    if (!frame || app_eid_is_zero(&frame->src_eid)) {
        return;
    }

    peer = app_peer_table_find_or_alloc(&frame->src_eid);
    if (!peer) {
        return;
    }

    peer->last_seen_tick = clock_time();
    peer->rssi = rssi;
    if (!peer->rssi_avg) {
        peer->rssi_avg = rssi;
    }
}

static u8 app_scan_defer_frame(const app_adv_frame_t *frame, s8 rssi)
{
    app_scan_deferred_frame_t *item;

    if (!frame || frame->payload_len > APP_SCAN_DEFER_PAYLOAD_MAX_LEN ||
        (frame->payload_len && !frame->payload)) {
        return 0;
    }
    if (s_defer_count >= APP_SCAN_DEFER_QUEUE_SIZE) {
        s_debug.defer_full++;
        return 0;
    }

    item = &s_defer_q[s_defer_tail];
    item->frame = *frame;
    if (frame->payload_len) {
        memcpy(item->payload, frame->payload, frame->payload_len);
        item->frame.payload = item->payload;
    } else {
        item->frame.payload = 0;
    }
    item->rssi = rssi;
    s_defer_tail = (u8)((s_defer_tail + 1) % APP_SCAN_DEFER_QUEUE_SIZE);
    s_defer_count++;
    return 1;
}

#if APP_SCAN_RX_LOG_ENABLE
static void scan_debug_u8(const char *label, u8 value)
{
    u_printf(label);
    u_printf("%x\r\n", value);
}

static void scan_debug_s8(const char *label, s8 value)
{
    u_printf(label);
    u_printf("%d\r\n", value);
}
#endif

void app_scan_init(void)
{
    app_scan_debug_reset();
}

void app_scan_on_report(const app_scan_report_t *report)
{
    app_adv_frame_t frame;
    app_status_t st;
    const u8 *vendor_payload = 0;
    u8 vendor_len = 0;

    if (!report || !report->adv_data || !report->adv_len) {
        return;
    }

    s_debug.reports++;
    s_debug.last_adv_len = report->adv_len;
    s_debug.last_rssi = report->rssi;

    st = app_adv_proto_decode(report->adv_data, report->adv_len, &frame);
    if (st != APP_OK) {
        s_debug.decode_fail++;
        if (app_adv_proto_is_manufacturer_payload(report->adv_data, report->adv_len, &vendor_payload, &vendor_len)) {
            s_debug.vendor_decode_fail++;
        }
#if APP_SCAN_RX_LOG_ENABLE
        if (vendor_payload && s_vendor_decode_log_count < 12) {
            u_printf("[SCAN] vendor decode fail\r\n");
            scan_debug_u8(" st=", (u8)st);
            scan_debug_u8(" adv_len=", report->adv_len);
            scan_debug_u8(" vendor_len=", vendor_len);
            scan_debug_u8(" b0=", vendor_len > 0 ? vendor_payload[0] : 0);
            scan_debug_u8(" b1=", vendor_len > 1 ? vendor_payload[1] : 0);
            s_vendor_decode_log_count++;
        }
#else
        (void)vendor_payload;
        (void)vendor_len;
        (void)s_vendor_decode_log_count;
#endif
        return;
    }
    s_debug.decode_ok++;
    if (app_eid_equal(&frame.src_eid, app_identity_get_eid())) {
        s_debug.self_ignored++;
        return;
    }
    s_debug.last_type = (u8)frame.type;
    s_debug.last_payload_len = frame.payload_len;
    s_debug.last_src0 = frame.src_eid.bytes[0];
    s_debug.last_src1 = frame.src_eid.bytes[1];
    if (frame.type == ADV_FRAME_BEACON) {
        app_peer_profile_t profile;
        app_status_t profile_st;
        s_debug.beacon_rx++;
#if APP_SCAN_RX_LOG_ENABLE
        if (s_beacon_log_count < 30) {
            u_printf("[SCAN] peer beacon\r\n");
            scan_debug_s8(" rssi=", report->rssi);
            scan_debug_u8(" seq_lo=", (u8)frame.frame_seq);
            scan_debug_u8(" payload=", frame.payload_len);
            scan_debug_u8(" src0=", frame.src_eid.bytes[0]);
            scan_debug_u8(" src1=", frame.src_eid.bytes[1]);
            s_beacon_log_count++;
        }
#else
        (void)s_beacon_log_count;
#endif
        s_beacon_eid = frame.src_eid;
        s_beacon_rssi = report->rssi;
        s_beacon_pending = 1;
        memset(&profile, 0, sizeof(profile));
        profile_st = frame.payload_len > 21 ?
            app_profile_parse_adv_block(&frame.payload[21], (u8)(frame.payload_len - 21), &profile) :
            APP_ERR_NOT_FOUND;
        if (profile_st == APP_OK) {
            app_profile_cache_peer(&frame.src_eid, report->rssi, &profile);
        } else if (frame.payload_len <= 21) {
            app_profile_remove_peer(&frame.src_eid);
        }
    } else if (frame.type == ADV_FRAME_DATA) {
        s_debug.data_rx++;
        refresh_peer_activity(&frame, report->rssi);
#if APP_SCAN_RX_LOG_ENABLE
        if (s_data_log_count < 20) {
            u_printf("[SCAN] data frame\r\n");
            scan_debug_s8(" rssi=", report->rssi);
            scan_debug_u8(" seq_lo=", (u8)frame.frame_seq);
            scan_debug_u8(" payload=", frame.payload_len);
            scan_debug_u8(" src0=", frame.src_eid.bytes[0]);
            scan_debug_u8(" dst0=", frame.dst_eid.bytes[0]);
            s_data_log_count++;
        }
#else
        (void)s_data_log_count;
#endif
        if (!app_scan_defer_frame(&frame, report->rssi)) {
            s_debug.other_rx++;
        }
    } else if (frame.type == ADV_FRAME_ACK) {
        s_debug.data_rx++;
        refresh_peer_activity(&frame, report->rssi);
        if (!app_scan_defer_frame(&frame, report->rssi)) {
            s_debug.other_rx++;
        }
    } else {
        s_debug.other_rx++;
    }
}

void app_scan_poll(void)
{
    app_scan_deferred_frame_t *item;
    u8 peer_frame_handled;

    if (s_beacon_pending) {
        app_eid_t eid = s_beacon_eid;
        s8 rssi = s_beacon_rssi;
        s_beacon_pending = 0;
        app_discovery_on_beacon(&eid, rssi, clock_time());
    }

    if (!s_defer_count) {
        return;
    }

    item = &s_defer_q[s_defer_head];
    if (item->frame.payload_len) {
        item->frame.payload = item->payload;
    }
    peer_frame_handled = app_peer_transport_on_adv_frame(&item->frame, item->rssi);
#if (APP_HOST_ENABLE_ADV_TRANSPORT)
    if (!peer_frame_handled) {
        app_host_adv_on_adv_frame(&item->frame, item->rssi);
    }
#else
    (void)peer_frame_handled;
#endif
    memset(item, 0, sizeof(*item));
    s_defer_head = (u8)((s_defer_head + 1) % APP_SCAN_DEFER_QUEUE_SIZE);
    s_defer_count--;
}

void app_scan_debug_reset(void)
{
    s_vendor_decode_log_count = 0;
    s_beacon_log_count = 0;
    s_data_log_count = 0;
    s_defer_head = 0;
    s_defer_tail = 0;
    s_defer_count = 0;
    s_beacon_pending = 0;
    memset(&s_beacon_eid, 0, sizeof(s_beacon_eid));
    s_beacon_rssi = 0;
    memset(s_defer_q, 0, sizeof(s_defer_q));
    memset(&s_debug, 0, sizeof(s_debug));
}

void app_scan_get_debug(app_scan_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
        debug->defer_count = s_defer_count;
    }
}
