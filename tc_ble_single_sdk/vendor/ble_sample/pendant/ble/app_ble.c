#include "app_ble.h"
#include "../event/app_event.h"
#include "../scan/app_scan.h"
#include "stack/ble/ble.h"
#include "common/string.h"

#ifndef BLC_SCAN_DISABLE
#define BLC_SCAN_DISABLE 0
#endif
#ifndef DUP_FILTER_DISABLE
#define DUP_FILTER_DISABLE 0
#endif
#ifndef APP_BLE_ENABLE_DISCOVERY_SCAN
#define APP_BLE_ENABLE_DISCOVERY_SCAN 1
#endif

static app_ble_conn_info_t s_conn;

void app_ble_init(void)
{
    memset(&s_conn, 0, sizeof(s_conn));
}

app_status_t app_ble_start_adv_scan(const app_ble_params_t *params)
{
    (void)params;
    blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, 1, ADV_HANDLE0, 0, 0);
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    blc_ll_setScanEnable(BLC_SCAN_ENABLE, DUP_FILTER_DISABLE);
#endif
    return APP_OK;
}

app_status_t app_ble_stop_adv_scan(void)
{
    blc_ll_setExtAdvEnable(BLC_ADV_DISABLE, 1, ADV_HANDLE0, 0, 0);
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    blc_ll_setScanEnable(BLC_SCAN_DISABLE, DUP_FILTER_DISABLE);
#endif
    return APP_OK;
}

app_status_t app_ble_update_ext_adv_data(const u8 *data, u8 len)
{
    if (!data || !len) {
        return APP_ERR_PARAM;
    }
    if (blc_ll_setExtAdvData(ADV_HANDLE0, DATA_OPER_COMPLETE, DATA_FRAGM_ALLOWED, len, (u8 *)data) != BLE_SUCCESS) {
        return APP_ERR_STATE;
    }
    return APP_OK;
}

app_status_t app_ble_disconnect_app(u8 reason)
{
    if (!s_conn.connected) {
        return APP_OK;
    }
    return bls_ll_terminateConnection(reason) == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

u8 app_ble_is_app_connected(void)
{
    return s_conn.connected;
}

void app_ble_get_conn_info(app_ble_conn_info_t *info)
{
    if (info) {
        *info = s_conn;
    }
}

void app_ble_poll(void)
{
}

void app_ble_on_connected(const u8 *peer_addr, u16 conn_handle)
{
    s_conn.connected = 1;
    s_conn.conn_handle = conn_handle;
    if (peer_addr) {
        memcpy(s_conn.peer_addr, peer_addr, sizeof(s_conn.peer_addr));
    }
    app_event_post(APP_EVT_APP_CONNECTED, 0, 0);
}

void app_ble_on_disconnected(u8 reason)
{
    s_conn.connected = 0;
    s_conn.conn_handle = 0;
    app_event_post(APP_EVT_APP_DISCONNECTED, &reason, sizeof(reason));
}

void app_ble_on_adv_report(const u8 *adv_data, u8 adv_len, s8 rssi, const u8 *addr)
{
    app_scan_report_t report;
    report.addr = addr;
    report.rssi = rssi;
    report.adv_data = adv_data;
    report.adv_len = adv_len;
    app_scan_on_report(&report);
}
