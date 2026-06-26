#include "app_system.h"
#include "../board/app_board.h"
#include "../event/app_event.h"
#include "../diag/app_diag.h"
#include "../storage/app_storage.h"
#include "../config/app_config_store.h"
#include "../identity/app_identity.h"
#include "../profile/app_profile.h"
#include "../ble/app_ble.h"
#include "../pm/app_pm.h"
#include "../app_config.h"
#include "drivers.h"
#include "timer.h"

static app_system_snapshot_t s_system;
static u32 s_boot_tick;

static void app_system_set_state(app_system_state_t state)
{
    if (s_system.state != state) {
        s_system.previous_state = s_system.state;
        s_system.state = state;
    }
}

static app_status_t app_system_self_check(void)
{
    app_status_t st;

    st = app_board_self_check();
    if (st != APP_OK) {
        return st;
    }

    st = app_storage_self_check();
    if (st != APP_OK) {
        return st;
    }

    st = app_config_load();
    if (st != APP_OK) {
        app_config_reset_default();
    }

    st = app_identity_load();
    if (st != APP_OK) {
        return st;
    }

    st = app_identity_self_check();
    if (st != APP_OK) {
        return st;
    }

    st = app_profile_load();
    if (st != APP_OK) {
        return st;
    }

    return APP_OK;
}

void app_system_init(void)
{
    s_system.state = SYS_STATE_BOOT;
    s_system.previous_state = SYS_STATE_BOOT;
    s_system.error_code = 0;
    s_system.wakeup_reason = app_pm_get_wakeup_reason();
    s_system.uptime_s = 0;
    s_boot_tick = clock_time();
#if (PENDANT_WATCHDOG_ENABLE)
    wd_set_interval_ms(PENDANT_WATCHDOG_TIMEOUT_MS, CLOCK_SYS_CLOCK_HZ / 1000);
    wd_clear();
    wd_start();
#endif
    app_event_post(APP_EVT_BOOT_DONE, 0, 0);
}

void app_system_poll(void)
{
    app_event_t evt;

#if (PENDANT_WATCHDOG_ENABLE)
    wd_clear();
#endif

    if (clock_time_exceed(s_boot_tick, 1000000)) {
        s_system.uptime_s++;
        s_boot_tick = clock_time();
    }

    while (app_event_fetch(&evt) == APP_OK) {
        app_system_handle_event(&evt);
    }
}

void app_system_handle_event(const app_event_t *event)
{
    if (!event) {
        return;
    }

    switch (event->id) {
    case APP_EVT_BOOT_DONE:
        app_system_set_state(SYS_STATE_SELF_CHECK);
        if (app_system_self_check() == APP_OK) {
            app_event_post(APP_EVT_SELF_CHECK_OK, 0, 0);
        } else {
            app_event_post(APP_EVT_SELF_CHECK_FAIL, 0, 0);
        }
        break;

    case APP_EVT_SELF_CHECK_OK:
#if (PENDANT_BLE_AUTO_START)
        app_ble_start_adv_scan(0);
#endif
        app_system_set_state(SYS_STATE_ADV_SCAN);
        break;

    case APP_EVT_SELF_CHECK_FAIL:
        app_system_report_error(0x0201, 0);
        break;

    case APP_EVT_APP_CONNECTED:
        app_system_set_state(SYS_STATE_APP_CONNECTED);
        break;

    case APP_EVT_APP_DISCONNECTED:
        if (s_system.state == SYS_STATE_APP_CONNECTED) {
            app_system_set_state(SYS_STATE_ADV_SCAN);
        }
        break;

    case APP_EVT_IDLE_TIMEOUT:
    case APP_EVT_APP_IDLE_TIMEOUT:
    case APP_EVT_BATTERY_CRITICAL:
        app_system_request_sleep((u8)event->id);
        break;

    case APP_EVT_FATAL_ERROR:
        app_system_report_error(0x0001, 0);
        break;

    case APP_EVT_SLEEP_READY:
        app_pm_enter_sleep();
        break;

    default:
        break;
    }
}

app_system_state_t app_system_get_state(void)
{
    return s_system.state;
}

void app_system_get_snapshot(app_system_snapshot_t *snapshot)
{
    if (snapshot) {
        *snapshot = s_system;
    }
}

app_status_t app_system_request_sleep(u8 reason)
{
    (void)reason;
    app_system_set_state(SYS_STATE_SLEEP_PREPARE);
    app_ble_stop_adv_scan();
    app_pm_prepare_sleep((app_sleep_reason_t)reason);
    app_event_post(APP_EVT_SLEEP_READY, 0, 0);
    return APP_OK;
}

app_status_t app_system_report_error(u16 error_code, u16 detail)
{
    s_system.error_code = error_code;
    app_diag_log_error(error_code, detail);
    app_system_set_state(SYS_STATE_ERROR);
    return APP_OK;
}
