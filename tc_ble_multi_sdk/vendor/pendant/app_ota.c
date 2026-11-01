#include "app_ota.h"
#include "ble/app_ble.h"
#include "system/app_system.h"
#include "storage/app_storage.h"
#include "crypto/app_crypto.h"
#include "pm/app_pm.h"
#include "build_info/app_build_info.h"
#include "stack/ble/ble.h"
#include "stack/ble/host/l2cap/l2cap.h"
#include "drivers/B85/driver_ext/mcu_boot.h"
#include "stack/ble/service/ota/ota.h"
#include "stack/ble/service/ota/ota_server.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define APP_OTA_SLOT0_BASE                      0x00000UL
#define APP_OTA_SLOT1_BASE                      MULTI_BOOT_ADDR_0x40000
#define APP_OTA_SECTOR_SIZE                     0x1000UL
#define APP_OTA_CONN_INTERVAL_MIN               6     /* 7.5 ms, 1.25 ms units */
#define APP_OTA_CONN_INTERVAL_MAX               12    /* 15 ms */
#define APP_OTA_CONN_TIMEOUT                    400   /* 4 s, 10 ms units */
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
#define APP_OTA_DBG_STAGE_REG                   DEEP_ANA_REG6
#define APP_OTA_DBG_WRITE_LO_REG                DEEP_ANA_REG7
#define APP_OTA_DBG_WRITE_HI_REG                DEEP_ANA_REG8
#define APP_OTA_DBG_INDEX_LO_REG                DEEP_ANA_REG9
 /* DEEP_ANA_REG10 unused: only the low byte of the last index is kept. */
#define APP_OTA_DBG_STAGE_START                 0xa1
#define APP_OTA_DBG_STAGE_START_ACCEPTED        0xa2
#define APP_OTA_DBG_STAGE_DATA                  0xd1
#define APP_OTA_DBG_STAGE_DIAG_START            0xe1
#define APP_OTA_DBG_STAGE_DIAG_FLASH_OK         0xe2
#define APP_OTA_DBG_STAGE_DIAG_FLASH_FAIL       0xe3
#define APP_OTA_DIAG_TEST_ADDR                  APP_OTA_SLOT1_BASE

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
static u8 s_ota_boot_config_status = 0xff;
static u16 s_ota_dbg_write_count;
static u16 s_ota_dbg_data_count;
static u16 s_ota_dbg_last_index;
static u8 s_ota_dbg_start_accepted;
#if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE)
static u8 s_ota_diag_data0_written;
static u16 s_ota_diag_real_data_count;
#endif
static u32 s_ota_diag_after_write_status;

static u8 app_ota_guard_load(app_ota_guard_record_t *record);
static app_status_t app_ota_guard_save(const app_ota_guard_record_t *record);
static void app_ota_guard_default_confirmed(void);

#if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE)
static void app_ota_diag_persist_after_write(u32 status)
{
    if (!s_guard_valid && !app_ota_guard_load(&s_guard)) {
        app_ota_guard_default_confirmed();
    }
    s_guard.pending_version = (s_ota_dbg_start_accepted ? 0x80000000UL : 0) |
                              ((u32)s_ota_dbg_write_count << 16) |
                              s_ota_dbg_data_count;
    s_guard.last_rollback_reason = status;
    app_ota_guard_save(&s_guard);
}
#endif

extern unsigned int ota_program_bootAddr;
extern unsigned int ota_firmware_max_size;
extern unsigned int ota_program_offset;

static u32 app_ota_slot_base(u8 slot)
{
    return slot ? APP_OTA_SLOT1_BASE : APP_OTA_SLOT0_BASE;
}

static u8 app_ota_other_slot(u8 slot)
{
    return slot ? 0 : 1;
}

static u8 app_ota_read_slot_boot_mark(u8 slot)
{
    u8 mark = 0xff;
    flash_read_page(app_ota_slot_base(slot) + BOOT_MARK_ADDR, 1, &mark);
    return mark;
}

static void app_ota_clear_slot_boot_mark(u8 slot)
{
    u8 zero = 0;
    flash_write_page(app_ota_slot_base(slot) + BOOT_MARK_ADDR, 1, &zero);
}

/* The Telink OTA server writes without erasing and verifies by read-back,
 * so any programmed byte left in the target slot head (e.g. a cleared boot
 * mark) makes the next update fail with OTA_WRITE_FLASH_ERR. */
static void app_ota_erase_slot_head_if_dirty(u8 slot)
{
    u8 head[16];
    u8 i;

    flash_read_page(app_ota_slot_base(slot), sizeof(head), head);
    for (i = 0; i < sizeof(head); i++) {
        if (head[i] != 0xff) {
            flash_erase_sector(app_ota_slot_base(slot));
            return;
        }
    }
}

