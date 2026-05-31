#ifndef APP_HOST_CMD_H_
#define APP_HOST_CMD_H_

#include "../common/app_types.h"

typedef enum {
    HOST_CMD_GET_DEVICE_INFO = 0x01,
    HOST_CMD_GET_SYSTEM_STATE = 0x02,
    HOST_CMD_GET_ADV_FRAME = 0x03,
    HOST_CMD_GET_PEER_TABLE = 0x04,
    HOST_CMD_SET_RSSI_CONFIG = 0x05,
    HOST_CMD_MOTOR_TEST = 0x06,
    HOST_CMD_LOG_ENABLE = 0x07,
    HOST_CMD_DEBUG_RESET_STATS = 0x08,
    HOST_CMD_ENTER_SLEEP = 0x09,
    HOST_CMD_GET_FLASH_MAP = 0x0A,
} app_host_cmd_id_t;

typedef enum {
    HOST_EVENT_PEER_LEVEL = 0x81,
    HOST_EVENT_SYSTEM = 0x82,
    HOST_EVENT_ERROR = 0x83,
} app_host_event_id_t;

void app_host_cmd_init(void);
void app_host_cmd_poll(void);
void app_host_cmd_on_rx_frame(const u8 *data, u8 len);
u8 app_host_cmd_next_tx_seq(void);
void app_host_cmd_log_text(u8 level, const char *tag, const char *msg);
void app_host_cmd_notify_peer_level(const app_eid_t *eid, u8 old_level, u8 new_level, s8 rssi_avg, u8 reason);
void app_host_cmd_notify_error(u16 error_code, u16 detail);

#endif
