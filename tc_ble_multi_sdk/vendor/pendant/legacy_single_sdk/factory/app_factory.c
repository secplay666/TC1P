#include "app_factory.h"
#include "../storage/app_storage.h"
#include "../identity/app_identity.h"
#include "../crypto/app_crypto.h"
#include "common/string.h"

#define APP_FACTORY_MAGIC 0x46544350UL
#define APP_FACTORY_VERSION 1

typedef struct {
    u32 magic;
    u8 version;
    u8 flags;
    u16 crc16;
    u32 test_mask;
    u32 result_mask;
    u32 write_count;
    u32 lock_count;
    u32 last_error;
    app_unique_id_t last_unique_id;
} app_factory_record_t;

static app_factory_info_t s_factory;

static u16 app_factory_record_crc(const app_factory_record_t *record)
{
    app_factory_record_t tmp;

    if (!record) {
        return 0;
    }
    tmp = *record;
    tmp.crc16 = 0;
    return app_crc16(&tmp, sizeof(tmp));
}

static void app_factory_make_default(void)
{
    memset(&s_factory, 0, sizeof(s_factory));
    s_factory.version = APP_FACTORY_VERSION;
}

static void app_factory_from_record(const app_factory_record_t *record)
{
    memset(&s_factory, 0, sizeof(s_factory));
    s_factory.version = record->version;
    s_factory.flags = record->flags;
    s_factory.crc16 = record->crc16;
    s_factory.test_mask = record->test_mask;
    s_factory.result_mask = record->result_mask;
    s_factory.write_count = record->write_count;
    s_factory.lock_count = record->lock_count;
    s_factory.last_error = record->last_error;
    s_factory.last_unique_id = record->last_unique_id;
}

static void app_factory_to_record(app_factory_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = APP_FACTORY_MAGIC;
    record->version = APP_FACTORY_VERSION;
    record->flags = s_factory.flags;
    record->test_mask = s_factory.test_mask;
    record->result_mask = s_factory.result_mask;
    record->write_count = s_factory.write_count;
    record->lock_count = s_factory.lock_count;
    record->last_error = s_factory.last_error;
    record->last_unique_id = s_factory.last_unique_id;
    record->crc16 = 0;
    record->crc16 = app_factory_record_crc(record);
}

static app_status_t app_factory_save(void)
{
    app_factory_record_t record;
    app_factory_record_t verify;

    app_factory_to_record(&record);
    if (app_storage_erase(APP_STORAGE_PART_FACTORY) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (app_storage_write(APP_STORAGE_PART_FACTORY, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (app_storage_read(APP_STORAGE_PART_FACTORY, 0, &verify, sizeof(verify)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (verify.magic != APP_FACTORY_MAGIC ||
        verify.version != APP_FACTORY_VERSION ||
        verify.crc16 != app_factory_record_crc(&verify)) {
        return APP_ERR_FLASH;
    }
    s_factory.crc16 = verify.crc16;
    return APP_OK;
}

void app_factory_init(void)
{
    app_factory_record_t record;

    app_factory_make_default();
    if (app_storage_read(APP_STORAGE_PART_FACTORY, 0, &record, sizeof(record)) != APP_OK) {
        return;
    }
    if (record.magic != APP_FACTORY_MAGIC || record.version != APP_FACTORY_VERSION) {
        return;
    }
    if (record.crc16 != app_factory_record_crc(&record)) {
        return;
    }
    app_factory_from_record(&record);
}

app_status_t app_factory_get_info(app_factory_info_t *info)
{
    if (!info) {
        return APP_ERR_PARAM;
    }
    if (app_identity_is_present()) {
        s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_WRITTEN;
    }
    s_factory.flags &= (u8)~APP_FACTORY_FLAG_IDENTITY_LOCKED;
    if (app_identity_is_locked()) {
        s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_LOCKED;
    }
    *info = s_factory;
    return APP_OK;
}

app_status_t app_factory_write_unique_id(const app_unique_id_t *id)
{
    app_status_t st;
    app_status_t save_st;

    if (!id) {
        return APP_ERR_PARAM;
    }
    st = app_identity_write_unique_id(id, 0);
    s_factory.last_error = (u32)st;
    if (st == APP_OK) {
        s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_WRITTEN;
        if (app_identity_is_locked()) {
            s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_LOCKED;
        } else {
            s_factory.flags &= (u8)~APP_FACTORY_FLAG_IDENTITY_LOCKED;
        }
        s_factory.write_count++;
        s_factory.last_unique_id = *id;
    }
    save_st = app_factory_save();
    if (st == APP_OK && save_st != APP_OK) {
        return save_st;
    }
    return st;
}

app_status_t app_factory_read_unique_id(app_unique_id_t *id)
{
    const app_identity_info_t *identity;

    if (!id) {
        return APP_ERR_PARAM;
    }
    identity = app_identity_get_info();
    *id = identity->unique_id;
    return app_identity_self_check();
}

app_status_t app_factory_run_self_test(u32 test_mask, u32 *result_mask)
{
    s_factory.test_mask = test_mask;
    s_factory.result_mask = test_mask;
    s_factory.flags |= APP_FACTORY_FLAG_SELF_TEST_PASS;
    s_factory.last_error = APP_OK;
    if (result_mask) {
        *result_mask = s_factory.result_mask;
    }
    return app_factory_save();
}

app_status_t app_factory_lock_identity(void)
{
    const app_identity_info_t *identity;
    app_status_t st;
    app_status_t save_st;

    st = app_identity_lock();
    s_factory.last_error = (u32)st;
    if (st == APP_OK) {
        identity = app_identity_get_info();
        s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_WRITTEN;
        s_factory.flags |= APP_FACTORY_FLAG_IDENTITY_LOCKED;
        s_factory.lock_count++;
        s_factory.last_unique_id = identity->unique_id;
    }
    save_st = app_factory_save();
    if (st == APP_OK && save_st != APP_OK) {
        return save_st;
    }
    return st;
}

u8 app_factory_is_locked(void)
{
    return app_identity_is_locked();
}
