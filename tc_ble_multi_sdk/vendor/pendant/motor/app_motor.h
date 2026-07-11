#ifndef APP_MOTOR_H_
#define APP_MOTOR_H_

#include "../common/app_types.h"

typedef enum {
    MOTOR_PATTERN_NONE = 0,
    MOTOR_PATTERN_ONE,
    MOTOR_PATTERN_TWO,
    MOTOR_PATTERN_THREE,
    MOTOR_PATTERN_ERROR,
    MOTOR_PATTERN_CUSTOM,
} app_motor_pattern_t;

void app_motor_init(void);
app_status_t app_motor_self_check(void);
app_status_t app_motor_play(app_motor_pattern_t pattern);
app_status_t app_motor_stop(void);
void app_motor_poll(u32 now_tick);
u8 app_motor_is_busy(void);

typedef struct {
    u8 hw_ready;
    u8 init_attempted;
    u8 busy;
    u8 init_timeout;
    u8 last_chip_id;
    u8 last_status;
    u8 last_mode;
    u8 last_control1;
    u8 last_go;
    u8 last_diag_z_result;
    u8 last_acal_comp;
    u8 last_acal_bemf;
    u8 live_chip_id;
    u8 live_status;
    u8 live_mode;
    u8 live_control1;
    u8 live_go;
    u8 live_diag_z_result;
    u8 live_lra_period_hi;
    u8 live_lra_period_lo;
    u8 live_rated_voltage;
    u8 live_od_clamp;
    u8 live_acal_comp;
    u8 live_acal_bemf;
    u8 live_fb_ctrl;
    u8 live_rated_clamp;
    u8 live_drive_time;
    u8 live_auto_cal_time;
    u8 live_ctrl3;
    u8 live_ol_lra_period_hi;
    u8 live_ol_lra_period_lo;
} app_motor_debug_t;

void app_motor_get_debug(app_motor_debug_t *debug);

#endif
