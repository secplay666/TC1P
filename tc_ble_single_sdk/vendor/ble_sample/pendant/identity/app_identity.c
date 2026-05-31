#include "app_identity.h"
#include "../storage/app_storage.h"
#include "../crypto/app_crypto.h"
#include "../config/app_config_store.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define APP_IDENTITY_MAGIC 0x49445050UL
#define APP_IDENTITY_VERSION 1
#ifndef APP_IDENTITY_ALLOW_DEV_FALLBACK
#define APP_IDENTITY_ALLOW_DEV_FALLBACK 1
#endif

typedef struct {
    u32 magic;
    u8 version;
    u8 locked;
    u16 crc16;
    app_unique_id_t unique_id;
} app_identity_record_t;

static app_identity_info_t s_identity;
static const u8 s_dev_key[16] = {
    0x50, 0x45, 0x4e, 0x44, 0x41, 0x4e, 0x54, 0x2d,
    0x44, 0x45, 0x56, 0x2d, 0x4b, 0x45, 0x59, 0x31,
};

void app_identity_init(void)
{
    memset(&s_identity, 0, sizeof(s_identity));
    s_identity.key_id = 1;
}

app_status_t app_identity_load(void)
{
    app_identity_record_t record;
    u16 crc;

    if (app_storage_read(APP_STORAGE_PART_IDENTITY, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (record.magic != APP_IDENTITY_MAGIC || record.version != APP_IDENTITY_VERSION) {
#if APP_IDENTITY_ALLOW_DEV_FALLBACK
        u32 tick = clock_time();
        memset(&s_identity.unique_id, 0, sizeof(s_identity.unique_id));
        s_identity.unique_id.bytes[0] = 'P';
        s_identity.unique_id.bytes[1] = 'D';
        s_identity.unique_id.bytes[2] = 'E';
        s_identity.unique_id.bytes[3] = 'V';
        s_identity.unique_id.bytes[4] = (u8)tick;
        s_identity.unique_id.bytes[5] = (u8)(tick >> 8);
        s_identity.unique_id.bytes[6] = (u8)(tick >> 16);
        s_identity.unique_id.bytes[7] = (u8)(tick >> 24);
        s_identity.privacy_mode = app_config_get()->privacy_mode;
        return app_identity_update_eid(0);
#else
        return APP_ERR_NOT_FOUND;
#endif
    }
    crc = app_crc16(&record.unique_id, sizeof(record.unique_id));
    if (crc != record.crc16) {
        return APP_ERR_CRC;
    }

    s_identity.unique_id = record.unique_id;
    s_identity.privacy_mode = app_config_get()->privacy_mode;
    return app_identity_update_eid(0);
}

app_status_t app_identity_self_check(void)
{
    u8 i;
    u8 all_zero = 1;
    u8 all_ff = 1;
    for (i = 0; i < APP_UNIQUE_ID_LEN; i++) {
        if (s_identity.unique_id.bytes[i] != 0x00) {
            all_zero = 0;
        }
        if (s_identity.unique_id.bytes[i] != 0xff) {
            all_ff = 0;
        }
    }
    return (all_zero || all_ff) ? APP_ERR_STATE : APP_OK;
}

app_status_t app_identity_update_eid(u32 epoch)
{
    u8 block[16];
    u8 i;

    for (i = 0; i < 16; i++) {
        block[i] = s_identity.unique_id.bytes[i];
    }
    block[0] ^= (u8)epoch;
    block[1] ^= (u8)(epoch >> 8);
    block[2] ^= (u8)(epoch >> 16);
    block[3] ^= (u8)(epoch >> 24);

    if (app_crypto_aes128_encrypt(s_dev_key, block, s_identity.current_eid.bytes) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }
    s_identity.short_id = ((u32)s_identity.current_eid.bytes[0]) |
                          ((u32)s_identity.current_eid.bytes[1] << 8) |
                          ((u32)s_identity.current_eid.bytes[2] << 16) |
                          ((u32)s_identity.current_eid.bytes[3] << 24);
    return APP_OK;
}

const app_identity_info_t *app_identity_get_info(void)
{
    return &s_identity;
}

const app_eid_t *app_identity_get_eid(void)
{
    return &s_identity.current_eid;
}

u32 app_identity_get_short_id(void)
{
    return s_identity.short_id;
}

u8 app_identity_get_key_id(void)
{
    return s_identity.key_id;
}
