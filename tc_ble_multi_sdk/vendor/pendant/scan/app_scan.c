#include "app_scan.h"
#include "../adv_proto/app_adv_proto.h"
#include "../discovery/app_discovery.h"
#include "../host_adv/app_host_adv.h"
#include "../identity/app_identity.h"
#include "../common/app_debug_print.h"
#include "drivers.h"
#include "timer.h"

static u8 s_vendor_decode_log_count;
static u8 s_beacon_log_count;
static u8 s_data_log_count;

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

void app_scan_init(void)
{
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

    st = app_adv_proto_decode(report->adv_data, report->adv_len, &frame);
    if (st != APP_OK) {
        if (app_adv_proto_is_manufacturer_payload(report->adv_data, report->adv_len, &vendor_payload, &vendor_len) &&
            s_vendor_decode_log_count < 12) {
            u_printf("[SCAN] vendor decode fail\r\n");
            scan_debug_u8(" st=", (u8)st);
            scan_debug_u8(" adv_len=", report->adv_len);
            scan_debug_u8(" vendor_len=", vendor_len);
            scan_debug_u8(" b0=", vendor_len > 0 ? vendor_payload[0] : 0);
            scan_debug_u8(" b1=", vendor_len > 1 ? vendor_payload[1] : 0);
            s_vendor_decode_log_count++;
        }
        return;
    }
    if (app_eid_equal(&frame.src_eid, app_identity_get_eid())) {
        return;
    }
    if (frame.type == ADV_FRAME_BEACON) {
        if (s_beacon_log_count < 30) {
            u_printf("[SCAN] peer beacon\r\n");
            scan_debug_s8(" rssi=", report->rssi);
            scan_debug_u8(" seq_lo=", (u8)frame.frame_seq);
            scan_debug_u8(" payload=", frame.payload_len);
            scan_debug_u8(" src0=", frame.src_eid.bytes[0]);
            scan_debug_u8(" src1=", frame.src_eid.bytes[1]);
            s_beacon_log_count++;
        }
        app_discovery_on_beacon(&frame.src_eid, report->rssi, clock_time());
    } else if (frame.type == ADV_FRAME_DATA) {
        if (s_data_log_count < 20) {
            u_printf("[SCAN] data frame\r\n");
            scan_debug_s8(" rssi=", report->rssi);
            scan_debug_u8(" seq_lo=", (u8)frame.frame_seq);
            scan_debug_u8(" payload=", frame.payload_len);
            scan_debug_u8(" src0=", frame.src_eid.bytes[0]);
            scan_debug_u8(" dst0=", frame.dst_eid.bytes[0]);
            s_data_log_count++;
        }
        app_host_adv_on_adv_frame(&frame, report->rssi);
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
}