static u8 app_ota_detect_active_slot(void)
{
    u8 mark0 = app_ota_read_slot_boot_mark(0);
    u8 mark1 = app_ota_read_slot_boot_mark(1);
    if (mark1 == BOOT_MARK_VALUE && mark0 != BOOT_MARK_VALUE) {
        return 1;
    }
    return 0;
}

static void app_ota_guard_note_wakeup_src(void)
{
    u8 raw = app_pm_get_raw_wakeup_src();
    if (raw) {
        s_guard.reserved = raw;
    }
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
    app_ota_guard_note_wakeup_src();
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

/* The OTA server erases the other slot when a new image initialises it
 * (blc_ota_initOtaServer_module), and the guard only backs up the first
 * sector. Rolling back onto an erased slot would leave nothing bootable,
 * so check that the image body is still there first. */
static u8 app_ota_slot_body_intact(u8 slot)
{
    static const u32 probe_offsets[] = {APP_OTA_SECTOR_SIZE, 0x10000UL};
    u8 probe[16];
    u8 i;
    u8 j;

    for (i = 0; i < sizeof(probe_offsets) / sizeof(probe_offsets[0]); i++) {
        flash_read_page(app_ota_slot_base(slot) + probe_offsets[i], sizeof(probe), probe);
        for (j = 0; j < sizeof(probe) && probe[j] == 0xff; j++) {
        }
        if (j == sizeof(probe)) {
            return 0;
        }
    }
    return 1;
}

static void app_ota_guard_request_rollback(u32 reason)
{
    u8 active;
    u32 previous_base;

    if (!s_guard_valid || !s_guard.backup_valid ||
        s_guard.previous_slot == APP_OTA_SLOT_INVALID ||
        !app_ota_guard_calc_addresses() ||
        !app_ota_slot_body_intact(s_guard.previous_slot)) {
        return;
    }

    active = app_ota_detect_active_slot();
    previous_base = app_ota_slot_base(s_guard.previous_slot);

    flash_erase_sector(previous_base);
    app_ota_guard_copy_sector(s_guard_backup_addr, previous_base);
    app_ota_clear_slot_boot_mark(active);

    s_guard.state = APP_OTA_GUARD_STATE_ROLLED_BACK;
    s_guard.active_slot = s_guard.previous_slot;
    s_guard.pending_slot = APP_OTA_SLOT_INVALID;
    s_guard.boot_attempts = 0;
    s_guard.last_rollback_reason = reason;
    app_ota_guard_save(&s_guard);
    start_reboot();
}

static void app_ota_guard_abort_update(u8 error)
{
    u8 active;
    u8 failed_slot;

    if (!s_guard_valid && !app_ota_guard_load(&s_guard)) {
        app_ota_guard_default_confirmed();
    }

    active = app_ota_detect_active_slot();
    failed_slot = app_ota_other_slot(active);
    if (s_guard.pending_slot != APP_OTA_SLOT_INVALID) {
        failed_slot = s_guard.pending_slot;
    }

    /* Erase rather than zero the boot mark so the slot stays writable. */
    flash_erase_sector(app_ota_slot_base(failed_slot));

    /* Only rewrite the previous slot's first sector when its boot mark is
     * actually gone (cleared by the stack at the end of a successful
     * transfer). For mid-transfer failures it is intact, and erasing it
     * would open a power-loss window with no bootable slot. */
    if (s_guard.backup_valid &&
        s_guard.previous_slot != APP_OTA_SLOT_INVALID &&
        app_ota_read_slot_boot_mark(s_guard.previous_slot) != BOOT_MARK_VALUE &&
        app_ota_guard_calc_addresses()) {
        flash_erase_sector(app_ota_slot_base(s_guard.previous_slot));
        app_ota_guard_copy_sector(s_guard_backup_addr, app_ota_slot_base(s_guard.previous_slot));
        active = s_guard.previous_slot;
    } else if (s_guard.previous_slot != APP_OTA_SLOT_INVALID) {
        active = s_guard.previous_slot;
    }
    s_guard.state = APP_OTA_GUARD_STATE_CONFIRMED;
    s_guard.active_slot = active;
    s_guard.previous_slot = active;
    s_guard.pending_slot = APP_OTA_SLOT_INVALID;
    s_guard.boot_attempts = 0;
    s_guard.backup_valid = 0;
    s_guard.pending_version = (s_ota_dbg_start_accepted ? 0x80000000UL : 0) |
                              ((u32)s_ota_dbg_write_count << 16) |
                              s_ota_dbg_data_count;
    s_guard.last_error = error;
    s_guard.last_rollback_reason = s_ota_diag_after_write_status;
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
    s_ota_dbg_start_accepted = 1;
    analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_START_ACCEPTED);
    app_ota_erase_slot_head_if_dirty(app_ota_other_slot(app_ota_detect_active_slot()));

#if (PENDANT_OTA_GUARD_ENABLE)
    app_ota_guard_prepare_for_update();
#endif

    app_ble_get_conn_info(&info);
    if (info.connected) {
        /* OTA throughput is bounded by round trips per connection event;
         * ask the central for a short interval (it may decline). */
        bls_l2cap_requestConnParamUpdate(info.conn_handle,
                                         APP_OTA_CONN_INTERVAL_MIN,
                                         APP_OTA_CONN_INTERVAL_MAX,
                                         0,
                                         APP_OTA_CONN_TIMEOUT);
    }

    app_ble_set_scan_enabled(0);
    app_ble_set_adv1_enabled(0);
    /* Flash writes run from the main loop; keep it running between events. */
    app_pm_block_ble_sleep(APP_PM_BLOCK_OTA, 1);
}

