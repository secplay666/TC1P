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
#define DRV2625_REG_DIAG_Z_RESULT      0x03
#define DRV2625_REG_LRA_PERIOD_H       0x05
#define DRV2625_REG_LRA_PERIOD_L       0x06
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
#define DRV2625_REG_ACAL_BEMF          0x22
#define DRV2625_REG_FEEDBACK_CTRL      0x23
#define DRV2625_REG_RATED_CLAMP        0x24
#define DRV2625_REG_DRIVE_TIME         0x27
#define DRV2625_REG_AUTO_CAL_TIME      0x2A
#define DRV2625_REG_CONTROL3           0x2C
#define DRV2625_REG_OL_LRA_PERIOD_H    0x2E
#define DRV2625_REG_OL_LRA_PERIOD_L    0x2F

#define DRV2625_GO_PLAY                0x01
#define DRV2625_MODE_STANDBY_OUT       0x48
#define DRV2625_MODE_AUTO_CALIB        0x4B
#define DRV2625_CONTROL_LRA            0x80
#define DRV2625_CONTROL_LIB_RAM        0x88
#define DRV2625_WAVE_CLOSE             0x54
#define DRV2625_WAVE_FAR               0x36
#define DRV2625_WAVE_ERROR             0x2F
#define DRV2625_WAVE_DELAY_100MS       0x82

#define MOTOR_DEFAULT_RATED_VOLTAGE    0x46
#define MOTOR_DEFAULT_OD_CLAMP         0x5C
#define MOTOR_DEFAULT_DRIVE_TIME       0x10

#define MOTOR_BUSY_ONE_US              140000
#define MOTOR_BUSY_TWO_US              380000
#define MOTOR_BUSY_THREE_US            620000
#define MOTOR_BUSY_ERROR_US            620000
#define MOTOR_AUTO_CAL_TIMEOUT_US      700000
#define MOTOR_AUTO_CAL_POLL_US         5000

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
static u8 s_init_timeout;
static u8 s_last_chip_id;
static u8 s_last_status;
static u8 s_last_mode;
static u8 s_last_control1;
static u8 s_last_go;
static u8 s_last_diag_z_result;
static u8 s_last_acal_comp;
static u8 s_last_acal_bemf;
static u8 s_rated_voltage;
static u8 s_od_clamp;
static u8 s_drive_time;
static u8 s_continuous;
static app_motor_pattern_t s_continuous_pattern;

static void drv2625_bus_init(void)
{
    u8 div = (u8)(CLOCK_SYS_CLOCK_HZ / (4 * DRV2625_I2C_CLOCK_HZ));

    if (!div) {
        div = 1;
    }

    i2c_gpio_set(I2C_GPIO_GROUP_B6D7);
    i2c_master_init(DRV2625_I2C_ID, div);
}

static u8 drv2625_read(u8 reg)
{
    return i2c_read_byte(reg, 1);
}

static void drv2625_write(u8 reg, u8 val)
{
    i2c_write_byte(reg, 1, val);
}

