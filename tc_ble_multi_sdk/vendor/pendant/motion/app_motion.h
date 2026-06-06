#ifndef APP_MOTION_H_
#define APP_MOTION_H_

#include "../common/app_types.h"

typedef struct {
    u32 motion_count;
    u32 last_motion_tick;
    u8 current_level;
} app_motion_state_t;

void app_motion_init(void);
void app_motion_poll(u32 now_tick);
void app_motion_on_gpio_edge(void);
void app_motion_get_state(app_motion_state_t *state);
u32 app_motion_get_wakeup_pin(void);

#endif
