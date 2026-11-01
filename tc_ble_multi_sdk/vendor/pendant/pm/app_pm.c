#include "app_pm.h"
#include "../motion/app_motion.h"
#include "../app_config.h"
#include "stack/ble/ble.h"
#include "drivers.h"
#include "timer.h"

#define APP_PM_BLE_SLEEP_MASK  (PM_SLEEP_LEG_ADV | PM_SLEEP_EXT_ADV | PM_SLEEP_ACL_PERIPHR)
#define APP_PM_ACTIVITY_HOLD_US 1000000

static u8 s_ble_sleep_blockers;
static u8 s_activity_hold;
static u32 s_activity_tick;

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

void app_pm_init_ble(void)
{
#if (PENDANT_BLE_PM_ENABLE)
    blc_ll_initPowerManagement_module();
    /* Suspend only: application RAM is not placed in the retention area. */
    blc_pm_setDeepsleepRetentionEnable(PM_DeepRetn_Disable);
    s_ble_sleep_blockers = 0;
    blc_pm_setSleepMask(APP_PM_BLE_SLEEP_MASK);
#endif
}

void app_pm_block_ble_sleep(u8 blocker, u8 block)
{
#if (PENDANT_BLE_PM_ENABLE)
    u8 before = s_ble_sleep_blockers;

    if (block) {
        s_ble_sleep_blockers |= blocker;
    } else {
        s_ble_sleep_blockers &= (u8)~blocker;
    }
    if (!before != !s_ble_sleep_blockers) {
        blc_pm_setSleepMask(s_ble_sleep_blockers ? PM_SLEEP_DISABLE : APP_PM_BLE_SLEEP_MASK);
    }
#else
    (void)blocker;
    (void)block;
#endif
}

void app_pm_note_host_activity(void)
{
    s_activity_tick = clock_time();
    if (!s_activity_hold) {
        s_activity_hold = 1;
        app_pm_block_ble_sleep(APP_PM_BLOCK_HOST_ACTIVITY, 1);
    }
}

void app_pm_poll(u32 now_tick)
{
    (void)now_tick;
    if (s_activity_hold && clock_time_exceed(s_activity_tick, APP_PM_ACTIVITY_HOLD_US)) {
        s_activity_hold = 0;
        app_pm_block_ble_sleep(APP_PM_BLOCK_HOST_ACTIVITY, 0);
    }
}