static void app_ota_restore_idle_radio(void)
{
    app_ble_set_adv0_enabled(1);
    app_ble_set_adv1_enabled(1);
    app_ble_set_scan_enabled(1);
    app_pm_block_ble_sleep(APP_PM_BLOCK_OTA, 0);
}

static void app_ota_on_result(int result)
{
    s_ota_last_result = (u8)result;
    s_ota_active = 0;
    if (result != 0) {
#if (PENDANT_OTA_GUARD_ENABLE)
        app_ota_guard_abort_update((u8)result);
#endif
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
    s_ota_boot_config_status = blc_ota_setFirmwareSizeAndBootAddress(PENDANT_OTA_FIRMWARE_MAX_K,
                                                                      MULTI_BOOT_ADDR_0x40000);
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
    app_ota_guard_note_wakeup_src();

    if (s_guard.state == APP_OTA_GUARD_STATE_PENDING &&
        active == s_guard.pending_slot) {
        if (APP_BUILD_VERSION_CODE < s_guard.confirmed_version) {
            app_ota_guard_request_rollback(APP_OTA_ROLLBACK_REASON_ANTI_ROLLBACK);
            /* Only returns when the previous image is gone: keep this one. */
            app_ota_guard_confirm_current();
            return;
        }
        if (s_guard.boot_attempts >= PENDANT_OTA_MAX_PENDING_BOOT_ATTEMPTS) {
            app_ota_guard_request_rollback(APP_OTA_ROLLBACK_REASON_BOOT_ATTEMPTS);
            /* Only returns when the previous image is gone: keep this one. */
            app_ota_guard_confirm_current();
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
        /* Only returns when the previous image is gone: keep this one. */
        app_ota_guard_confirm_current();
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

    /* On the first boot of a new image this erases the other slot, which
     * can take several seconds; keep the early watchdog from firing. */
#if (PENDANT_WATCHDOG_ENABLE)
    wd_stop();
#endif
    blc_ota_initOtaServer_module();
#if (PENDANT_WATCHDOG_ENABLE)
    wd_clear();
    wd_start();
#endif
    blc_ota_setFirmwareVersionNumber((u16)APP_BUILD_VERSION_CODE);
    blc_ota_setOtaProcessTimeout(1000);
    blc_ota_registerOtaStartCmdCb(app_ota_on_start);
    blc_ota_registerOtaResultIndicationCb(app_ota_on_result);
#endif
}

void app_ota_debug_note_write(u16 l2cap_len, const u8 *value, u16 value_len)
{
    u16 cmd;

    (void)l2cap_len;
    if (!value || value_len < 2) {
        return;
    }

    cmd = (u16)value[0] | ((u16)value[1] << 8);
    if (cmd == CMD_OTA_START || cmd == CMD_OTA_START_EXT) {
        s_ota_dbg_write_count = 0;
        s_ota_dbg_data_count = 0;
        s_ota_dbg_last_index = 0xffff;
        s_ota_dbg_start_accepted = 0;
        s_ota_diag_after_write_status = 0;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_START);
        analog_write(APP_OTA_DBG_WRITE_LO_REG, 0);
        analog_write(APP_OTA_DBG_WRITE_HI_REG, 0);
        analog_write(APP_OTA_DBG_INDEX_LO_REG, 0xff);
    }
    s_ota_dbg_write_count++;
    analog_write(APP_OTA_DBG_WRITE_LO_REG, (u8)s_ota_dbg_write_count);
    analog_write(APP_OTA_DBG_WRITE_HI_REG, (u8)(s_ota_dbg_write_count >> 8));
    if (cmd < CMD_OTA_VERSION) {
        s_ota_dbg_data_count++;
        s_ota_dbg_last_index = cmd;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DATA);
        analog_write(APP_OTA_DBG_INDEX_LO_REG, (u8)s_ota_dbg_last_index);
    }
}

u8 app_ota_diag_should_bypass_write(const u8 *value, u16 value_len)
{
#if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE)
    u16 cmd;
    u8 verify[16];

    if (!value || value_len < 2) {
        return 1;
    }

    cmd = (u16)value[0] | ((u16)value[1] << 8);
    if (cmd == CMD_OTA_START || cmd == CMD_OTA_START_EXT) {
        s_ota_diag_data0_written = 0;
        s_ota_diag_real_data_count = 0;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_START);
        return PENDANT_OTA_DIAG_BYPASS_OTAWRITE == 1 ? 1 : 0;
    }

    if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE == 4) {
        if (cmd < CMD_OTA_VERSION &&
            s_ota_diag_real_data_count < PENDANT_OTA_DIAG_REAL_DATA_LIMIT) {
            s_ota_diag_real_data_count++;
            return 0;
        }
        return 1;
    }

    if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE == 3) {
        if (cmd < CMD_OTA_VERSION && !s_ota_diag_data0_written) {
            s_ota_diag_data0_written = 1;
            return 0;
        }
        return 1;
    }

    if (cmd < CMD_OTA_VERSION && !s_ota_diag_data0_written && value_len >= 18) {
        s_ota_diag_data0_written = 1;
        flash_erase_sector(APP_OTA_DIAG_TEST_ADDR);
        flash_write_page(APP_OTA_DIAG_TEST_ADDR, 16, (u8 *)(value + 2));
        flash_read_page(APP_OTA_DIAG_TEST_ADDR, 16, verify);
        if (!memcmp(verify, value + 2, 16)) {
            analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_FLASH_OK);
        } else {
            analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_FLASH_FAIL);
        }
    }
    return 1;
