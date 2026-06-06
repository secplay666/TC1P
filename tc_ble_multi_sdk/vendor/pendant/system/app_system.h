#ifndef APP_SYSTEM_H_
#define APP_SYSTEM_H_

#include "../common/app_types.h"

typedef struct {
    app_system_state_t state;
    app_system_state_t previous_state;
    u16 error_code;
    u8 wakeup_reason;
    u32 uptime_s;
} app_system_snapshot_t;

void app_system_init(void);
void app_system_poll(void);
void app_system_handle_event(const app_event_t *event);
app_system_state_t app_system_get_state(void);
void app_system_get_snapshot(app_system_snapshot_t *snapshot);
app_status_t app_system_request_sleep(u8 reason);
app_status_t app_system_report_error(u16 error_code, u16 detail);

#endif
