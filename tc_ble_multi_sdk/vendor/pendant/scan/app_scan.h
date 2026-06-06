#ifndef APP_SCAN_H_
#define APP_SCAN_H_

#include "../common/app_types.h"

typedef struct {
    const u8 *addr;
    s8 rssi;
    const u8 *adv_data;
    u8 adv_len;
} app_scan_report_t;

void app_scan_init(void);
void app_scan_on_report(const app_scan_report_t *report);
void app_scan_poll(void);
void app_scan_debug_reset(void);

#endif
