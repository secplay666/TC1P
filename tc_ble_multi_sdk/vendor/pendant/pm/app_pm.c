#include "app_pm.h"
#include "../motion/app_motion.h"
#include "../board/app_board.h"
#include "../ble/app_ble.h"
#include "../charge/app_charge.h"
#include "../motor/app_motor.h"
#include "../peer_table/app_peer_table.h"
#include "../system/app_system.h"
#include "../app_ota.h"
#include "../app_config.h"
#include "stack/ble/ble.h"
#include "drivers.h"
#include "timer.h"

#if (PENDANT_BLE_PM_CONN_SLEEP)
#define APP_PM_BLE_SLEEP_MASK  (PM_SLEEP_LEG_ADV | PM_SLEEP_EXT_ADV | PM_SLEEP_ACL_PERIPHR)
#else
#define APP_PM_BLE_SLEEP_MASK  (PM_SLEEP_LEG_ADV | PM_SLEEP_EXT_ADV)
#endif
#define APP_PM_ACTIVITY_HOLD_US 1000000
/* Idle sleep uses deep retention so that this marker, kept in retention
 * SRAM, identifies a wake from our own sleep. The chip's pad-wake status
 * could not be read reliably at the point it is needed, and plain deep
 * sleep keeps neither SRAM nor the spare analog registers. */
#define APP_PM_SLEEP_MAGIC      0x534C5050UL /* "SLPP" */
/* Stay awake briefly after every boot: Swire cannot halt a suspended MCU,
 * so this is the window in which a debugger (BDT activate) can take over. */
#define APP_PM_BOOT_AWAKE_US    3000000

static u8 s_ble_sleep_blockers;
static u8 s_activity_hold;
static u32 s_activity_tick;
static u8 s_boot_hold;
static u32 s_boot_tick;

static u16 s_idle_s;
static u32 s_idle_tick;
static u32 s_idle_motion_count;
static u8 s_sleep_requested;
static u8 s_idle_blockers;
static u16 s_idle_peak_s;

static app_sleep_reason_t s_sleep_reason;
static app_wakeup_reason_t s_wakeup_reason;
static u8 s_raw_wakeup_src;
/* Placed explicitly: the SDK's _attribute_data_retention_ is empty because
 * PM_DEEPSLEEP_RETENTION_ENABLE is 0. The linker puts .retention_data right
 * after ram_code, inside the low 32 KB kept by DEEPSLEEP_MODE_RET_SRAM_LOW32K;
 * the non-zero initialiser keeps it out of .bss. */
__attribute__((section(".retention_data"))) static u32 s_sleep_magic = 1;
/* Outcome of the last wake check, handed across the reboot that follows it:
 * .data_no_init is neither cleared nor initialised at boot, so it survives a
 * software reboot. [31:16] magic, [15:8] active slots, [7:0] reason. */
#define APP_PM_WAKE_REPORT_MAGIC 0x5741UL
__attribute__((section(".data_no_init"))) static u32 s_wake_report;
/* Wakes rejected since the last real boot: [15:8] count, [7:0] slots of the
 * last one. Kept in retention SRAM across sleeps, handed over with the
 * report on the next real wake. */
__attribute__((section(".retention_data"))) static u32 s_rejected = 1;
__attribute__((section(".data_no_init"))) static u32 s_rejected_report;
static u16 s_rejected_info;
static u8 s_wake_reason_code;
static u8 s_retention_flag;
static u8 s_wake_slots;

void app_pm_init(void)
{
    s_sleep_reason = PM_SLEEP_REASON_IDLE;
    s_raw_wakeup_src = (u8)pm_get_wakeup_src();
    if (s_wake_reason_code) {
        s_raw_wakeup_src = 0x80 | s_wake_reason_code;
    }
    if (s_raw_wakeup_src & WAKEUP_STATUS_WD) {
        s_wakeup_reason = PM_WAKEUP_REASON_RESET;
    } else if (s_raw_wakeup_src & WAKEUP_STATUS_PAD) {
        s_wakeup_reason = PM_WAKEUP_REASON_MOTION;
    } else {
        s_wakeup_reason = PM_WAKEUP_REASON_UNKNOWN;
    }
}

app_wakeup_reason_t app_pm_get_wakeup_reason(void)
{
    return s_wakeup_reason;
}

u8 app_pm_get_raw_wakeup_src(void)
{
    return s_raw_wakeup_src;
}

/* Pad wake is level-only. The vibration switch rests open or closed at
 * random, so arm the motion pin for the level opposite to its current one:
 * any movement then wakes, and sleep is never re-entered already "woken". */
