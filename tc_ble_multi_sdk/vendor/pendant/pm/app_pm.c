#include "app_pm.h"
#include "../motion/app_motion.h"
#include "drivers.h"
#include "timer.h"

static app_sleep_reason_t s_sleep_reason;
static app_wakeup_reason_t s_wakeup_reason;
static u8 s_raw_wakeup_src;

void app_pm_init(void)
{
    s_sleep_reason = PM_SLEEP_REASON_IDLE;
    s_raw_wakeup_src = (u8)pm_get_wakeup_src();
    if (s_raw_wakeup_src & WAKEUP_STATUS_WD) {
        s_wakeup_reason = PM_WAKEUP_REASON_RESET;
    } else if (s_raw_wakeup_src & WAKEUP_STATUS_PAD) {
        s_wakeup_reason = PM_WAKEUP_REASON_MOTION;
    } else {
        s_wakeup_reason = PM_WAKEUP_REASON_UNKNOWN;
    }
}

app_wakeup_reason_t app_pm_get_wakeup_reason(void)
{
    return s_wakeup_reason;
}

u8 app_pm_get_raw_wakeup_src(void)
{
    return s_raw_wakeup_src;
}

app_status_t app_pm_prepare_sleep(app_sleep_reason_t reason)
{
    u32 wake_pin;
    s_sleep_reason = reason;
    wake_pin = app_motion_get_wakeup_pin();
    if (wake_pin) {
        cpu_set_gpio_wakeup(wake_pin, Level_High, 1);
    }
    return APP_OK;
}

void app_pm_enter_sleep(void)
{
    (void)s_sleep_reason;
    cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
}

void app_pm_poll(u32 now_tick)
{
    (void)now_tick;
}
