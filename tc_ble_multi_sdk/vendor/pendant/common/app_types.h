#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include "tl_common.h"

#define APP_EID_LEN                         16
#define APP_UNIQUE_ID_LEN                   16
#define APP_PEER_MAX_COUNT                  8
#define APP_EVENT_DATA_MAX_LEN              32
#define APP_ADV_COMPANY_ID_DEV              0xffff
#define APP_ADV_MAGIC_LO                    0x44
#define APP_ADV_MAGIC_HI                    0x50
#define APP_ADV_PROTOCOL_VERSION            0x01
#define APP_ADV_HEADER_LEN                  50
#define APP_ADV_AD_OVERHEAD_LEN             4
#define APP_ADV_FRAME_CRC_LEN               4
#define APP_ADV_FRAME_MAX_LEN               251
#define APP_ADV_PAYLOAD_MAX_LEN             (APP_ADV_FRAME_MAX_LEN - APP_ADV_AD_OVERHEAD_LEN - APP_ADV_HEADER_LEN - APP_ADV_FRAME_CRC_LEN)

typedef enum {
    APP_OK = 0,
    APP_ERR_PARAM = 1,
    APP_ERR_STATE = 2,
    APP_ERR_BUSY = 3,
    APP_ERR_NO_MEM = 4,
    APP_ERR_TIMEOUT = 5,
    APP_ERR_CRC = 6,
    APP_ERR_FLASH = 7,
    APP_ERR_UNSUPPORTED = 8,
    APP_ERR_PERMISSION = 9,
    APP_ERR_NOT_FOUND = 10,
} app_status_t;

typedef enum {
    SYS_STATE_BOOT = 0,
    SYS_STATE_SELF_CHECK,
    SYS_STATE_ADV_SCAN,
    SYS_STATE_APP_CONNECTED,
    SYS_STATE_SLEEP_PREPARE,
    SYS_STATE_SLEEP,
    SYS_STATE_ERROR,
} app_system_state_t;

typedef enum {
    PEER_LEVEL_NONE = 0,
    PEER_LEVEL_S1 = 1,
    PEER_LEVEL_S2 = 2,
    PEER_LEVEL_S3 = 3,
    PEER_LEVEL_LOST = 4,
} app_peer_level_t;

typedef enum {
    APP_EVT_BOOT_DONE = 0,
    APP_EVT_SELF_CHECK_OK,
    APP_EVT_SELF_CHECK_FAIL,
    APP_EVT_APP_CONNECTED,
    APP_EVT_APP_DISCONNECTED,
    APP_EVT_APP_COMMAND_RX,
    APP_EVT_APP_IDLE_TIMEOUT,
    APP_EVT_ADV_REPORT_RX,
    APP_EVT_PEER_FOUND,
    APP_EVT_PEER_LEVEL_CHANGED,
    APP_EVT_PEER_MESSAGE_RX,
    APP_EVT_PEER_MESSAGE_TX_DONE,
    APP_EVT_IDLE_TIMEOUT,
    APP_EVT_MOTION_DETECTED,
    APP_EVT_CHARGE_STARTED,
    APP_EVT_CHARGE_STOPPED,
    APP_EVT_BATTERY_LOW,
    APP_EVT_BATTERY_CRITICAL,
    APP_EVT_FATAL_ERROR,
    APP_EVT_SLEEP_READY,
} app_event_id_t;

typedef struct {
    u8 bytes[APP_EID_LEN];
} app_eid_t;

typedef struct {
    u8 bytes[APP_UNIQUE_ID_LEN];
} app_unique_id_t;

typedef struct {
    app_event_id_t id;
    u16 len;
    u8 data[APP_EVENT_DATA_MAX_LEN];
} app_event_t;

typedef struct {
    u8 version;
    s8 rssi_t1;
    s8 rssi_t2;
    s8 rssi_t3;
    u16 tin_ms;
    u16 tout_ms;
    u16 idle_sleep_s;
    u16 app_idle_s;
    u16 adv_interval_ms;
    u16 scan_interval_ms;
    u16 scan_window_ms;
    u8 vibration_enable;
    u8 reliable_msg_default;
    u8 privacy_mode;
    u16 crc16;
} app_runtime_config_t;

static inline u8 app_eid_is_zero(const app_eid_t *eid)
{
    u8 i;
    for (i = 0; i < APP_EID_LEN; i++) {
        if (eid->bytes[i]) {
            return 0;
        }
    }
    return 1;
}

static inline u8 app_eid_equal(const app_eid_t *a, const app_eid_t *b)
{
    u8 i;
    for (i = 0; i < APP_EID_LEN; i++) {
        if (a->bytes[i] != b->bytes[i]) {
            return 0;
        }
    }
    return 1;
}

#endif
