#ifndef APP_HOST_FRAME_H_
#define APP_HOST_FRAME_H_

#include "../common/app_types.h"

#define APP_HOST_FRAME_VERSION          0x01
#define APP_HOST_FRAME_MAGIC            0xA5
#define APP_HOST_FRAME_MAX_PACKET_LEN   20
#define APP_HOST_FRAME_HEADER_LEN       9
#define APP_HOST_FRAME_CRC_LEN          2
#define APP_HOST_FRAME_CHUNK_MAX_LEN    (APP_HOST_FRAME_MAX_PACKET_LEN - APP_HOST_FRAME_HEADER_LEN - APP_HOST_FRAME_CRC_LEN)
#define APP_HOST_MESSAGE_MAX_LEN        192

typedef enum {
    HOST_FRAME_TYPE_CMD = 1,
    HOST_FRAME_TYPE_RSP = 2,
    HOST_FRAME_TYPE_LOG = 3,
    HOST_FRAME_TYPE_EVENT = 4,
} app_host_frame_type_t;

typedef enum {
    HOST_STATUS_OK = 0,
    HOST_STATUS_ERR_PARAM = 1,
    HOST_STATUS_ERR_STATE = 2,
    HOST_STATUS_ERR_BUSY = 3,
    HOST_STATUS_ERR_UNSUPPORTED = 4,
    HOST_STATUS_ERR_CRC = 5,
    HOST_STATUS_ERR_NO_MEM = 6,
    HOST_STATUS_ERR_PERMISSION = 7,
    HOST_STATUS_ERR_NOT_FOUND = 8,
    HOST_STATUS_ERR_FLASH = 9,
} app_host_status_t;

typedef struct {
    app_host_frame_type_t type;
    u8 seq;
    u8 cmd;
    u8 status;
    u8 frag_index;
    u8 frag_count;
    const u8 *payload;
    u8 payload_len;
} app_host_frame_tx_t;

typedef struct {
    app_host_frame_type_t type;
    u8 seq;
    u8 cmd;
    u8 status;
    u8 frag_index;
    u8 frag_count;
    u8 payload[APP_HOST_FRAME_CHUNK_MAX_LEN];
    u8 payload_len;
} app_host_frame_rx_t;

app_status_t app_host_frame_encode(const app_host_frame_tx_t *frame, u8 *out, u8 out_max, u8 *out_len);
app_status_t app_host_frame_decode(const u8 *data, u8 len, app_host_frame_rx_t *frame);

#endif
