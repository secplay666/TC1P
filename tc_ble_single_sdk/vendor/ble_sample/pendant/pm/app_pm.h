#ifndef APP_PM_H_
#define APP_PM_H_

#include "../common/app_types.h"

typedef enum {
    PM_SLEEP_REASON_IDLE = 1,
    PM_SLEEP_REASON_CHARGE = 2,
    PM_SLEEP_REASON_APP_IDLE = 3,
    PM_SLEEP_REASON_BATTERY_CRITICAL = 4,
    PM_SLEEP_REASON_ERROR = 5,
} app_sleep_reason_t;

typedef enum {
    PM_WAKEUP_REASON_UNKNOWN = 0,
    PM_WAKEUP_REASON_MOTION = 1,
    PM_WAKEUP_REASON_CHARGE_STOPPED = 2,
    PM_WAKEUP_REASON_RESET = 3,
} app_wakeup_reason_t;

void app_pm_init(void);
app_wakeup_reason_t app_pm_get_wakeup_reason(void);
app_status_t app_pm_prepare_sleep(app_sleep_reason_t reason);
void app_pm_enter_sleep(void);
void app_pm_poll(u32 now_tick);

#endif
