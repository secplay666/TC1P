#include "app_config_store.h"
#include "../storage/app_storage.h"
#include "../crypto/app_crypto.h"
#include "common/string.h"

#define APP_CONFIG_MAGIC 0x43464750UL
#define APP_CONFIG_VERSION 1

typedef struct {
    u32 magic;
    app_runtime_config_t config;
} app_config_record_t;

static app_runtime_config_t s_config;
static u8 s_dirty;

static void app_config_make_default(app_runtime_config_t *config)
{
    config->version = APP_CONFIG_VERSION;
    config->rssi_t1 = -80;
    config->rssi_t2 = -65;
    config->rssi_t3 = -50;
    config->tin_ms = 1500;
    config->tout_ms = 2500;
    config->idle_sleep_s = 60;
    config->app_idle_s = 180;
    config->adv_interval_ms = 200;
    config->scan_interval_ms = 50;
    config->scan_window_ms = 50;
    config->vibration_enable = 1;
    config->reliable_msg_default = 1;
    config->privacy_mode = 0;
    config->crc16 = 0;
    config->crc16 = app_crc16(config, sizeof(app_runtime_config_t) - sizeof(u16));
}

void app_config_init(void)
{
    app_config_make_default(&s_config);
    s_dirty = 0;
}

const app_runtime_config_t *app_config_get(void)
{
    return &s_config;
}

app_status_t app_config_validate(const app_runtime_config_t *config)
{
    u16 crc;
    if (!config) {
        return APP_ERR_PARAM;
    }
    if (config->version != APP_CONFIG_VERSION) {
        return APP_ERR_UNSUPPORTED;
    }
    if (!(config->rssi_t3 > config->rssi_t2 && config->rssi_t2 > config->rssi_t1)) {
        return APP_ERR_PARAM;
    }
    if (!config->tin_ms || !config->tout_ms || !config->adv_interval_ms) {
        return APP_ERR_PARAM;
    }
    crc = app_crc16(config, sizeof(app_runtime_config_t) - sizeof(u16));
    if (crc != config->crc16) {
        return APP_ERR_CRC;
    }
    return APP_OK;
}

app_status_t app_config_set(const app_runtime_config_t *config)
{
    app_runtime_config_t tmp;
    if (!config) {
        return APP_ERR_PARAM;
    }
    tmp = *config;
    tmp.crc16 = 0;
    tmp.crc16 = app_crc16(&tmp, sizeof(app_runtime_config_t) - sizeof(u16));
    if (app_config_validate(&tmp) != APP_OK) {
        return APP_ERR_PARAM;
    }
    s_config = tmp;
    s_dirty = 1;
    return APP_OK;
}

app_status_t app_config_load(void)
{
    app_config_record_t record;
    if (app_storage_read(APP_STORAGE_PART_CONFIG, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (record.magic != APP_CONFIG_MAGIC) {
        return APP_ERR_NOT_FOUND;
    }
    if (app_config_validate(&record.config) != APP_OK) {
        return APP_ERR_CRC;
    }
    s_config = record.config;
    s_dirty = 0;
    return APP_OK;
}

app_status_t app_config_save(void)
{
    app_config_record_t record;
    record.magic = APP_CONFIG_MAGIC;
    record.config = s_config;
    app_storage_erase(APP_STORAGE_PART_CONFIG);
    if (app_storage_write(APP_STORAGE_PART_CONFIG, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    s_dirty = 0;
    return APP_OK;
}

app_status_t app_config_reset_default(void)
{
    app_config_make_default(&s_config);
    s_dirty = 1;
    return APP_OK;
}

u8 app_config_is_dirty(void)
{
    return s_dirty;
}