static void app_pm_arm_wakeup_pins(void)
{
    u32 motion_pin = app_board_get_pin(APP_PIN_MOTION);
    u32 charge_pin = app_board_get_pin(APP_PIN_CHARGE_DET);
    u32 trig_pin = app_board_get_pin(APP_PIN_MOTOR_TRIG);

    if (motion_pin) {
        cpu_set_gpio_wakeup((GPIO_PinTypeDef)motion_pin,
                            gpio_read(motion_pin) ? Level_Low : Level_High, 1);
    }
    if (charge_pin) {
        cpu_set_gpio_wakeup((GPIO_PinTypeDef)charge_pin, Level_High, 1);
    }
    if (trig_pin) {
        /* Digital outputs are released in deep sleep; keep the motor off. */
        gpio_setup_up_down_resistor((GPIO_PinTypeDef)trig_pin, PM_PIN_PULLDOWN_100K);
    }
}

app_status_t app_pm_prepare_sleep(app_sleep_reason_t reason)
{
    s_sleep_reason = reason;
    app_motor_stop();
    return APP_OK;
}

static void app_pm_retention_sleep(void)
{
    app_pm_arm_wakeup_pins();
    s_sleep_magic = APP_PM_SLEEP_MAGIC;
    /* Runtime BLE PM keeps deep retention off; without enabling it here the
     * low-level sleep falls back to plain deep sleep and SRAM is lost. */
    blc_pm_setDeepsleepRetentionEnable(PM_DeepRetn_Enable);
    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);
    cpu_sleep_wakeup(DEEPSLEEP_MODE_RET_SRAM_LOW32K, PM_WAKEUP_PAD, 0);
}

void app_pm_enter_sleep(void)
{
    (void)s_sleep_reason;
    app_pm_retention_sleep();
}

/* Pad wake enables live in analog registers that survive resets (only a
 * power cycle clears them). Left armed, a pin already at its wake level
 * (e.g. the charge pin while charging) aborts every BLE suspend at once
 * and the stack stops advertising, so disarm them on every boot. */
static void app_pm_disarm_wakeup_pins(void)
{
    u32 motion_pin = app_board_get_pin(APP_PIN_MOTION);
    u32 charge_pin = app_board_get_pin(APP_PIN_CHARGE_DET);

    if (motion_pin) {
        cpu_set_gpio_wakeup((GPIO_PinTypeDef)motion_pin, Level_High, 0);
    }
    if (charge_pin) {
        cpu_set_gpio_wakeup((GPIO_PinTypeDef)charge_pin, Level_High, 0);
    }
}

static void app_pm_reboot_after_wake(u8 reason, u8 slots)
{
    s_wake_report = (APP_PM_WAKE_REPORT_MAGIC << 16) | ((u32)slots << 8) |
                    (s_retention_flag ? 0x40 : 0) | reason;
    s_rejected_report = (APP_PM_WAKE_REPORT_MAGIC << 16) | (u16)s_rejected;
    start_reboot();
}

void app_pm_handle_boot(u8 retention_wake)
{
    u32 motion_pin;
    u32 charge_pin;
    u32 start_tick;
    u32 slot_tick;
    u8 level;
    u8 slots = 0;
    u8 slot_active = 0;

    app_board_init();
    app_pm_disarm_wakeup_pins();
    /* A normal boot always re-copies .retention_data from flash, so the
     * marker survives only a real deep-retention wake; the SDK's retention
     * flag (retention_wake) did not report it here. */
    if (s_sleep_magic != APP_PM_SLEEP_MAGIC) {
        s_sleep_magic = 1;
        if ((s_wake_report >> 16) == APP_PM_WAKE_REPORT_MAGIC) {
            s_wake_reason_code = (u8)s_wake_report;
            s_wake_slots = (u8)(s_wake_report >> 8);
            if ((s_rejected_report >> 16) == APP_PM_WAKE_REPORT_MAGIC) {
                s_rejected_info = (u16)s_rejected_report;
            }
        }
        s_wake_report = 0;
        s_rejected_report = 0;
        s_rejected = 0;
        return;
    }
    /* Waking from deep retention leaves non-retention RAM uninitialised, so
     * every path below ends in a full reboot or another sleep. */
    s_sleep_magic = 1;
    s_retention_flag = retention_wake;
    motion_pin = app_board_get_pin(APP_PIN_MOTION);
    charge_pin = app_board_get_pin(APP_PIN_CHARGE_DET);
    if (!motion_pin || (charge_pin && gpio_read(charge_pin))) {
        app_pm_reboot_after_wake(APP_PM_WAKE_CHARGING, 0);
    }

    level = gpio_read(motion_pin) ? 1 : 0;
    start_tick = clock_time();
    slot_tick = start_tick;
    while (!clock_time_exceed(start_tick, PENDANT_MOTION_WAKE_WINDOW_MS * 1000UL)) {
        wd_clear();
        if (charge_pin && gpio_read(charge_pin)) {
            app_pm_reboot_after_wake(APP_PM_WAKE_CHARGING, slots);
        }
        if (clock_time_exceed(slot_tick, PENDANT_MOTION_WAKE_SLOT_MS * 1000UL)) {
            slot_tick = clock_time();
            slot_active = 0;
        }
        if ((gpio_read(motion_pin) ? 1 : 0) != level) {
            level ^= 1;
            if (!slot_active) {
                slot_active = 1;
                slots++;
            }
        }
    }
    /* Watch the whole window (rather than stopping at the threshold) so the
     * reported slot count shows how far a wake was from the threshold. */
    if (slots >= PENDANT_MOTION_WAKE_MIN_SLOTS) {
        app_pm_reboot_after_wake(APP_PM_WAKE_MOTION, slots);
    }
    s_rejected = (((s_rejected >> 8) + 1) & 0xFF) << 8 | slots;
    app_pm_retention_sleep();
}

