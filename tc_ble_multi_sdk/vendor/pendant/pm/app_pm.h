#ifndef APP_PM_H_
#define APP_PM_H_

#include "../common/app_types.h"

typedef enum {
    PM_SLEEP_REASON_IDLE = 1,
    PM_SLEEP_REASON_CHARGE = 2,
    PM_SLEEP_REASON_APP_IDLE = 3,
    PM_SLEEP_REASON_BATTERY_CRITICAL = 4,
    PM_SLEEP_REASON_ERROR = 5,
} app_sleep_reason_t;

typedef enum {
    PM_WAKEUP_REASON_UNKNOWN = 0,
    PM_WAKEUP_REASON_MOTION = 1,
    PM_WAKEUP_REASON_CHARGE_STOPPED = 2,
    PM_WAKEUP_REASON_RESET = 3,
} app_wakeup_reason_t;

void app_pm_init(void);
app_wakeup_reason_t app_pm_get_wakeup_reason(void);
u8 app_pm_get_raw_wakeup_src(void);
app_status_t app_pm_prepare_sleep(app_sleep_reason_t reason);
void app_pm_enter_sleep(void);
void app_pm_poll(u32 now_tick);
/* Call early in main() after a deep-sleep wake: returns if the wake is a
 * real motion or charging, otherwise goes straight back to deep sleep. */
void app_pm_confirm_motion_wakeup(void);
/* Longest idle run (s) since the last call; the current run always reads
 * 0 to a host because its connection blocks idle. */
u16 app_pm_take_idle_peak_seconds(void);
/* Raw wakeup status; this bit marks a pad wake out of deep sleep. */
#define APP_PM_WAKEUP_DEEP_PAD       0x80
#define APP_PM_IDLE_BLOCK_MOTION     0x01
#define APP_PM_IDLE_BLOCK_CHARGING   0x02
#define APP_PM_IDLE_BLOCK_PEERS      0x04
#define APP_PM_IDLE_BLOCK_OTA        0x08
#define APP_PM_IDLE_BLOCK_STATE      0x10
#define APP_PM_IDLE_BLOCK_CONNECTED  0x20
/* Idle-sleep blockers seen while no app was connected, since the last call. */
u8 app_pm_take_idle_blockers(void);
/* BLE suspend between radio events; call after link layer init. */
#define APP_PM_BLOCK_OTA            0x01
#define APP_PM_BLOCK_HOST_ACTIVITY  0x02
#define APP_PM_BLOCK_BOOT           0x04
void app_pm_init_ble(void);
/* Suspend is allowed only while no blocker is set. */
void app_pm_block_ble_sleep(u8 blocker, u8 block);
/* Keep the MCU awake briefly after app traffic so replies are not delayed. */
void app_pm_note_host_activity(void);

#endif
