#ifndef APP_IDENTITY_H_
#define APP_IDENTITY_H_

#include "../common/app_types.h"

typedef struct {
    app_unique_id_t unique_id;
    app_eid_t current_eid;
    u32 short_id;
    u8 key_id;
    u8 privacy_mode;
} app_identity_info_t;

void app_identity_init(void);
app_status_t app_identity_load(void);
app_status_t app_identity_self_check(void);
app_status_t app_identity_update_eid(u32 epoch);
const app_identity_info_t *app_identity_get_info(void);
const app_eid_t *app_identity_get_eid(void);
u32 app_identity_get_short_id(void);
u8 app_identity_get_key_id(void);

#endif
