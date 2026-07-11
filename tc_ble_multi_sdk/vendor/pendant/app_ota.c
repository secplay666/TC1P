#include "app_ota.h"
#include "ble/app_ble.h"
#include "system/app_system.h"
#include "storage/app_storage.h"
#include "crypto/app_crypto.h"
#include "build_info/app_build_info.h"
#include "stack/ble/ble.h"
#include "stack/ble/host/l2cap/l2cap.h"
#include "drivers/B85/driver_ext/mcu_boot.h"
#include "stack/ble/service/ota/ota_server.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define APP_OTA_SLOT0_BASE                      0x00000UL
#define APP_OTA_SLOT1_BASE                      MULTI_BOOT_ADDR_0x40000
#define APP_OTA_SECTOR_SIZE                     0x1000UL
#define APP_OTA_GUARD_MAGIC                     0x4741544FUL /* OTAG */
#define APP_OTA_GUARD_VERSION                   1
#define APP_OTA_GUARD_STATE_CONFIRMED           1
#define APP_OTA_GUARD_STATE_UPDATE_PREPARED     2
#define APP_OTA_GUARD_STATE_PENDING             3
#define APP_OTA_GUARD_STATE_ROLLED_BACK         4
#define APP_OTA_SLOT_INVALID                    0xff
#define APP_OTA_ROLLBACK_REASON_BOOT_ATTEMPTS   1
#define APP_OTA_ROLLBACK_REASON_ANTI_ROLLBACK   2
#define APP_OTA_ROLLBACK_REASON_MANUAL          3

typedef struct {
    u32 magic;
    u8 version;
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
    u16 crc16;
    u16 reserved;
} app_ota_guard_record_t;

static u8 s_ota_active;
static u8 s_ota_last_result;
static app_ota_guard_record_t s_guard;
static u8 s_guard_valid;
static u8 s_guard_pending_boot;
static u32 s_guard_boot_tick;
static u32 s_guard_meta_addr;
static u32 s_guard_backup_addr;
static u8 s_flash_copy_buf[16];

static u32 app_ota_slot_base(u8 slot)
{
    return slot ? APP_OTA_SLOT1_BASE : APP_OTA_SLOT0_BASE;
}

static u8 app_ota_other_slot(u8 slot)
{
    return slot ? 0 : 1;
}

static u8 app_ota_detect_active_slot(void)
{
    u8 mark0 = 0xff;
    u8 mark1 = 0xff;
    flash_read_page(APP_OTA_SLOT0_BASE + BOOT_MARK_ADDR, 1, &mark0);
    flash_read_page(APP_OTA_SLOT1_BASE + BOOT_MARK_ADDR, 1, &mark1);
    if (mark1 == BOOT_MARK_VALUE && mark0 != BOOT_MARK_VALUE) {
        return 1;
    }
    return 0;
}

static u16 app_ota_guard_crc(const app_ota_guard_record_t *record)
{
    app_ota_guard_record_t tmp;
    if (!record) {
        return 0;
    }
    tmp = *record;
    tmp.crc16 = 0;
    return app_crc16(&tmp, sizeof(tmp));
}

static u8 app_ota_guard_calc_addresses(void)
{
    app_storage_flash_info_t info;
    app_storage_get_flash_info(&info);
    if (info.app_base_addr < (APP_OTA_SECTOR_SIZE * 2)) {
        return 0;
    }
    s_guard_meta_addr = info.app_base_addr - APP_OTA_SECTOR_SIZE;
    s_guard_backup_addr = info.app_base_addr - (APP_OTA_SECTOR_SIZE * 2);
    if (s_guard_backup_addr < APP_OTA_SLOT1_BASE + ((u32)PENDANT_OTA_FIRMWARE_MAX_K * 1024UL)) {
        return 0;
    }
    return 1;
}

static u8 app_ota_guard_load(app_ota_guard_record_t *record)
{
    if (!record || !app_ota_guard_calc_addresses()) {
        return 0;
    }
    flash_read_page(s_guard_meta_addr, sizeof(*record), (u8 *)record);
    if (record->magic != APP_OTA_GUARD_MAGIC ||
        record->version != APP_OTA_GUARD_VERSION ||
        record->crc16 != app_ota_guard_crc(record)) {
        return 0;
    }
    return 1;
}

