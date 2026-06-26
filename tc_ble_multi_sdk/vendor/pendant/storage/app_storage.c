#include "app_storage.h"
#include "drivers.h"
#include "vendor/common/ble_flash.h"
#include "common/string.h"

#define APP_STORAGE_SECTOR_SIZE 0x1000
#define APP_STORAGE_PART_SIZE   APP_STORAGE_SECTOR_SIZE

static app_storage_partition_t s_partitions[APP_STORAGE_PART_COUNT];
static app_storage_flash_info_t s_flash_info;
static u8 s_initialized;

static const char *const s_part_names[APP_STORAGE_PART_COUNT] = {
    "identity",
    "config",
    "bond",
    "profile",
    "factory",
};

static u32 app_storage_flash_size_bytes(void)
{
    switch (blc_flash_capacity) {
    case FLASH_SIZE_512K:
        return 0x80000;
    case FLASH_SIZE_1M:
        return 0x100000;
    case FLASH_SIZE_2M:
        return 0x200000;
    default:
        return 0;
    }
}

static u32 app_storage_min_nonzero(u32 a, u32 b)
{
    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }
    return a < b ? a : b;
}

static u32 app_storage_calc_reserved_start(void)
{
    u32 start = 0;
    start = app_storage_min_nonzero(start, flash_sector_smp_storage);
    start = app_storage_min_nonzero(start, flash_sector_mac_address);
    start = app_storage_min_nonzero(start, flash_sector_calibration);
    start = app_storage_min_nonzero(start, flash_sector_simple_sdp_att);
    return start;
}

static u32 app_storage_part_addr(app_storage_part_t part)
{
    if (part >= APP_STORAGE_PART_COUNT) {
        return 0;
    }
    return s_partitions[part].addr;
}

static app_status_t app_storage_check_range(app_storage_part_t part, u32 offset, u16 len)
{
    if (!s_initialized) {
        app_storage_init();
    }
    if (part >= APP_STORAGE_PART_COUNT ||
        offset > s_partitions[part].size ||
        (u32)len > s_partitions[part].size - offset) {
        return APP_ERR_PARAM;
    }
    return APP_OK;
}

void app_storage_init(void)
{
    u32 app_base;
    u8 i;

    memset(&s_flash_info, 0, sizeof(s_flash_info));
    memset(s_partitions, 0, sizeof(s_partitions));

    blc_readFlashSize_autoConfigCustomFlashSector();

    s_flash_info.flash_mid = blc_flash_mid;
    s_flash_info.flash_vendor = blc_flash_vendor;
    s_flash_info.flash_size = app_storage_flash_size_bytes();
    s_flash_info.sdk_reserved_start = app_storage_calc_reserved_start();
    s_flash_info.sdk_mac_addr = flash_sector_mac_address;
    s_flash_info.sdk_calibration_addr = flash_sector_calibration;
    s_flash_info.sdk_smp_pairing_addr = flash_sector_smp_storage;
    s_flash_info.sdk_master_pairing_addr = flash_sector_simple_sdp_att;
    s_flash_info.app_total_size = APP_STORAGE_PART_COUNT * APP_STORAGE_PART_SIZE;

    if (s_flash_info.sdk_reserved_start >= s_flash_info.app_total_size) {
        app_base = s_flash_info.sdk_reserved_start - s_flash_info.app_total_size;
    } else {
        app_base = 0;
    }
    app_base &= ~(APP_STORAGE_SECTOR_SIZE - 1);
    s_flash_info.app_base_addr = app_base;

    for (i = 0; i < APP_STORAGE_PART_COUNT; i++) {
        s_partitions[i].part = (app_storage_part_t)i;
        s_partitions[i].name = s_part_names[i];
        s_partitions[i].addr = app_base + ((u32)i * APP_STORAGE_PART_SIZE);
        s_partitions[i].size = APP_STORAGE_PART_SIZE;
    }

    s_initialized = 1;
}

app_status_t app_storage_self_check(void)
{
    u8 i;

    if (!s_initialized) {
        app_storage_init();
    }
    if (!s_flash_info.flash_size || !s_flash_info.sdk_reserved_start || !s_flash_info.app_base_addr) {
        return APP_ERR_STATE;
    }
    if (s_flash_info.app_base_addr + s_flash_info.app_total_size > s_flash_info.sdk_reserved_start) {
        return APP_ERR_STATE;
    }
    if (s_flash_info.sdk_reserved_start > s_flash_info.flash_size) {
        return APP_ERR_STATE;
    }

    for (i = 0; i < APP_STORAGE_PART_COUNT; i++) {
        u8 probe;
        if ((s_partitions[i].addr & (APP_STORAGE_SECTOR_SIZE - 1)) ||
            s_partitions[i].size != APP_STORAGE_PART_SIZE ||
            s_partitions[i].addr + s_partitions[i].size > s_flash_info.sdk_reserved_start) {
            return APP_ERR_STATE;
        }
        flash_read_page(s_partitions[i].addr, 1, &probe);
    }

    return APP_OK;
}

app_status_t app_storage_read(app_storage_part_t part, u32 offset, void *buf, u16 len)
{
    if (!buf) {
        return APP_ERR_PARAM;
    }
    if (app_storage_check_range(part, offset, len) != APP_OK) {
        return APP_ERR_PARAM;
    }
    flash_read_page(app_storage_part_addr(part) + offset, len, (u8 *)buf);
    return APP_OK;
}

app_status_t app_storage_write(app_storage_part_t part, u32 offset, const void *buf, u16 len)
{
    if (!buf) {
        return APP_ERR_PARAM;
    }
    if (app_storage_check_range(part, offset, len) != APP_OK) {
        return APP_ERR_PARAM;
    }
    flash_write_page(app_storage_part_addr(part) + offset, len, (u8 *)buf);
    return APP_OK;
}

app_status_t app_storage_erase(app_storage_part_t part)
{
    if (app_storage_check_range(part, 0, APP_STORAGE_PART_SIZE) != APP_OK) {
        return APP_ERR_PARAM;
    }
    flash_erase_sector(app_storage_part_addr(part));
    return APP_OK;
}

u32 app_storage_get_part_addr(app_storage_part_t part)
{
    if (!s_initialized) {
        app_storage_init();
    }
    if (part >= APP_STORAGE_PART_COUNT) {
        return 0;
    }
    return app_storage_part_addr(part);
}

u32 app_storage_get_part_size(app_storage_part_t part)
{
    if (!s_initialized) {
        app_storage_init();
    }
    if (part >= APP_STORAGE_PART_COUNT) {
        return 0;
    }
    return s_partitions[part].size;
}

const app_storage_partition_t *app_storage_get_partition(app_storage_part_t part)
{
    if (!s_initialized) {
        app_storage_init();
    }
    if (part >= APP_STORAGE_PART_COUNT) {
        return 0;
    }
    return &s_partitions[part];
}

void app_storage_get_flash_info(app_storage_flash_info_t *info)
{
    if (!s_initialized) {
        app_storage_init();
    }
    if (info) {
        *info = s_flash_info;
    }
}
