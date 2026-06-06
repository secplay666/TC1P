#ifndef APP_BATTERY_H_
#define APP_BATTERY_H_

#include "../common/app_types.h"

typedef struct {
    u16 voltage_mv;
    u8 percent;
    u8 low;
    u8 critical;
} app_battery_state_t;

void app_battery_init(void);
app_status_t app_battery_sample(void);
void app_battery_poll(u32 now_tick);
void app_battery_get_state(app_battery_state_t *state);

#endif