static app_status_t app_ota_guard_save(const app_ota_guard_record_t *record)
{
    app_ota_guard_record_t out;
    app_ota_guard_record_t verify;

    if (!record || !app_ota_guard_calc_addresses()) {
        return APP_ERR_STATE;
    }

    out = *record;
    out.magic = APP_OTA_GUARD_MAGIC;
    out.version = APP_OTA_GUARD_VERSION;
    out.crc16 = 0;
    out.crc16 = app_ota_guard_crc(&out);

    flash_erase_sector(s_guard_meta_addr);
    flash_write_page(s_guard_meta_addr, sizeof(out), (u8 *)&out);
    flash_read_page(s_guard_meta_addr, sizeof(verify), (u8 *)&verify);
    if (verify.magic != APP_OTA_GUARD_MAGIC ||
        verify.version != APP_OTA_GUARD_VERSION ||
        verify.crc16 != app_ota_guard_crc(&verify)) {
        return APP_ERR_FLASH;
    }
    s_guard = verify;
    s_guard_valid = 1;
    return APP_OK;
}

static void app_ota_guard_default_confirmed(void)
{
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.magic = APP_OTA_GUARD_MAGIC;
    s_guard.version = APP_OTA_GUARD_VERSION;
    s_guard.state = APP_OTA_GUARD_STATE_CONFIRMED;
    s_guard.active_slot = app_ota_detect_active_slot();
    s_guard.previous_slot = APP_OTA_SLOT_INVALID;
    s_guard.pending_slot = APP_OTA_SLOT_INVALID;
    s_guard.boot_attempts = 0;
    s_guard.backup_valid = 0;
    s_guard.confirmed_version = APP_BUILD_VERSION_CODE;
    s_guard.pending_version = 0;
    s_guard.boot_count = 1;
    s_guard.last_rollback_reason = 0;
    s_guard_valid = app_ota_guard_save(&s_guard) == APP_OK;
}

static app_status_t app_ota_guard_copy_sector(u32 src, u32 dst)
{
    u32 offset;
    for (offset = 0; offset < APP_OTA_SECTOR_SIZE; offset += sizeof(s_flash_copy_buf)) {
        flash_read_page(src + offset, sizeof(s_flash_copy_buf), s_flash_copy_buf);
        flash_write_page(dst + offset, sizeof(s_flash_copy_buf), s_flash_copy_buf);
    }
    return APP_OK;
}

static app_status_t app_ota_guard_backup_slot(u8 slot)
{
    if (!app_ota_guard_calc_addresses()) {
        return APP_ERR_STATE;
    }
    flash_erase_sector(s_guard_backup_addr);
    return app_ota_guard_copy_sector(app_ota_slot_base(slot), s_guard_backup_addr);
}

static void app_ota_guard_request_rollback(u32 reason)
{
    u8 active;
    u8 zero = 0;
    u32 previous_base;

    if (!s_guard_valid || !s_guard.backup_valid ||
        s_guard.previous_slot == APP_OTA_SLOT_INVALID ||
        !app_ota_guard_calc_addresses()) {
        return;
    }

    active = app_ota_detect_active_slot();
    previous_base = app_ota_slot_base(s_guard.previous_slot);

    flash_erase_sector(previous_base);
    app_ota_guard_copy_sector(s_guard_backup_addr, previous_base);
    flash_write_page(app_ota_slot_base(active) + BOOT_MARK_ADDR, 1, &zero);

    s_guard.state = APP_OTA_GUARD_STATE_ROLLED_BACK;
    s_guard.active_slot = s_guard.previous_slot;
    s_guard.pending_slot = APP_OTA_SLOT_INVALID;
    s_guard.boot_attempts = 0;
    s_guard.last_rollback_reason = reason;
    app_ota_guard_save(&s_guard);
    start_reboot();
}

