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

#endif
