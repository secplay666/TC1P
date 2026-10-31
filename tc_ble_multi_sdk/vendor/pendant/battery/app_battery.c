#include "app_battery.h"
#include "../board/app_board.h"
#include "../event/app_event.h"
#include "../app_config.h"
#include "drivers.h"
#include "timer.h"

#define BATTERY_LOW_MV         3300
#define BATTERY_CRITICAL_MV    3000
/* Readings outside this window mean the divider/ADC is not what we expect
 * (pin floating, different board); they are reported but never act. */
#define BATTERY_VALID_MIN_MV   2500
#define BATTERY_VALID_MAX_MV   4600
#define BATTERY_SAMPLE_US      1000000
#define BATTERY_AVG_COUNT      4
/* PB1 sees 78.4% of the battery voltage through the board divider. */
#define BATTERY_DIVIDER_NUM    1000
#define BATTERY_DIVIDER_DEN    784

#ifndef PENDANT_BATTERY_SLEEP_ON_CRITICAL
#define PENDANT_BATTERY_SLEEP_ON_CRITICAL 0
#endif

static app_battery_state_t s_battery;
static u32 s_last_sample_tick;
static u32 s_pin;
static u16 s_history[BATTERY_AVG_COUNT];
static u8 s_history_count;
static u8 s_history_pos;

/* Single-cell Li-ion open-circuit curve, mV -> percent. */
static const u16 s_curve_mv[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
static const u8 s_curve_pct[] = {0, 5, 12, 30, 50, 65, 78, 90, 100};

static u8 app_battery_percent_from_mv(u16 mv)
{
    u8 i;

    if (mv <= s_curve_mv[0]) {
        return 0;
    }
    for (i = 1; i < sizeof(s_curve_mv) / sizeof(s_curve_mv[0]); i++) {
        if (mv <= s_curve_mv[i]) {
            return (u8)(s_curve_pct[i - 1] +
                        ((u32)(mv - s_curve_mv[i - 1]) * (s_curve_pct[i] - s_curve_pct[i - 1])) /
                            (s_curve_mv[i] - s_curve_mv[i - 1]));
        }
    }
    return 100;
}

static u16 app_battery_read_mv(void)
{
    u32 pin_mv;

    adc_base_init((GPIO_PinTypeDef)s_pin);
    adc_power_on_sar_adc(1);
    pin_mv = adc_sample_and_get_result();
    adc_power_on_sar_adc(0);
    return (u16)(pin_mv * BATTERY_DIVIDER_NUM / BATTERY_DIVIDER_DEN);
}

void app_battery_init(void)
{
    memset(&s_battery, 0, sizeof(s_battery));
    s_battery.min_mv = 0xffff;
    s_history_count = 0;
    s_history_pos = 0;
    s_last_sample_tick = 0;
    s_pin = app_board_get_pin(APP_PIN_BAT_ADC);
    if (s_pin) {
        adc_init();
    }
}

app_status_t app_battery_sample(void)
{
    u16 raw;
    u32 sum = 0;
    u8 i;

    if (!s_pin) {
        return APP_ERR_STATE;
    }
    raw = app_battery_read_mv();
    s_battery.raw_mv = raw;
    s_battery.valid = raw >= BATTERY_VALID_MIN_MV && raw <= BATTERY_VALID_MAX_MV;
    if (!s_battery.valid) {
        return APP_ERR_STATE;
    }

    s_history[s_history_pos] = raw;
    s_history_pos = (u8)((s_history_pos + 1) % BATTERY_AVG_COUNT);
    if (s_history_count < BATTERY_AVG_COUNT) {
        s_history_count++;
    }
    for (i = 0; i < s_history_count; i++) {
        sum += s_history[i];
    }
    s_battery.voltage_mv = (u16)(sum / s_history_count);
    if (raw < s_battery.min_mv) {
        s_battery.min_mv = raw;
    }
    s_battery.percent = app_battery_percent_from_mv(s_battery.voltage_mv);
    s_battery.low = s_battery.voltage_mv <= BATTERY_LOW_MV;
    s_battery.critical = s_battery.voltage_mv <= BATTERY_CRITICAL_MV;

    /* Only act on a full averaging window so one bad sample cannot sleep the device. */
    if (s_history_count < BATTERY_AVG_COUNT) {
        return APP_OK;
    }
#if (PENDANT_BATTERY_SLEEP_ON_CRITICAL)
    if (s_battery.critical) {
        app_event_post(APP_EVT_BATTERY_CRITICAL, &s_battery.voltage_mv, sizeof(s_battery.voltage_mv));
        return APP_OK;
    }
#endif
    if (s_battery.low) {
        app_event_post(APP_EVT_BATTERY_LOW, &s_battery.voltage_mv, sizeof(s_battery.voltage_mv));
    }
    return APP_OK;
}

void app_battery_poll(u32 now_tick)
{
    (void)now_tick;
    if (!s_last_sample_tick || clock_time_exceed(s_last_sample_tick, BATTERY_SAMPLE_US)) {
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