#else
    (void)value;
    (void)value_len;
    return 0;
#endif
}

void app_ota_diag_note_after_write(const u8 *value, u16 value_len, int result)
{
#if (PENDANT_OTA_DIAG_BYPASS_OTAWRITE)
    u16 cmd;
    u8 flash[16];
    u8 all_ff = 1;
    u8 i;

    if (!value || value_len < 2) {
        return;
    }

    cmd = (u16)value[0] | ((u16)value[1] << 8);
    if (cmd >= CMD_OTA_VERSION) {
        s_ota_diag_after_write_status = 0xd0000000UL |
                                        (((u32)result & 0xffUL) << 16) |
                                        ((u32)cmd << 8) |
                                        ((u32)result & 0xffUL);
        app_ota_diag_persist_after_write(s_ota_diag_after_write_status);
        return;
    }

    flash_read_page(APP_OTA_SLOT1_BASE, sizeof(flash), flash);
    for (i = 0; i < sizeof(flash); i++) {
        if (flash[i] != 0xff) {
            all_ff = 0;
            break;
        }
    }

    if (all_ff) {
        s_ota_diag_after_write_status = 0xf3000000UL |
                                        (((u32)result & 0xffUL) << 16) |
                                        cmd;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_FLASH_FAIL);
    } else if (cmd == 0 && value_len >= 18 && !memcmp(flash, value + 2, sizeof(flash))) {
        s_ota_diag_after_write_status = 0xf1000000UL |
                                        (((u32)result & 0xffUL) << 16) |
                                        cmd;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_FLASH_OK);
    } else {
        s_ota_diag_after_write_status = 0xf2000000UL |
                                        (((u32)result & 0xffUL) << 16) |
                                        cmd;
        analog_write(APP_OTA_DBG_STAGE_REG, APP_OTA_DBG_STAGE_DIAG_FLASH_OK);
    }
    if (cmd == 0) {
        app_ota_diag_persist_after_write(s_ota_diag_after_write_status);
    }
#else
    (void)value;
    (void)value_len;
    (void)result;
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
    info->slot0_mark = app_ota_read_slot_boot_mark(0);
    info->slot1_mark = app_ota_read_slot_boot_mark(1);
    info->boot_config_status = s_ota_boot_config_status;
    info->configured_boot_addr = blc_ota_getCurrentUsedMultipleBootAddress();
    info->program_boot_addr = ota_program_bootAddr;
    info->program_offset = ota_program_offset;
    info->firmware_max_size = ota_firmware_max_size;
    info->debug_stage = analog_read(APP_OTA_DBG_STAGE_REG);
    info->debug_write_count = ((u16)analog_read(APP_OTA_DBG_WRITE_HI_REG) << 8) |
                              analog_read(APP_OTA_DBG_WRITE_LO_REG);
    info->debug_last_index = analog_read(APP_OTA_DBG_INDEX_LO_REG);
    info->debug_boot_wakeup_src = s_guard.reserved;
}