static void app_ota_guard_prepare_for_update(void)
{
    u8 active;

    if (!app_ota_guard_calc_addresses()) {
        return;
    }
    if (!s_guard_valid && !app_ota_guard_load(&s_guard)) {
        app_ota_guard_default_confirmed();
    }
    active = app_ota_detect_active_slot();
    if (app_ota_guard_backup_slot(active) != APP_OK) {
        s_guard.last_error = APP_ERR_FLASH;
        app_ota_guard_save(&s_guard);
        return;
    }
    s_guard.state = APP_OTA_GUARD_STATE_UPDATE_PREPARED;
    s_guard.active_slot = active;
    s_guard.previous_slot = active;
    s_guard.pending_slot = app_ota_other_slot(active);
    s_guard.boot_attempts = 0;
    s_guard.backup_valid = 1;
    s_guard.pending_version = 0;
    s_guard.last_error = APP_OK;
    app_ota_guard_save(&s_guard);
}

static void app_ota_guard_mark_pending(void)
{
    u8 active;
    u8 previous;
    u8 pending;

    if (!s_guard_valid && !app_ota_guard_load(&s_guard)) {
        app_ota_guard_default_confirmed();
    }
    active = app_ota_detect_active_slot();
    previous = s_guard.previous_slot;
    pending = s_guard.pending_slot;
    if (s_guard.state != APP_OTA_GUARD_STATE_UPDATE_PREPARED ||
        previous == APP_OTA_SLOT_INVALID ||
        pending == APP_OTA_SLOT_INVALID) {
        previous = active;
        pending = app_ota_other_slot(active);
    }
    s_guard.state = APP_OTA_GUARD_STATE_PENDING;
    s_guard.previous_slot = previous;
    s_guard.pending_slot = pending;
    s_guard.active_slot = active;
    s_guard.boot_attempts = 0;
    s_guard.pending_version = 0;
    s_guard.last_error = APP_OK;
    app_ota_guard_save(&s_guard);
}

static void app_ota_guard_confirm_current(void)
{
    u8 active = app_ota_detect_active_slot();

    if (!s_guard_valid && !app_ota_guard_load(&s_guard)) {
        app_ota_guard_default_confirmed();
        return;
    }
    s_guard.state = APP_OTA_GUARD_STATE_CONFIRMED;
    s_guard.active_slot = active;
    s_guard.previous_slot = active;
    s_guard.pending_slot = APP_OTA_SLOT_INVALID;
    s_guard.boot_attempts = 0;
    s_guard.backup_valid = 0;
    s_guard.pending_version = 0;
    if (s_guard.confirmed_version < APP_BUILD_VERSION_CODE) {
        s_guard.confirmed_version = APP_BUILD_VERSION_CODE;
    }
    s_guard.last_error = APP_OK;
    app_ota_guard_save(&s_guard);
}

static void app_ota_guard_poll(void)
{
#if (PENDANT_OTA_GUARD_ENABLE)
    if (!s_guard_pending_boot) {
        return;
    }
    if (!clock_time_exceed(s_guard_boot_tick, PENDANT_OTA_CONFIRM_DELAY_MS * 1000UL)) {
        return;
    }
    if (app_system_get_state() == SYS_STATE_ADV_SCAN ||
        app_system_get_state() == SYS_STATE_APP_CONNECTED) {
        s_guard_pending_boot = 0;
        app_ota_guard_confirm_current();
    }
#endif
}

static void app_ota_on_start(void)
{
    app_ble_conn_info_t info;

    s_ota_active = 1;
    s_ota_last_result = 0xff;

#if (PENDANT_OTA_GUARD_ENABLE)
    app_ota_guard_prepare_for_update();
#endif

    app_ble_get_conn_info(&info);
    if (info.connected) {
        bls_l2cap_requestConnParamUpdate(info.conn_handle,
                                         CONN_INTERVAL_7P5MS,
                                         CONN_INTERVAL_15MS,
                                         0,
                                         CONN_TIMEOUT_4S);
    }

    /* Keep the active GATT connection quiet while flash is being written. */
    app_ble_set_scan_enabled(0);
    app_ble_set_adv1_enabled(0);
}

static void app_ota_restore_idle_radio(void)
{
    app_ble_set_adv0_enabled(1);
    app_ble_set_adv1_enabled(1);
    app_ble_set_scan_enabled(1);
}

static void app_ota_on_result(int result)
{
    s_ota_last_result = (u8)result;
    s_ota_active = 0;
    if (result != 0) {
        app_ota_restore_idle_radio();
    } else {
#if (PENDANT_OTA_GUARD_ENABLE)
        app_ota_guard_mark_pending();
#endif
    }
}

