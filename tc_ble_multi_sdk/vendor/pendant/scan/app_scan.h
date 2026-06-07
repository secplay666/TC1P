#ifndef APP_SCAN_H_
#define APP_SCAN_H_

#include "../common/app_types.h"

typedef struct {
    const u8 *addr;
    s8 rssi;
    const u8 *adv_data;
    u8 adv_len;
} app_scan_report_t;

typedef struct {
    u32 reports;
    u32 decode_ok;
    u32 decode_fail;
    u32 vendor_decode_fail;
    u32 self_ignored;
    u32 beacon_rx;
    u32 data_rx;
    u32 other_rx;
    u8 last_adv_len;
    u8 last_type;
    u8 last_payload_len;
    u8 last_src0;
    u8 last_src1;
    s8 last_rssi;
} app_scan_debug_t;

void app_scan_init(void);
void app_scan_on_report(const app_scan_report_t *report);
void app_scan_poll(void);
void app_scan_debug_reset(void);
void app_scan_get_debug(app_scan_debug_t *debug);

#endif
