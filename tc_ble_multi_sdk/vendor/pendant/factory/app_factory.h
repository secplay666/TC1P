#ifndef APP_FACTORY_H_
#define APP_FACTORY_H_

#include "../common/app_types.h"

#define APP_FACTORY_FLAG_IDENTITY_WRITTEN 0x01
#define APP_FACTORY_FLAG_IDENTITY_LOCKED  0x02
#define APP_FACTORY_FLAG_SELF_TEST_PASS   0x04

typedef struct {
    app_unique_id_t last_unique_id;
    u32 test_mask;
    u32 result_mask;
    u32 write_count;
    u32 lock_count;
    u32 last_error;
    u16 crc16;
    u8 version;
    u8 flags;
} app_factory_info_t;

void app_factory_init(void);
app_status_t app_factory_get_info(app_factory_info_t *info);
app_status_t app_factory_write_unique_id(const app_unique_id_t *id);
app_status_t app_factory_read_unique_id(app_unique_id_t *id);
app_status_t app_factory_run_self_test(u32 test_mask, u32 *result_mask);
app_status_t app_factory_lock_identity(void);
u8 app_factory_is_locked(void);

#endif
