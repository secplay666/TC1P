#include "app_motor.h"
#include "../board/app_board.h"
#include "drivers.h"
#include "timer.h"

#define MOTOR_PULSE_MS        80
#define MOTOR_GAP_MS          120

__attribute__((weak)) int pendant_motor_hw_init(void)
{
    return 0;
}

__attribute__((weak)) int pendant_motor_hw_play(u8 pattern)
{
    (void)pattern;
    return 0;
}

__attribute__((weak)) void pendant_motor_hw_stop(void)
{
}

typedef enum {
    MOTOR_RUN_IDLE = 0,
    MOTOR_RUN_PULSE_ON,
    MOTOR_RUN_PULSE_GAP,
} motor_run_state_t;

static motor_run_state_t s_state;
static u8 s_total_pulses;
static u8 s_done_pulses;
static u32 s_next_tick;
static u32 s_trig_pin;

static u8 app_motor_pattern_count(app_motor_pattern_t pattern)
{
    switch (pattern) {
    case MOTOR_PATTERN_ONE:
        return 1;
    case MOTOR_PATTERN_TWO:
        return 2;
    case MOTOR_PATTERN_THREE:
    case MOTOR_PATTERN_ERROR:
        return 3;
    default:
        return 0;
    }
}

void app_motor_init(void)
{
    s_trig_pin = app_board_get_pin(APP_PIN_MOTOR_TRIG);
    gpio_set_output_en(s_trig_pin, 1);
    gpio_write(s_trig_pin, 0);
    pendant_motor_hw_init();
    s_state = MOTOR_RUN_IDLE;
    s_total_pulses = 0;
    s_done_pulses = 0;
    s_next_tick = 0;
}

app_status_t app_motor_self_check(void)
{
    return s_trig_pin ? APP_OK : APP_ERR_STATE;
}

app_status_t app_motor_play(app_motor_pattern_t pattern)
{
    u8 count = app_motor_pattern_count(pattern);
    if (!count) {
        return APP_ERR_PARAM;
    }

    pendant_motor_hw_play((u8)pattern);
    s_total_pulses = count;
    s_done_pulses = 0;
    s_state = MOTOR_RUN_PULSE_ON;
    gpio_write(s_trig_pin, 1);
    s_next_tick = clock_time();
    return APP_OK;
}

app_status_t app_motor_stop(void)
{
    gpio_write(s_trig_pin, 0);
    pendant_motor_hw_stop();
    s_state = MOTOR_RUN_IDLE;
    s_total_pulses = 0;
    s_done_pulses = 0;
    return APP_OK;
}

void app_motor_poll(u32 now_tick)
{
    (void)now_tick;

    if (s_state == MOTOR_RUN_IDLE) {
        return;
    }

    if (s_state == MOTOR_RUN_PULSE_ON) {
        if (clock_time_exceed(s_next_tick, MOTOR_PULSE_MS * 1000)) {
            gpio_write(s_trig_pin, 0);
            s_done_pulses++;
            s_next_tick = clock_time();
            s_state = (s_done_pulses >= s_total_pulses) ? MOTOR_RUN_IDLE : MOTOR_RUN_PULSE_GAP;
        }
    } else if (s_state == MOTOR_RUN_PULSE_GAP) {
        if (clock_time_exceed(s_next_tick, MOTOR_GAP_MS * 1000)) {
            gpio_write(s_trig_pin, 1);
            s_next_tick = clock_time();
            s_state = MOTOR_RUN_PULSE_ON;
        }
    }
}

u8 app_motor_is_busy(void)
{
    return s_state != MOTOR_RUN_IDLE;
}
