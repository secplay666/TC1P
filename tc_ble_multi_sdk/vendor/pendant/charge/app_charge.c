#include "app_charge.h"
#include "../board/app_board.h"
#include "../event/app_event.h"
#include "drivers.h"
#include "timer.h"

#define CHARGE_DEBOUNCE_US 50000

static app_charge_state_t s_charge_state;
static u8 s_last_level;
static u32 s_change_tick;
static u32 s_pin;

void app_charge_init(void)
{
    s_pin = app_board_get_pin(APP_PIN_CHARGE_DET);
    gpio_set_input_en(s_pin, 1);
    s_last_level = gpio_read(s_pin) ? 1 : 0;
    s_charge_state = s_last_level ? CHARGE_STATE_CHARGING : CHARGE_STATE_NOT_CHARGING;
    s_change_tick = clock_time();
}

void app_charge_poll(u32 now_tick)
{
    u8 level;
    (void)now_tick;
    level = gpio_read(s_pin) ? 1 : 0;
    if (level != s_last_level) {
        if (clock_time_exceed(s_change_tick, CHARGE_DEBOUNCE_US)) {
            s_last_level = level;
            s_charge_state = level ? CHARGE_STATE_CHARGING : CHARGE_STATE_NOT_CHARGING;
            app_event_post(level ? APP_EVT_CHARGE_STARTED : APP_EVT_CHARGE_STOPPED, 0, 0);
            s_change_tick = clock_time();
        }
    } else {
        s_change_tick = clock_time();
    }
}

app_charge_state_t app_charge_get_state(void)
{
    return s_charge_state;
}

u8 app_charge_is_charging(void)
{
    return s_charge_state == CHARGE_STATE_CHARGING;
}
