#include "app_ble.h"
#include "../event/app_event.h"
#include "../scan/app_scan.h"
#include "../common/app_debug_print.h"
#include "stack/ble/ble.h"
#include "common/string.h"

#ifndef BLC_SCAN_DISABLE
#define BLC_SCAN_DISABLE 0
#endif
#ifndef DUP_FILTER_DISABLE
#define DUP_FILTER_DISABLE 0
#endif
#ifndef APP_BLE_ENABLE_DISCOVERY_SCAN
#define APP_BLE_ENABLE_DISCOVERY_SCAN 0
#endif
#ifndef PENDANT_EXT_ADV_ENABLE
#define PENDANT_EXT_ADV_ENABLE 0
#endif

#define APP_BLE_APP_ADV_HANDLE      ADV_HANDLE0
#define APP_BLE_PENDANT_ADV_HANDLE  ADV_HANDLE1

static app_ble_conn_info_t s_conn;
static u8 s_adv_scan_started;
static u8 s_adv_update_error_log_count;

void app_ble_init(void)
{
    memset(&s_conn, 0, sizeof(s_conn));
    s_adv_scan_started = 0;
    s_adv_update_error_log_count = 0;
}

app_status_t app_ble_start_adv_scan(const app_ble_params_t *params)
{
    ble_sts_t adv0_st;
    ble_sts_t adv1_st;
    ble_sts_t scan_st = BLE_SUCCESS;

    (void)params;
    adv0_st = blc_ll_setAdvEnable(BLC_ADV_ENABLE);
#if PENDANT_EXT_ADV_ENABLE
    adv1_st = blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
#else
    adv1_st = BLE_SUCCESS;
#endif
    s_adv_scan_started = 1;
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    scan_st = blc_ll_setExtScanEnable(BLC_SCAN_ENABLE, DUP_FILTER_DISABLE, SCAN_DURATION_CONTINUOUS, SCAN_WINDOW_CONTINUOUS);
#endif
    u_printf("[BLE] start\r\n");
    u_printf(" adv0=");
    u_printf("%x\r\n", adv0_st);
    u_printf(" adv1=");
    u_printf("%x\r\n", adv1_st);
    u_printf(" scan=");
    u_printf("%x\r\n", scan_st);
    return APP_OK;
}

app_status_t app_ble_stop_adv_scan(void)
{
    blc_ll_setAdvEnable(BLC_ADV_DISABLE);
#if PENDANT_EXT_ADV_ENABLE
    blc_ll_setExtAdvEnable(BLC_ADV_DISABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
#endif
    s_adv_scan_started = 0;
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    blc_ll_setExtScanEnable(BLC_SCAN_DISABLE, DUP_FILTER_DISABLE, SCAN_DURATION_CONTINUOUS, SCAN_WINDOW_CONTINUOUS);
#endif
    return APP_OK;
}

app_status_t app_ble_update_ext_adv_data(const u8 *data, u8 len)
{
#if PENDANT_EXT_ADV_ENABLE
    ble_sts_t st;
#endif

    if (!data || !len) {
        return APP_ERR_PARAM;
    }

#if !PENDANT_EXT_ADV_ENABLE
    return APP_OK;
#else
    st = blc_ll_setExtAdvData(APP_BLE_PENDANT_ADV_HANDLE, len, data);
    if (st != BLE_SUCCESS && s_adv_scan_started) {
        ble_sts_t retry_st;
        blc_ll_setExtAdvEnable(BLC_ADV_DISABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
        retry_st = blc_ll_setExtAdvData(APP_BLE_PENDANT_ADV_HANDLE, len, data);
        blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
        if (s_adv_update_error_log_count < 4) {
            u_printf("[BLE] adv data retry\r\n");
            u_printf(" st=");
            u_printf("%x\r\n", st);
            u_printf(" retry=");
            u_printf("%x\r\n", retry_st);
            s_adv_update_error_log_count++;
        }
        st = retry_st;
    }

    if (st != BLE_SUCCESS) {
        return APP_ERR_STATE;
    }
    return APP_OK;
#endif
}

app_status_t app_ble_disconnect_app(u8 reason)
{
    if (!s_conn.connected) {
        return APP_OK;
    }
    return blc_ll_disconnect(s_conn.conn_handle, reason) == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
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
