#include "app_pendant.h"
#include "board/app_board.h"
#include "diag/app_diag.h"
#include "event/app_event.h"
#include "storage/app_storage.h"
#include "crypto/app_crypto.h"
#include "config/app_config_store.h"
#include "identity/app_identity.h"
#include "factory/app_factory.h"
#include "battery/app_battery.h"
#include "charge/app_charge.h"
#include "motion/app_motion.h"
#include "motor/app_motor.h"
#include "pm/app_pm.h"
#include "ble/app_ble.h"
#include "adv_proto/app_adv_proto.h"
#include "adv_scheduler/app_adv_scheduler.h"
#include "peer_table/app_peer_table.h"
#include "discovery/app_discovery.h"
#include "scan/app_scan.h"
#include "host_cmd/app_host_cmd.h"
#include "host_gatt/app_host_gatt.h"
#include "host_transport/app_host_transport.h"
#include "debug_shell/app_debug_shell.h"
#include "system/app_system.h"
#include "drivers.h"
#include "timer.h"

void app_pendant_init(void)
{
    app_board_init();
    app_diag_init();
    app_event_init();
    app_storage_init();
    app_crypto_init();
    app_config_init();
    app_identity_init();
    app_factory_init();
    app_battery_init();
    app_charge_init();
    app_motion_init();
    app_motor_init();
    app_pm_init();
    app_ble_init();
    app_adv_proto_init();
    app_adv_scheduler_init();
    app_peer_table_init();
    app_discovery_init();
    app_scan_init();
    app_host_cmd_init();
    app_host_transport_init();
    app_system_init();
    app_debug_shell_init();
}

void app_pendant_poll(void)
{
    u32 now = clock_time();
    app_debug_shell_poll();
    app_ble_poll();
    app_system_poll();
    app_scan_poll();
    app_host_cmd_poll();
    app_host_transport_poll();
    app_host_gatt_poll();
    app_discovery_poll(now);
    app_adv_scheduler_poll();
    app_battery_poll(now);
    app_charge_poll(now);
    app_motion_poll(now);
    app_motor_poll(now);
    app_pm_poll(now);
}

void app_pendant_on_adv_report(const u8 *adv_data, u8 adv_len, s8 rssi, const u8 *addr)
{
    app_ble_on_adv_report(adv_data, adv_len, rssi, addr);
}

void app_pendant_on_app_connected(const u8 *peer_addr, u16 conn_handle)
{
    app_ble_on_connected(peer_addr, conn_handle);
    app_host_gatt_on_connected(conn_handle);
}

void app_pendant_on_app_disconnected(u8 reason)
{
    app_ble_on_disconnected(reason);
    app_host_gatt_on_disconnected();
    (void)reason;
}
