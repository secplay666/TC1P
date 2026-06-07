#include "app_scan.h"
#include "../app_config.h"
#include "../adv_proto/app_adv_proto.h"
#include "../discovery/app_discovery.h"
#include "../host_adv/app_host_adv.h"
#include "../identity/app_identity.h"
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
        app_discovery_on_beacon(&frame.src_eid, report->rssi, clock_time());
    } else if (frame.type == ADV_FRAME_DATA) {
        u8 peer_frame_handled;
        s_debug.data_rx++;
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
        peer_frame_handled = app_peer_transport_on_adv_frame(&frame, report->rssi);
#if (APP_HOST_ENABLE_ADV_TRANSPORT)
        if (!peer_frame_handled) {
            app_host_adv_on_adv_frame(&frame, report->rssi);
        }
#else
        (void)peer_frame_handled;
#endif
    } else {
        s_debug.other_rx++;
    }
}

void app_scan_poll(void)
{
}

void app_scan_debug_reset(void)
{
    s_vendor_decode_log_count = 0;
    s_beacon_log_count = 0;
    s_data_log_count = 0;
    memset(&s_debug, 0, sizeof(s_debug));
}

void app_scan_get_debug(app_scan_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
    }
}
