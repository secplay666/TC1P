#include "app_ble.h"
#include "../app_config.h"
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
#define APP_BLE_PENDANT_ADV_SID_A   ADV_SID_1
#define APP_BLE_PENDANT_ADV_SID_B   ADV_SID_2
#define APP_BLE_SCAN_FILTER_DUP     DUPE_FLTR_DISABLE
#define APP_BLE_SCAN_DURATION       SCAN_DURATION_CONTINUOUS
#define APP_BLE_SCAN_PERIOD         SCAN_WINDOW_CONTINUOUS

static app_ble_conn_info_t s_conn;
static u8 s_adv_scan_started;
static u8 s_adv_update_error_log_count;
static u8 s_scan_filter_policy;
static u8 s_adv1_sid;
static app_ble_debug_t s_debug;

static void app_ble_refresh_started(void)
{
    s_adv_scan_started = s_debug.adv0_enabled || s_debug.adv1_enabled || s_debug.scan_enabled;
    s_debug.started = s_adv_scan_started;
}

static ble_sts_t app_ble_configure_ext_scan(void)
{
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    return blc_ll_setExtScanParam(OWN_ADDRESS_PUBLIC, s_scan_filter_policy, SCAN_PHY_1M,
                                  SCAN_TYPE_ACTIVE, SCAN_INTERVAL_100MS, SCAN_WINDOW_100MS,
                                  0, 0, 0);
#else
    return BLE_SUCCESS;
#endif
}

static ble_sts_t app_ble_configure_adv1(u8 sid)
{
#if PENDANT_EXT_ADV_ENABLE
    return blc_ll_setExtAdvParam(APP_BLE_PENDANT_ADV_HANDLE,
                                 ADV_EVT_PROP_EXTENDED_NON_CONNECTABLE_NON_SCANNABLE_UNDIRECTED,
                                 ADV_INTERVAL_100MS, ADV_INTERVAL_100MS, BLT_ENABLE_ADV_ALL,
                                 OWN_ADDRESS_PUBLIC, BLE_ADDR_PUBLIC, NULL, ADV_FP_NONE,
                                 TX_POWER_3dBm, BLE_PHY_1M, 0, BLE_PHY_1M, sid, 0);
#else
    (void)sid;
    return BLE_SUCCESS;
#endif
}

void app_ble_init(void)
{
    memset(&s_conn, 0, sizeof(s_conn));
    memset(&s_debug, 0, sizeof(s_debug));
    s_scan_filter_policy = SCAN_FP_ALLOW_ADV_ANY;
    s_adv1_sid = APP_BLE_PENDANT_ADV_SID_A;
    s_debug.adv1_sid = s_adv1_sid;
    s_debug.scan_filter_policy = s_scan_filter_policy;
    s_adv_scan_started = 0;
    s_adv_update_error_log_count = 0;
}

app_status_t app_ble_start_adv_scan(const app_ble_params_t *params)
{
    (void)params;
    app_ble_set_adv0_enabled(1);
    app_ble_set_adv1_enabled(1);
    app_ble_set_scan_enabled(1);
    u_printf("[BLE] start\r\n");
    u_printf(" adv0=");
    u_printf("%x\r\n", s_debug.adv0_status);
    u_printf(" adv1=");
    u_printf("%x\r\n", s_debug.adv1_status);
    u_printf(" scan=");
    u_printf("%x\r\n", s_debug.scan_status);
    return APP_OK;
}

