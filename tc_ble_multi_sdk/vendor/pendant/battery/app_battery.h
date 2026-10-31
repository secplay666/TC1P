#ifndef APP_BATTERY_H_
#define APP_BATTERY_H_

#include "../common/app_types.h"

typedef struct {
    u16 voltage_mv;   /* averaged battery voltage */
    u16 raw_mv;       /* latest single sample */
    u16 min_mv;       /* lowest valid sample since boot, 0xffff if none */
    u8 percent;
    u8 low;
    u8 critical;
    u8 valid;         /* latest sample within the plausible range */
} app_battery_state_t;

void app_battery_init(void);
app_status_t app_battery_sample(void);
void app_battery_poll(u32 now_tick);
void app_battery_get_state(app_battery_state_t *state);

#endif
