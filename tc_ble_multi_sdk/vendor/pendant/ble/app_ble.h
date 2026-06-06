#ifndef APP_BLE_H_
#define APP_BLE_H_

#include "../common/app_types.h"

typedef struct {
    u16 adv_interval_ms;
    u16 scan_interval_ms;
    u16 scan_window_ms;
    u8 tx_power;
} app_ble_params_t;

typedef struct {
    u8 connected;
    u8 peer_addr[6];
    u16 conn_handle;
} app_ble_conn_info_t;

typedef struct {
    u8 connected;
    u8 adv0_status;
    u8 adv1_status;
    u8 scan_status;
    u8 last_adv_update_status;
    u16 adv_update_ok;
    u16 adv_update_fail;
} app_ble_debug_t;

void app_ble_init(void);
app_status_t app_ble_start_adv_scan(const app_ble_params_t *params);
app_status_t app_ble_stop_adv_scan(void);
app_status_t app_ble_update_ext_adv_data(const u8 *data, u8 len);
app_status_t app_ble_disconnect_app(u8 reason);
u8 app_ble_is_app_connected(void);
void app_ble_get_conn_info(app_ble_conn_info_t *info);
void app_ble_get_debug(app_ble_debug_t *debug);
void app_ble_poll(void);
void app_ble_on_connected(const u8 *peer_addr, u16 conn_handle);
void app_ble_on_disconnected(u8 reason);
void app_ble_on_adv_report(const u8 *adv_data, u8 adv_len, s8 rssi, const u8 *addr);

#endif
