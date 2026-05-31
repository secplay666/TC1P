#include "app_storage.h"
#include "drivers.h"

#ifndef APP_STORAGE_BASE_ADDR
#define APP_STORAGE_BASE_ADDR 0x70000
#endif

#define APP_STORAGE_PART_SIZE 0x1000

static u32 app_storage_part_addr(app_storage_part_t part)
{
    return APP_STORAGE_BASE_ADDR + ((u32)part * APP_STORAGE_PART_SIZE);
}

void app_storage_init(void)
{
}

app_status_t app_storage_self_check(void)
{
    return APP_OK;
}

app_status_t app_storage_read(app_storage_part_t part, u32 offset, void *buf, u16 len)
{
    if (part >= APP_STORAGE_PART_COUNT || !buf || offset + len > APP_STORAGE_PART_SIZE) {
        return APP_ERR_PARAM;
    }
    flash_read_page(app_storage_part_addr(part) + offset, len, (u8 *)buf);
    return APP_OK;
}

app_status_t app_storage_write(app_storage_part_t part, u32 offset, const void *buf, u16 len)
{
    if (part >= APP_STORAGE_PART_COUNT || !buf || offset + len > APP_STORAGE_PART_SIZE) {
        return APP_ERR_PARAM;
    }
    flash_write_page(app_storage_part_addr(part) + offset, len, (u8 *)buf);
    return APP_OK;
}

app_status_t app_storage_erase(app_storage_part_t part)
{
    if (part >= APP_STORAGE_PART_COUNT) {
        return APP_ERR_PARAM;
    }
    flash_erase_sector(app_storage_part_addr(part));
    return APP_OK;
}

u32 app_storage_get_part_addr(app_storage_part_t part)
{
    if (part >= APP_STORAGE_PART_COUNT) {
        return 0;
    }
    return app_storage_part_addr(part);
}

u32 app_storage_get_part_size(app_storage_part_t part)
{
    (void)part;
    return APP_STORAGE_PART_SIZE;
}