app_status_t app_ble_set_adv0_enabled(u8 enable)
{
    ble_sts_t st;
#if PENDANT_EXT_ADV_ENABLE
    st = blc_ll_setExtAdvEnable(enable ? BLC_ADV_ENABLE : BLC_ADV_DISABLE, APP_BLE_APP_ADV_HANDLE, 0, 0);
#else
    st = blc_ll_setAdvEnable(enable ? BLC_ADV_ENABLE : BLC_ADV_DISABLE);
#endif
    s_debug.adv0_status = st;
    s_debug.adv0_enabled = enable && st == BLE_SUCCESS;
    app_ble_refresh_started();
    return st == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

app_status_t app_ble_set_adv1_enabled(u8 enable)
{
    ble_sts_t st = BLE_SUCCESS;
#if PENDANT_EXT_ADV_ENABLE
    if (enable && s_debug.adv1_enabled) {
        s_debug.adv1_status = BLE_SUCCESS;
        app_ble_refresh_started();
        return APP_OK;
    }
    if (enable) {
        s_debug.adv1_param_status = app_ble_configure_adv1(s_adv1_sid);
        if (s_debug.adv1_param_status != BLE_SUCCESS) {
            s_debug.adv1_status = s_debug.adv1_param_status;
            s_debug.adv1_enabled = 0;
            app_ble_refresh_started();
            return APP_ERR_STATE;
        }
    }
    st = blc_ll_setExtAdvEnable(enable ? BLC_ADV_ENABLE : BLC_ADV_DISABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
#endif
    s_debug.adv1_status = st;
    s_debug.adv1_enabled = enable && st == BLE_SUCCESS;
    app_ble_refresh_started();
    return st == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

app_status_t app_ble_set_scan_enabled(u8 enable)
{
    ble_sts_t st = BLE_SUCCESS;
#if APP_BLE_ENABLE_DISCOVERY_SCAN
    if (enable) {
        if (s_debug.scan_enabled) {
            blc_ll_setExtScanEnable(BLC_SCAN_DISABLE,
                                    DUP_FILTER_DISABLE,
                                    SCAN_DURATION_CONTINUOUS,
                                    SCAN_WINDOW_CONTINUOUS);
            s_debug.scan_enabled = 0;
        }
        st = app_ble_configure_ext_scan();
        s_debug.scan_param_status = st;
        if (st != BLE_SUCCESS) {
            s_debug.scan_status = st;
            app_ble_refresh_started();
            return APP_ERR_STATE;
        }
    }
    st = blc_ll_setExtScanEnable(enable ? BLC_SCAN_ENABLE : BLC_SCAN_DISABLE,
                                 enable ? APP_BLE_SCAN_FILTER_DUP : DUP_FILTER_DISABLE,
                                 enable ? APP_BLE_SCAN_DURATION : SCAN_DURATION_CONTINUOUS,
                                 enable ? APP_BLE_SCAN_PERIOD : SCAN_WINDOW_CONTINUOUS);
#else
    (void)enable;
#endif
    s_debug.scan_status = st;
    s_debug.scan_enabled = enable && st == BLE_SUCCESS;
    s_debug.scan_filter_policy = s_scan_filter_policy;
    app_ble_refresh_started();
    return st == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

app_status_t app_ble_set_scan_whitelist_enabled(u8 enable)
{
    s_scan_filter_policy = enable ? SCAN_FP_ALLOW_ADV_WL : SCAN_FP_ALLOW_ADV_ANY;
    s_debug.scan_filter_policy = s_scan_filter_policy;
    return APP_OK;
}

app_status_t app_ble_whitelist_clear(void)
{
    return blc_ll_clearWhiteList() == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

app_status_t app_ble_whitelist_add_public(const u8 *addr)
{
    if (!addr) {
        return APP_ERR_PARAM;
    }
    return blc_ll_addDeviceToWhiteList(BLE_ADDR_PUBLIC, (u8 *)addr) == BLE_SUCCESS ? APP_OK : APP_ERR_STATE;
}

app_status_t app_ble_stop_adv_scan(void)
{
    app_ble_set_scan_enabled(0);
    app_ble_set_adv1_enabled(0);
    app_ble_set_adv0_enabled(0);
    app_ble_refresh_started();
    return APP_OK;
}

app_status_t app_ble_update_ext_adv_data(const u8 *data, u8 len)
{
#if PENDANT_EXT_ADV_ENABLE
    ble_sts_t st;
    ble_sts_t param_st = BLE_SUCCESS;
#endif

    if (!data || !len) {
        return APP_ERR_PARAM;
    }

#if !PENDANT_EXT_ADV_ENABLE
    return APP_OK;
#else
    if (s_debug.adv1_enabled) {
        blc_ll_setExtAdvEnable(BLC_ADV_DISABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
        st = blc_ll_setExtAdvData(APP_BLE_PENDANT_ADV_HANDLE, len, data);
        blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, APP_BLE_PENDANT_ADV_HANDLE, 0, 0);
    } else {
        st = blc_ll_setExtAdvData(APP_BLE_PENDANT_ADV_HANDLE, len, data);
    }
    s_debug.adv1_param_status = param_st;
    s_debug.adv1_sid = s_adv1_sid;
    s_debug.last_adv_update_status = st;
    if ((st != BLE_SUCCESS || param_st != BLE_SUCCESS) && s_debug.adv1_enabled) {
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
        s_debug.adv1_param_status = param_st;
        s_debug.last_adv_update_status = st;
    }

    if (st != BLE_SUCCESS || param_st != BLE_SUCCESS) {
        s_debug.adv_update_fail++;
        return APP_ERR_STATE;
    }
    s_debug.adv_update_ok++;
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

void app_ble_get_debug(app_ble_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
        debug->connected = s_conn.connected;
        debug->started = s_adv_scan_started;
    }
}

void app_ble_poll(void)
{
}

void app_ble_on_connected(const u8 *peer_addr, u16 conn_handle)
{
    s_conn.connected = 1;
    s_debug.connected = 1;
    s_conn.conn_handle = conn_handle;
    if (peer_addr) {
        memcpy(s_conn.peer_addr, peer_addr, sizeof(s_conn.peer_addr));
    }
    app_event_post(APP_EVT_APP_CONNECTED, 0, 0);
}

void app_ble_on_disconnected(u8 reason)
{
    ble_sts_t adv_st;

    s_conn.connected = 0;
    s_debug.connected = 0;
    s_conn.conn_handle = 0;
    if (s_adv_scan_started) {
#if PENDANT_EXT_ADV_ENABLE
        adv_st = blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, APP_BLE_APP_ADV_HANDLE, 0, 0);
#else
        adv_st = blc_ll_setAdvEnable(BLC_ADV_ENABLE);
#endif
        s_debug.adv0_status = adv_st;
    }
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
