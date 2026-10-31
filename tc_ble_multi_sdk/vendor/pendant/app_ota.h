#ifndef APP_OTA_H_
#define APP_OTA_H_

#include "app_config.h"
#include "common/app_types.h"

void app_ota_configure_boot(void);
void app_ota_guard_boot_check(void);
void app_ota_init(void);
void app_ota_poll(void);
u8 app_ota_is_active(void);
u8 app_ota_last_result(void);
void app_ota_debug_note_write(u16 l2cap_len, const u8 *value, u16 value_len);
u8 app_ota_diag_should_bypass_write(const u8 *value, u16 value_len);
void app_ota_diag_note_after_write(const u8 *value, u16 value_len, int result);

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
    u8 slot0_mark;
    u8 slot1_mark;
    u8 boot_config_status;
    u32 configured_boot_addr;
    u32 program_boot_addr;
    u32 program_offset;
    u32 firmware_max_size;
    u8 debug_stage;
    u16 debug_write_count;
    u16 debug_last_index;
    u16 debug_boot_wakeup_src;
} app_ota_guard_info_t;

void app_ota_guard_get_info(app_ota_guard_info_t *info);

#endif