static u8 app_pm_idle_blocked(void)
{
    app_motion_state_t motion;
    u8 blockers = 0;

    app_motion_get_state(&motion);
    if (motion.motion_count != s_idle_motion_count) {
        s_idle_motion_count = motion.motion_count;
        blockers |= APP_PM_IDLE_BLOCK_MOTION;
    }
    if (app_charge_is_charging()) {
        blockers |= APP_PM_IDLE_BLOCK_CHARGING;
    }
    if (app_peer_table_count()) {
        blockers |= APP_PM_IDLE_BLOCK_PEERS;
    }
    if (app_ota_is_active()) {
        blockers |= APP_PM_IDLE_BLOCK_OTA;
    }
    if (app_system_get_state() != SYS_STATE_ADV_SCAN) {
        blockers |= APP_PM_IDLE_BLOCK_STATE;
    }
    if (app_ble_is_app_connected()) {
        blockers |= APP_PM_IDLE_BLOCK_CONNECTED;
    } else {
        /* Accumulate reasons seen while unconnected (cleared when read), so
         * a host that connects to ask why the device stays awake sees them. */
        s_idle_blockers |= blockers;
    }
    return blockers;
}

u8 app_pm_take_idle_blockers(void)
{
    u8 blockers = s_idle_blockers;

    s_idle_blockers = 0;
    return blockers;
}

/* Counts whole seconds: the 16 MHz system tick wraps after ~268 s, too
 * short for clock_time_exceed() on a 5 minute timeout. */
static void app_pm_idle_poll(void)
{
#if (PENDANT_IDLE_SLEEP_ENABLE)
    if (s_sleep_requested) {
        return;
    }
    if (app_pm_idle_blocked()) {
        s_idle_s = 0;
        s_idle_tick = clock_time();
        return;
    }
    if (!clock_time_exceed(s_idle_tick, 1000000)) {
        return;
    }
    s_idle_tick = clock_time();
    if (++s_idle_s > s_idle_peak_s) {
        s_idle_peak_s = s_idle_s;
    }
    if (s_idle_s >= PENDANT_IDLE_SLEEP_S) {
        s_sleep_requested = 1;
        app_system_request_sleep(PM_SLEEP_REASON_IDLE);
    }
#endif
}

u8 app_pm_get_wake_slots(void)
{
    return s_wake_slots;
}

u16 app_pm_get_rejected_wakes(void)
{
    return s_rejected_info;
}

u16 app_pm_take_idle_peak_seconds(void)
{
    u16 peak = s_idle_peak_s;

    s_idle_peak_s = 0;
    return peak;
}

void app_pm_init_ble(void)
{
#if (PENDANT_BLE_PM_ENABLE)
    blc_ll_initPowerManagement_module();
    /* Suspend only: application RAM is not placed in the retention area. */
    blc_pm_setDeepsleepRetentionEnable(PM_DeepRetn_Disable);
    s_ble_sleep_blockers = APP_PM_BLOCK_BOOT;
    s_boot_hold = 1;
    s_boot_tick = clock_time();
    blc_pm_setSleepMask(PM_SLEEP_DISABLE);
#endif
}

void app_pm_block_ble_sleep(u8 blocker, u8 block)
{
#if (PENDANT_BLE_PM_ENABLE)
    u8 before = s_ble_sleep_blockers;

    if (block) {
        s_ble_sleep_blockers |= blocker;
    } else {
        s_ble_sleep_blockers &= (u8)~blocker;
    }
    if (!before != !s_ble_sleep_blockers) {
        blc_pm_setSleepMask(s_ble_sleep_blockers ? PM_SLEEP_DISABLE : APP_PM_BLE_SLEEP_MASK);
    }
#else
    (void)blocker;
    (void)block;
#endif
}

void app_pm_note_host_activity(void)
{
    s_activity_tick = clock_time();
    if (!s_activity_hold) {
        s_activity_hold = 1;
        app_pm_block_ble_sleep(APP_PM_BLOCK_HOST_ACTIVITY, 1);
    }
}

void app_pm_poll(u32 now_tick)
{
    (void)now_tick;
    app_pm_idle_poll();
    if (s_boot_hold && clock_time_exceed(s_boot_tick, APP_PM_BOOT_AWAKE_US)) {
        s_boot_hold = 0;
        app_pm_block_ble_sleep(APP_PM_BLOCK_BOOT, 0);
    }
    if (s_activity_hold && clock_time_exceed(s_activity_tick, APP_PM_ACTIVITY_HOLD_US)) {
        s_activity_hold = 0;
        app_pm_block_ble_sleep(APP_PM_BLOCK_HOST_ACTIVITY, 0);
    }
}
