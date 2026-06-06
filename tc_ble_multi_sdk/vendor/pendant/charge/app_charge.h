#ifndef APP_CHARGE_H_
#define APP_CHARGE_H_

#include "../common/app_types.h"

typedef enum {
    CHARGE_STATE_UNKNOWN = 0,
    CHARGE_STATE_NOT_CHARGING = 1,
    CHARGE_STATE_CHARGING = 2,
    CHARGE_STATE_FULL = 3,
} app_charge_state_t;

void app_charge_init(void);
void app_charge_poll(u32 now_tick);
app_charge_state_t app_charge_get_state(void);
u8 app_charge_is_charging(void);

#endif
