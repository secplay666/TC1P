#include "app_motion.h"
#include "../board/app_board.h"
#include "../event/app_event.h"
#include "drivers.h"
#include "timer.h"

#define MOTION_DEBOUNCE_US 50000

static app_motion_state_t s_motion;
static u32 s_pin;
static u32 s_last_check_tick;

void app_motion_init(void)
{
    s_pin = app_board_get_pin(APP_PIN_MOTION);
    gpio_set_input_en(s_pin, 1);
    s_motion.current_level = gpio_read(s_pin) ? 1 : 0;
    s_motion.motion_count = 0;
    s_motion.last_motion_tick = 0;
    s_last_check_tick = clock_time();
}

void app_motion_poll(u32 now_tick)
{
    u8 level;
    (void)now_tick;
    level = gpio_read(s_pin) ? 1 : 0;
    if (level != s_motion.current_level && clock_time_exceed(s_last_check_tick, MOTION_DEBOUNCE_US)) {
        s_motion.current_level = level;
        app_motion_on_gpio_edge();
    }
    if (level == s_motion.current_level) {
        s_last_check_tick = clock_time();
    }
}

void app_motion_on_gpio_edge(void)
{
    s_motion.motion_count++;
    s_motion.last_motion_tick = clock_time();
    app_event_post(APP_EVT_MOTION_DETECTED, &s_motion.motion_count, sizeof(s_motion.motion_count));
}

void app_motion_get_state(app_motion_state_t *state)
{
    if (state) {
        *state = s_motion;
    }
}

u32 app_motion_get_wakeup_pin(void)
{
    return s_pin;
}
