#ifndef APP_HOST_GATT_H_
#define APP_HOST_GATT_H_

#include "../common/app_types.h"
#include "../host_frame/app_host_frame.h"

#define APP_HOST_GATT_SERVICE_UUID_STR "50544E44-0001-4B45-5931-444556000001"
#define APP_HOST_GATT_CMD_UUID_STR     "50544E44-0002-4B45-5931-444556000001"
#define APP_HOST_GATT_RSP_UUID_STR     "50544E44-0003-4B45-5931-444556000001"
#define APP_HOST_GATT_LOG_UUID_STR     "50544E44-0004-4B45-5931-444556000001"
#define APP_HOST_GATT_EVT_UUID_STR     "50544E44-0005-4B45-5931-444556000001"

void app_host_gatt_init(void);
void app_host_gatt_poll(void);
void app_host_gatt_on_connected(u16 conn_handle);
void app_host_gatt_on_disconnected(void);
u8 app_host_gatt_is_ready(void);
app_status_t app_host_gatt_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len);
app_status_t app_host_gatt_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len);

#endif
