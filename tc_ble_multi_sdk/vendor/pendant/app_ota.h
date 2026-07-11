#ifndef APP_OTA_H_
#define APP_OTA_H_

#include "app_config.h"
#include "common/app_types.h"

void app_ota_configure_boot(void);
void app_ota_guard_boot_check(void);
void app_ota_init(void);
void app_ota_poll(void);
void app_ota_on_disconnected(void);
u8 app_ota_is_active(void);
u8 app_ota_last_result(void);

typedef struct {
    u8 valid;
    u8 state;
    u8 active_slot;
    u8 previous_slot;
    u8 pending_slot;
    u8 boot_attempts;
    u8 backup_valid;
    u8 last_error;
    u32 confirmed_version;
    u32 pending_version;
    u32 boot_count;
    u32 last_rollback_reason;
    u32 meta_addr;
    u32 backup_addr;
} app_ota_guard_info_t;

void app_ota_guard_get_info(app_ota_guard_info_t *info);

#endif
