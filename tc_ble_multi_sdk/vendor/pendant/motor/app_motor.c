#include "app_motor.h"
#include "../board/app_board.h"
#include "../config/app_config_store.h"
#include "../app_config.h"
#include "drivers.h"
#include "timer.h"

#ifndef PENDANT_MOTOR_BOOT_INIT_ENABLE
#define PENDANT_MOTOR_BOOT_INIT_ENABLE 0
#endif

#define DRV2625_I2C_ID                 0xB4
#define DRV2625_I2C_CLOCK_HZ           200000

#define DRV2625_REG_STATUS             0x01
#define DRV2625_REG_MODE               0x07
#define DRV2625_REG_CONTROL1           0x08
#define DRV2625_REG_GO                 0x0C
#define DRV2625_REG_WAVEFORM0          0x0F
#define DRV2625_REG_WAVEFORM1          0x10
#define DRV2625_REG_WAVEFORM2          0x11
#define DRV2625_REG_WAVEFORM3          0x12
#define DRV2625_REG_REPEAT             0x19
#define DRV2625_REG_RATED_VOLTAGE      0x1F
#define DRV2625_REG_OD_CLAMP           0x20
#define DRV2625_REG_DRIVE_TIME         0x27

#define DRV2625_MODE_STANDBY_OUT       0x48
#define DRV2625_MODE_AUTO_CALIB        0x4B
#define DRV2625_CONTROL_LRA            0x80
#define DRV2625_CONTROL_LIB_RAM        0x88
#define DRV2625_WAVE_CLOSE             0x54
#define DRV2625_WAVE_FAR               0x36
#define DRV2625_WAVE_ERROR             0x2F
#define DRV2625_WAVE_DELAY_100MS       0x82

#define MOTOR_BUSY_ONE_US              140000
#define MOTOR_BUSY_TWO_US              380000
#define MOTOR_BUSY_THREE_US            620000
#define MOTOR_BUSY_ERROR_US            620000

typedef struct {
    u8 wave;
    u8 repeat;
    u8 has_delay;
    u32 busy_us;
} motor_hw_pattern_t;

static u8 s_hw_ready;
static u8 s_init_attempted;
static u8 s_busy;
static u32 s_busy_start_tick;
static u32 s_busy_us;
static u32 s_trig_pin;

static u8 drv2625_read(u8 reg)
{
    return i2c_read_byte(reg, 1);
}

static void drv2625_write(u8 reg, u8 val)
{
    i2c_write_byte(reg, 1, val);
}

static void drv2625_set_register_mode(void)
{
    u8 mode = drv2625_read(DRV2625_REG_MODE);
    drv2625_write(DRV2625_REG_MODE, (u8)(mode & 0xFD));
    drv2625_write(DRV2625_REG_CONTROL1, DRV2625_CONTROL_LIB_RAM);
}

static motor_hw_pattern_t motor_get_hw_pattern(app_motor_pattern_t pattern)
{
    motor_hw_pattern_t hw;

    hw.wave = DRV2625_WAVE_CLOSE;
    hw.repeat = 0;
    hw.has_delay = 0;
    hw.busy_us = MOTOR_BUSY_ONE_US;

    switch (pattern) {
    case MOTOR_PATTERN_ONE:
        hw.wave = DRV2625_WAVE_CLOSE;
        hw.repeat = 0;
        hw.has_delay = 0;
        hw.busy_us = MOTOR_BUSY_ONE_US;
        break;
    case MOTOR_PATTERN_TWO:
        hw.wave = DRV2625_WAVE_CLOSE;
        hw.repeat = 1;
        hw.has_delay = 1;
        hw.busy_us = MOTOR_BUSY_TWO_US;
        break;
    case MOTOR_PATTERN_THREE:
        hw.wave = DRV2625_WAVE_CLOSE;
        hw.repeat = 2;
        hw.has_delay = 1;
        hw.busy_us = MOTOR_BUSY_THREE_US;
        break;
    case MOTOR_PATTERN_ERROR:
        hw.wave = DRV2625_WAVE_ERROR;
        hw.repeat = 2;
        hw.has_delay = 1;
        hw.busy_us = MOTOR_BUSY_ERROR_US;
        break;
    default:
        break;
    }

    return hw;
}