void app_ota_configure_boot(void)
{
#if (PENDANT_OTA_ENABLE)
    blc_ota_setFirmwareSizeAndBootAddress(PENDANT_OTA_FIRMWARE_MAX_K, MULTI_BOOT_ADDR_0x40000);
#endif
}

void app_ota_guard_boot_check(void)
{
#if (PENDANT_OTA_GUARD_ENABLE)
    u8 active;

    s_guard_valid = app_ota_guard_load(&s_guard);
    if (!s_guard_valid) {
        app_ota_guard_default_confirmed();
        return;
    }

    active = app_ota_detect_active_slot();
    s_guard.boot_count++;
    s_guard.active_slot = active;

    if (s_guard.state == APP_OTA_GUARD_STATE_PENDING &&
        active == s_guard.pending_slot) {
        if (APP_BUILD_VERSION_CODE < s_guard.confirmed_version) {
            app_ota_guard_request_rollback(APP_OTA_ROLLBACK_REASON_ANTI_ROLLBACK);
            return;
        }
        if (s_guard.boot_attempts >= PENDANT_OTA_MAX_PENDING_BOOT_ATTEMPTS) {
            app_ota_guard_request_rollback(APP_OTA_ROLLBACK_REASON_BOOT_ATTEMPTS);
            return;
        }
        s_guard.boot_attempts++;
        s_guard.pending_version = APP_BUILD_VERSION_CODE;
        app_ota_guard_save(&s_guard);
        s_guard_pending_boot = 1;
        s_guard_boot_tick = clock_time();
        return;
    }

    if (APP_BUILD_VERSION_CODE < s_guard.confirmed_version &&
        s_guard.backup_valid) {
        app_ota_guard_request_rollback(APP_OTA_ROLLBACK_REASON_ANTI_ROLLBACK);
        return;
    }

    if (s_guard.state != APP_OTA_GUARD_STATE_CONFIRMED ||
        s_guard.confirmed_version < APP_BUILD_VERSION_CODE) {
        app_ota_guard_confirm_current();
    } else {
        app_ota_guard_save(&s_guard);
    }
#endif
}

void app_ota_init(void)
{
#if (PENDANT_OTA_ENABLE)
    s_ota_active = 0;
    s_ota_last_result = 0xff;

    blc_ota_initOtaServer_module();
    blc_ota_setFirmwareVersionNumber((u16)APP_BUILD_VERSION_CODE);
    blc_ota_setOtaProcessTimeout(1000);
    blc_ota_setOtaDataPacketTimeout(10);
    blc_ota_setOtaScheduleIndication_by_pduNum(128);
    blc_ota_registerOtaStartCmdCb(app_ota_on_start);
    blc_ota_registerOtaResultIndicationCb(app_ota_on_result);
#endif
}

void app_ota_poll(void)
{
    if (s_ota_active) {
        app_system_watchdog_feed();
    } else {
        app_ota_guard_poll();
    }
}

void app_ota_on_disconnected(void)
{
    if (s_ota_active) {
        s_ota_last_result = 0x0e;
        s_ota_active = 0;
        app_ota_restore_idle_radio();
    }
}

u8 app_ota_is_active(void)
{
    return s_ota_active;
}

u8 app_ota_last_result(void)
{
    return s_ota_last_result;
}

void app_ota_guard_get_info(app_ota_guard_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    if (!s_guard_valid) {
        s_guard_valid = app_ota_guard_load(&s_guard);
    }
    app_ota_guard_calc_addresses();
    info->valid = s_guard_valid;
    info->state = s_guard.state;
    info->active_slot = app_ota_detect_active_slot();
    info->previous_slot = s_guard.previous_slot;
    info->pending_slot = s_guard.pending_slot;
    info->boot_attempts = s_guard.boot_attempts;
    info->backup_valid = s_guard.backup_valid;
    info->last_error = s_guard.last_error;
    info->confirmed_version = s_guard.confirmed_version;
    info->pending_version = s_guard.pending_version;
    info->boot_count = s_guard.boot_count;
    info->last_rollback_reason = s_guard.last_rollback_reason;
    info->meta_addr = s_guard_meta_addr;
    info->backup_addr = s_guard_backup_addr;
}
