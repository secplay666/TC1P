#ifndef APP_STORAGE_H_
#define APP_STORAGE_H_

#include "../common/app_types.h"

typedef enum {
    APP_STORAGE_PART_IDENTITY = 0,
    APP_STORAGE_PART_CONFIG,
    APP_STORAGE_PART_BOND,
    APP_STORAGE_PART_EVENT_LOG,
    APP_STORAGE_PART_FACTORY,
    APP_STORAGE_PART_COUNT,
} app_storage_part_t;

typedef struct {
    app_storage_part_t part;
    const char *name;
    u32 addr;
    u32 size;
} app_storage_partition_t;

typedef struct {
    u32 flash_mid;
    u32 flash_vendor;
    u32 flash_size;
    u32 sdk_reserved_start;
    u32 sdk_mac_addr;
    u32 sdk_calibration_addr;
    u32 sdk_smp_pairing_addr;
    u32 sdk_master_pairing_addr;
    u32 app_base_addr;
    u32 app_total_size;
} app_storage_flash_info_t;

void app_storage_init(void);
app_status_t app_storage_self_check(void);
app_status_t app_storage_read(app_storage_part_t part, u32 offset, void *buf, u16 len);
app_status_t app_storage_write(app_storage_part_t part, u32 offset, const void *buf, u16 len);
app_status_t app_storage_erase(app_storage_part_t part);
u32 app_storage_get_part_addr(app_storage_part_t part);
u32 app_storage_get_part_size(app_storage_part_t part);
const app_storage_partition_t *app_storage_get_partition(app_storage_part_t part);
void app_storage_get_flash_info(app_storage_flash_info_t *info);

#endif
