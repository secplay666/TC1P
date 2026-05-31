#include "app_battery.h"
#include "../event/app_event.h"
#include "drivers.h"
#include "timer.h"

#define BATTERY_LOW_MV         3300
#define BATTERY_CRITICAL_MV    3000

static app_battery_state_t s_battery;
static u32 s_last_sample_tick;

static u8 app_battery_percent_from_mv(u16 mv)
{
    if (mv <= 3000) {
        return 0;
    }
    if (mv >= 4200) {
        return 100;
    }
    return (u8)(((u32)(mv - 3000) * 100) / 1200);
}

void app_battery_init(void)
{
    s_battery.voltage_mv = 3700;
    s_battery.percent = app_battery_percent_from_mv(s_battery.voltage_mv);
    s_battery.low = 0;
    s_battery.critical = 0;
    s_last_sample_tick = 0;
}

app_status_t app_battery_sample(void)
{
    s_battery.percent = app_battery_percent_from_mv(s_battery.voltage_mv);
    s_battery.low = s_battery.voltage_mv <= BATTERY_LOW_MV;
    s_battery.critical = s_battery.voltage_mv <= BATTERY_CRITICAL_MV;
    if (s_battery.critical) {
        app_event_post(APP_EVT_BATTERY_CRITICAL, &s_battery.voltage_mv, sizeof(s_battery.voltage_mv));
    } else if (s_battery.low) {
        app_event_post(APP_EVT_BATTERY_LOW, &s_battery.voltage_mv, sizeof(s_battery.voltage_mv));
    }
    return APP_OK;
}

void app_battery_poll(u32 now_tick)
{
    (void)now_tick;
    if (!s_last_sample_tick || clock_time_exceed(s_last_sample_tick, 5000000)) {
        s_last_sample_tick = clock_time();
        app_battery_sample();
    }
}

void app_battery_get_state(app_battery_state_t *state)
{
    if (state) {
        *state = s_battery;
    }
}
