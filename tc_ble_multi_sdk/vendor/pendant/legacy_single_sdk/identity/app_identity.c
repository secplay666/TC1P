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

static u32 rd32_le(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr32_le(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static app_status_t app_identity_read_record(app_identity_record_t *record)
{
    u16 crc;
    app_status_t st;

    if (!record) {
        return APP_ERR_PARAM;
    }

    st = app_storage_read(APP_STORAGE_PART_IDENTITY, 0, record, sizeof(*record));
    if (st != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (record->magic != APP_IDENTITY_MAGIC) {
        return APP_ERR_NOT_FOUND;
    }
    if (record->version != APP_IDENTITY_VERSION) {
        return APP_ERR_UNSUPPORTED;
    }
    crc = app_crc16(&record->unique_id, sizeof(record->unique_id));
    if (crc != record->crc16) {
        return APP_ERR_CRC;
    }
    st = app_identity_validate_unique_id(&record->unique_id);
    if (st != APP_OK) {
        return st;
    }
    return APP_OK;
}

static void app_identity_set_loaded(const app_unique_id_t *id, u8 flags, u16 crc16)
{
    if (id) {
        s_identity.unique_id = *id;
    }
    s_identity.flags = flags;
    s_identity.crc16 = crc16;
    s_identity.privacy_mode = app_config_get()->privacy_mode;
    app_identity_update_eid(0);
}

void app_identity_init(void)
{
    memset(&s_identity, 0, sizeof(s_identity));
    s_identity.key_id = 1;
}

app_status_t app_identity_load(void)
{
    app_identity_record_t record;
    app_status_t st;

    st = app_identity_read_record(&record);
    if (st == APP_OK) {
        app_identity_set_loaded(
            &record.unique_id,
            APP_IDENTITY_FLAG_PRESENT | APP_IDENTITY_FLAG_VALID |
                (record.locked ? APP_IDENTITY_FLAG_LOCKED : 0),
            record.crc16);
        return APP_OK;
    }

    if (st == APP_ERR_NOT_FOUND || st == APP_ERR_UNSUPPORTED) {
#if APP_IDENTITY_ALLOW_DEV_FALLBACK
        u32 tick = clock_time();
        app_unique_id_t dev_id;
        memset(&dev_id, 0, sizeof(dev_id));
        dev_id.bytes[0] = 'P';
        dev_id.bytes[1] = 'D';
        dev_id.bytes[2] = 'E';
        dev_id.bytes[3] = 'V';
        dev_id.bytes[4] = (u8)tick;
        dev_id.bytes[5] = (u8)(tick >> 8);
        dev_id.bytes[6] = (u8)(tick >> 16);
        dev_id.bytes[7] = (u8)(tick >> 24);
        app_identity_set_loaded(
            &dev_id,
            APP_IDENTITY_FLAG_DEV_FALLBACK | APP_IDENTITY_FLAG_VALID,
            app_crc16(&dev_id, sizeof(dev_id)));
        return APP_OK;
#else
        return st;
#endif
    }

    return st;
}

app_status_t app_identity_validate_unique_id(const app_unique_id_t *id)
{
    u8 i;
    u8 all_zero = 1;
    u8 all_ff = 1;
    u32 product_sn;
    u32 terminal_sn;
    u32 reserved;

    if (!id) {
        return APP_ERR_PARAM;
    }

    for (i = 0; i < APP_UNIQUE_ID_LEN; i++) {
        if (id->bytes[i] != 0x00) {
            all_zero = 0;
        }
        if (id->bytes[i] != 0xff) {
            all_ff = 0;
        }
    }
    if (all_zero || all_ff) {
        return APP_ERR_STATE;
    }

    product_sn = rd32_le(&id->bytes[0]);
    terminal_sn = rd32_le(&id->bytes[4]);
    reserved = rd32_le(&id->bytes[12]);
    if (!product_sn || product_sn == 0xffffffff ||
        !terminal_sn || terminal_sn == 0xffffffff ||
        reserved != 0) {
        return APP_ERR_PARAM;
    }

    return APP_OK;
}

app_status_t app_identity_self_check(void)
{
    app_status_t st = app_identity_validate_unique_id(&s_identity.unique_id);
    if (st == APP_OK) {
        s_identity.flags |= APP_IDENTITY_FLAG_VALID;
    } else {
        s_identity.flags &= (u8)~APP_IDENTITY_FLAG_VALID;
    }
    return st;
}

void app_identity_build_unique_id(u32 product_sn, u32 terminal_sn, u32 random_value, app_unique_id_t *id)
{
    if (!id) {
        return;
    }
    memset(id, 0, sizeof(*id));
    wr32_le(&id->bytes[0], product_sn);
    wr32_le(&id->bytes[4], terminal_sn);
    wr32_le(&id->bytes[8], random_value);
    wr32_le(&id->bytes[12], 0);
}

app_status_t app_identity_write_unique_id(const app_unique_id_t *id, u8 lock_after_write)
{
    app_identity_record_t old_record;
    app_identity_record_t record;
    app_status_t st;

    if (!id) {
        return APP_ERR_PARAM;
    }
    st = app_identity_validate_unique_id(id);
    if (st != APP_OK) {
        return st;
    }

    memset(&old_record, 0, sizeof(old_record));
    if (app_storage_read(APP_STORAGE_PART_IDENTITY, 0, &old_record, sizeof(old_record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (old_record.magic == APP_IDENTITY_MAGIC &&
        old_record.version == APP_IDENTITY_VERSION &&
        old_record.locked) {
        return APP_ERR_PERMISSION;
    }

    memset(&record, 0, sizeof(record));
    record.magic = APP_IDENTITY_MAGIC;
    record.version = APP_IDENTITY_VERSION;
    record.locked = lock_after_write ? 1 : 0;
    record.unique_id = *id;
    record.crc16 = app_crc16(&record.unique_id, sizeof(record.unique_id));

    if (app_storage_erase(APP_STORAGE_PART_IDENTITY) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (app_storage_write(APP_STORAGE_PART_IDENTITY, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }

    st = app_identity_load();
    if (st != APP_OK) {
        return st;
    }
    if (memcmp(s_identity.unique_id.bytes, id->bytes, APP_UNIQUE_ID_LEN) != 0) {
        return APP_ERR_FLASH;
    }
    return APP_OK;
}

app_status_t app_identity_lock(void)
{
    app_identity_record_t record;
    app_status_t st;

    st = app_identity_read_record(&record);
    if (st != APP_OK) {
        return st;
    }
    if (record.locked) {
        s_identity.flags |= APP_IDENTITY_FLAG_LOCKED;
        return APP_OK;
    }
    return app_identity_write_unique_id(&record.unique_id, 1);
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

u8 app_identity_is_locked(void)
{
    return (s_identity.flags & APP_IDENTITY_FLAG_LOCKED) ? 1 : 0;
}

u8 app_identity_is_present(void)
{
    return (s_identity.flags & APP_IDENTITY_FLAG_PRESENT) ? 1 : 0;
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