static void drv2625_capture_last(void)
{
    s_last_chip_id = drv2625_read(0x00);
    s_last_status = drv2625_read(DRV2625_REG_STATUS);
    s_last_mode = drv2625_read(DRV2625_REG_MODE);
    s_last_control1 = drv2625_read(DRV2625_REG_CONTROL1);
    s_last_go = drv2625_read(DRV2625_REG_GO);
    s_last_diag_z_result = drv2625_read(DRV2625_REG_DIAG_Z_RESULT);
    s_last_acal_comp = drv2625_read(DRV2625_REG_FEEDBACK_CTRL);
    s_last_acal_bemf = drv2625_read(DRV2625_REG_ACAL_BEMF);
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

static app_status_t drv2625_wait_go_clear(u32 timeout_us)
{
    u32 start_tick = clock_time();

    while (drv2625_read(DRV2625_REG_GO) & DRV2625_GO_PLAY) {
        if (clock_time_exceed(start_tick, timeout_us)) {
            s_init_timeout = 1;
            return APP_ERR_STATE;
        }
        sleep_us(MOTOR_AUTO_CAL_POLL_US);
    }

    return APP_OK;
}

static app_status_t drv2625_init(void)
{
    s_init_attempted = 1;
    s_hw_ready = 0;
    s_init_timeout = 0;

    drv2625_bus_init();

    drv2625_write(DRV2625_REG_MODE, DRV2625_MODE_STANDBY_OUT);
    drv2625_write(DRV2625_REG_MODE, DRV2625_MODE_AUTO_CALIB);
    drv2625_write(DRV2625_REG_CONTROL1, DRV2625_CONTROL_LRA);
    drv2625_write(DRV2625_REG_RATED_VOLTAGE, s_rated_voltage);
    drv2625_write(DRV2625_REG_OD_CLAMP, s_od_clamp);
    drv2625_write(DRV2625_REG_DRIVE_TIME, s_drive_time);
    drv2625_write(DRV2625_REG_GO, 0x01);

    drv2625_wait_go_clear(MOTOR_AUTO_CAL_TIMEOUT_US);

    drv2625_capture_last();

    if (s_init_timeout) {
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
    s_init_timeout = 0;
    s_last_chip_id = 0;
    s_last_status = 0;
    s_last_mode = 0;
    s_last_control1 = 0;
    s_last_go = 0;
    s_last_diag_z_result = 0;
    s_last_acal_comp = 0;
    s_last_acal_bemf = 0;
    s_rated_voltage = MOTOR_DEFAULT_RATED_VOLTAGE;
    s_od_clamp = MOTOR_DEFAULT_OD_CLAMP;
    s_drive_time = MOTOR_DEFAULT_DRIVE_TIME;
    s_continuous = 0;
    s_continuous_pattern = MOTOR_PATTERN_NONE;
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

app_status_t app_motor_set_drive_params(u8 rated_voltage, u8 od_clamp, u8 drive_time)
{
    if (!rated_voltage || !od_clamp || !drive_time) {
        return APP_ERR_PARAM;
    }
    if (rated_voltage > MOTOR_DEFAULT_RATED_VOLTAGE ||
        od_clamp > MOTOR_DEFAULT_OD_CLAMP ||
        drive_time > MOTOR_DEFAULT_DRIVE_TIME) {
        return APP_ERR_PARAM;
    }

    app_motor_stop();
    s_rated_voltage = rated_voltage;
    s_od_clamp = od_clamp;
    s_drive_time = drive_time;
    s_hw_ready = 0;
    s_init_attempted = 0;
    s_init_timeout = 0;
    return APP_OK;
}

app_status_t app_motor_set_default_drive_params(void)
{
    return app_motor_set_drive_params(MOTOR_DEFAULT_RATED_VOLTAGE,
                                      MOTOR_DEFAULT_OD_CLAMP,
                                      MOTOR_DEFAULT_DRIVE_TIME);
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

app_status_t app_motor_play_continuous(app_motor_pattern_t pattern)
{
    app_status_t st;

    if (pattern == MOTOR_PATTERN_NONE ||
        pattern == MOTOR_PATTERN_CUSTOM ||
        pattern == MOTOR_PATTERN_CONTINUOUS) {
        return APP_ERR_PARAM;
    }

    st = app_motor_set_default_drive_params();
    if (st != APP_OK) {
        return st;
    }

    st = app_motor_play(pattern);
    if (st == APP_OK && s_busy) {
        s_continuous = 1;
        s_continuous_pattern = pattern;
    }
    return st;
}

app_status_t app_motor_stop(void)
{
    s_continuous = 0;
    s_continuous_pattern = MOTOR_PATTERN_NONE;
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
        if (s_hw_ready) {
            drv2625_write(DRV2625_REG_GO, 0x00);
        }
        s_busy = 0;
        if (s_continuous) {
            app_status_t st = app_motor_play(s_continuous_pattern);
            if (st != APP_OK) {
                s_continuous = 0;
                s_continuous_pattern = MOTOR_PATTERN_NONE;
            }
        }
    }
}

u8 app_motor_is_busy(void)
{
    return s_busy;
}

void app_motor_get_debug(app_motor_debug_t *debug)
{
    if (!debug) {
        return;
    }

    debug->hw_ready = s_hw_ready;
    debug->init_attempted = s_init_attempted;
    debug->busy = s_busy;
    debug->init_timeout = s_init_timeout;
    debug->continuous = s_continuous;
    debug->last_chip_id = s_last_chip_id;
    debug->last_status = s_last_status;
    debug->last_mode = s_last_mode;
    debug->last_control1 = s_last_control1;
    debug->last_go = s_last_go;
    debug->last_diag_z_result = s_last_diag_z_result;
    debug->last_acal_comp = s_last_acal_comp;
    debug->last_acal_bemf = s_last_acal_bemf;

    if (s_init_attempted) {
        drv2625_bus_init();
        debug->live_chip_id = drv2625_read(0x00);
        debug->live_status = drv2625_read(DRV2625_REG_STATUS);
        debug->live_mode = drv2625_read(DRV2625_REG_MODE);
        debug->live_control1 = drv2625_read(DRV2625_REG_CONTROL1);
        debug->live_go = drv2625_read(DRV2625_REG_GO);
        debug->live_diag_z_result = drv2625_read(DRV2625_REG_DIAG_Z_RESULT);
        debug->live_lra_period_hi = drv2625_read(DRV2625_REG_LRA_PERIOD_H);
        debug->live_lra_period_lo = drv2625_read(DRV2625_REG_LRA_PERIOD_L);
        debug->live_rated_voltage = drv2625_read(DRV2625_REG_RATED_VOLTAGE);
        debug->live_od_clamp = drv2625_read(DRV2625_REG_OD_CLAMP);
        debug->live_acal_comp = drv2625_read(DRV2625_REG_FEEDBACK_CTRL);
        debug->live_acal_bemf = drv2625_read(DRV2625_REG_ACAL_BEMF);
        debug->live_fb_ctrl = drv2625_read(DRV2625_REG_FEEDBACK_CTRL);
        debug->live_rated_clamp = drv2625_read(DRV2625_REG_RATED_CLAMP);
        debug->live_drive_time = drv2625_read(DRV2625_REG_DRIVE_TIME);
        debug->live_auto_cal_time = drv2625_read(DRV2625_REG_AUTO_CAL_TIME);
        debug->live_ctrl3 = drv2625_read(DRV2625_REG_CONTROL3);
        debug->live_ol_lra_period_hi = drv2625_read(DRV2625_REG_OL_LRA_PERIOD_H);
        debug->live_ol_lra_period_lo = drv2625_read(DRV2625_REG_OL_LRA_PERIOD_L);
    } else {
        debug->live_chip_id = 0;
        debug->live_status = 0;
        debug->live_mode = 0;
        debug->live_control1 = 0;
        debug->live_go = 0;
        debug->live_diag_z_result = 0;
        debug->live_lra_period_hi = 0;
        debug->live_lra_period_lo = 0;
        debug->live_rated_voltage = 0;
        debug->live_od_clamp = 0;
        debug->live_acal_comp = 0;
        debug->live_acal_bemf = 0;
        debug->live_fb_ctrl = 0;
        debug->live_rated_clamp = 0;
        debug->live_drive_time = 0;
        debug->live_auto_cal_time = 0;
        debug->live_ctrl3 = 0;
        debug->live_ol_lra_period_hi = 0;
        debug->live_ol_lra_period_lo = 0;
    }
}
