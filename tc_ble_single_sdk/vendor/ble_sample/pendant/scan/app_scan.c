#include "app_scan.h"
#include "../adv_proto/app_adv_proto.h"
#include "../discovery/app_discovery.h"
#include "../identity/app_identity.h"
#include "drivers.h"
#include "timer.h"

void app_scan_init(void)
{
}

void app_scan_on_report(const app_scan_report_t *report)
{
    app_adv_frame_t frame;
    if (!report || !report->adv_data || !report->adv_len) {
        return;
    }
    if (app_adv_proto_decode(report->adv_data, report->adv_len, &frame) != APP_OK) {
        return;
    }
    if (app_eid_equal(&frame.src_eid, app_identity_get_eid())) {
        return;
    }
    if (frame.type == ADV_FRAME_BEACON) {
        app_discovery_on_beacon(&frame.src_eid, report->rssi, clock_time());
    }
}

void app_scan_poll(void)
{
}
