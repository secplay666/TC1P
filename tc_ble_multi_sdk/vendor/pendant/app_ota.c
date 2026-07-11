#include "app_ota.h"
#include "ble/app_ble.h"
#include "system/app_system.h"
#include "stack/ble/ble.h"
#include "stack/ble/host/l2cap/l2cap.h"
#include "drivers/B85/driver_ext/mcu_boot.h"
#include "stack/ble/service/ota/ota_server.h"
#include "common/string.h"

static u8 s_ota_active;
static u8 s_ota_last_result;

static void app_ota_on_start(void)
{
    app_ble_conn_info_t info;

    s_ota_active = 1;
    s_ota_last_result = 0xff;

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
    }
}

void app_ota_configure_boot(void)
{
#if (PENDANT_OTA_ENABLE)
    blc_ota_setFirmwareSizeAndBootAddress(PENDANT_OTA_FIRMWARE_MAX_K, MULTI_BOOT_ADDR_0x40000);
#endif
}

void app_ota_init(void)
{
#if (PENDANT_OTA_ENABLE)
    s_ota_active = 0;
    s_ota_last_result = 0xff;

    blc_ota_initOtaServer_module();
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
