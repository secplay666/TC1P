#ifndef APP_IDENTITY_H_
#define APP_IDENTITY_H_

#include "../common/app_types.h"

#define APP_IDENTITY_FLAG_PRESENT       0x01
#define APP_IDENTITY_FLAG_LOCKED        0x02
#define APP_IDENTITY_FLAG_DEV_FALLBACK  0x04
#define APP_IDENTITY_FLAG_VALID         0x08

typedef struct {
    app_unique_id_t unique_id;
    app_eid_t current_eid;
    u32 short_id;
    u16 crc16;
    u8 key_id;
    u8 privacy_mode;
    u8 flags;
} app_identity_info_t;

void app_identity_init(void);
app_status_t app_identity_load(void);
app_status_t app_identity_self_check(void);
app_status_t app_identity_update_eid(u32 epoch);
app_status_t app_identity_validate_unique_id(const app_unique_id_t *id);
void app_identity_build_unique_id(u32 product_sn, u32 terminal_sn, u32 random_value, app_unique_id_t *id);
app_status_t app_identity_write_unique_id(const app_unique_id_t *id, u8 lock_after_write);
app_status_t app_identity_lock(void);
u8 app_identity_is_locked(void);
u8 app_identity_is_present(void);
const app_identity_info_t *app_identity_get_info(void);
const app_eid_t *app_identity_get_eid(void);
u32 app_identity_get_short_id(void);
u8 app_identity_get_key_id(void);

#endif