static app_status_t drv2625_init(void)
{
    u8 div = (u8)(CLOCK_SYS_CLOCK_HZ / (4 * DRV2625_I2C_CLOCK_HZ));
    s_init_attempted = 1;
    s_hw_ready = 0;

    if (!div) {
        div = 1;
    }

    i2c_gpio_set(I2C_GPIO_GROUP_B6D7);
    i2c_master_init(DRV2625_I2C_ID, div);

    drv2625_write(DRV2625_REG_MODE, DRV2625_MODE_STANDBY_OUT);
    drv2625_write(DRV2625_REG_MODE, DRV2625_MODE_AUTO_CALIB);
    drv2625_write(DRV2625_REG_CONTROL1, DRV2625_CONTROL_LRA);
    drv2625_write(DRV2625_REG_RATED_VOLTAGE, 0x46);
    drv2625_write(DRV2625_REG_OD_CLAMP, 0x5C);
    drv2625_write(DRV2625_REG_DRIVE_TIME, 0x10);
    drv2625_write(DRV2625_REG_GO, 0x01);

    sleep_us(100000);

    if (drv2625_read(DRV2625_REG_STATUS) & 0x80) {
        s_hw_ready = 0;
        return APP_ERR_STATE;
    }

    drv2625_set_register_mode();
    drv2625_write(DRV2625_REG_GO, 0x00);
    s_hw_ready = 1;
    return APP_OK;
}

void app_motor_init(void)
{
    s_trig_pin = app_board_get_pin(APP_PIN_MOTOR_TRIG);
    gpio_set_func(s_trig_pin, AS_GPIO);
    gpio_set_output_en(s_trig_pin, 1);
    gpio_set_input_en(s_trig_pin, 0);
    gpio_write(s_trig_pin, 1);

    s_busy = 0;
    s_busy_start_tick = 0;
    s_busy_us = 0;
#if (PENDANT_MOTOR_BOOT_INIT_ENABLE)
    drv2625_init();
#else
    s_hw_ready = 0;
    s_init_attempted = 0;
#endif
}

app_status_t app_motor_self_check(void)
{
    if (!s_trig_pin) {
        return APP_ERR_STATE;
    }
    return APP_OK;
}

app_status_t app_motor_play(app_motor_pattern_t pattern)
{
    motor_hw_pattern_t hw;
    const app_runtime_config_t *cfg = app_config_get();

    if (pattern == MOTOR_PATTERN_NONE || pattern == MOTOR_PATTERN_CUSTOM) {
        return APP_ERR_PARAM;
    }
    if (cfg && !cfg->vibration_enable) {
        return APP_OK;
    }
    if (!s_hw_ready) {
        if (!s_init_attempted) {
            app_status_t st = drv2625_init();
            if (st != APP_OK) {
                return st;
            }
        } else {
            return APP_ERR_STATE;
        }
    }
    if (s_busy) {
        return APP_ERR_BUSY;
    }

    hw = motor_get_hw_pattern(pattern);
    drv2625_set_register_mode();
    drv2625_write(DRV2625_REG_REPEAT, 0x00);
    drv2625_write(DRV2625_REG_WAVEFORM0, hw.wave);
    if (hw.has_delay) {
        drv2625_write(DRV2625_REG_WAVEFORM1, DRV2625_WAVE_DELAY_100MS);
        drv2625_write(DRV2625_REG_WAVEFORM2, 0x00);
    } else {
        drv2625_write(DRV2625_REG_WAVEFORM1, 0x00);
    }
    drv2625_write(DRV2625_REG_WAVEFORM3, 0x00);
    drv2625_write(DRV2625_REG_REPEAT, hw.repeat);
    drv2625_write(DRV2625_REG_GO, 0x01);

    s_busy = 1;
    s_busy_start_tick = clock_time();
    s_busy_us = hw.busy_us;
    return APP_OK;
}

app_status_t app_motor_stop(void)
{
    if (s_hw_ready) {
        drv2625_write(DRV2625_REG_GO, 0x00);
    }
    s_busy = 0;
    s_busy_start_tick = 0;
    s_busy_us = 0;
    return APP_OK;
}

void app_motor_poll(u32 now_tick)
{
    (void)now_tick;
    if (s_busy && clock_time_exceed(s_busy_start_tick, s_busy_us)) {
        s_busy = 0;
    }
}

u8 app_motor_is_busy(void)
{
    return s_busy;
}
