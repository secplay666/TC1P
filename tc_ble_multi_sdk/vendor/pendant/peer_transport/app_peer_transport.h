#ifndef APP_PEER_TRANSPORT_H_
#define APP_PEER_TRANSPORT_H_

#include "../common/app_types.h"
#include "../adv_proto/app_adv_proto.h"

#define APP_PEER_TRANSPORT_VERSION_LEGACY       0x01
#define APP_PEER_TRANSPORT_VERSION              0x02
#define APP_PEER_TRANSPORT_MAGIC_LO             0x50
#define APP_PEER_TRANSPORT_MAGIC_HI             0x54
#define APP_PEER_TRANSPORT_HEADER_LEN           8
#define APP_PEER_TRANSPORT_RX_ADV_MAX_LEN       229
#define APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN      (APP_PEER_TRANSPORT_RX_ADV_MAX_LEN - APP_ADV_AD_OVERHEAD_LEN - APP_ADV_HEADER_LEN - APP_ADV_FRAME_CRC_LEN - APP_PEER_TRANSPORT_HEADER_LEN)
#define APP_PEER_TRANSPORT_FRAGMENT_BODY_MAX_LEN APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN
#define APP_PEER_TRANSPORT_MAX_FRAGMENTS        2
#define APP_PEER_TRANSPORT_MESSAGE_MAX_LEN      256
#define APP_PEER_TRANSPORT_ACK_BODY_LEN         8

#define APP_PEER_TRANSPORT_FRAME_FLAG_RELIABLE  0x01
#define APP_PEER_TRANSPORT_FRAME_FLAG_NOTIFY    0x02
#define APP_PEER_TRANSPORT_FRAME_FLAG_PRIORITY  0x04
#define APP_PEER_TRANSPORT_ADV_FLAG_ACK_REQ     0x01

typedef enum {
    APP_PEER_SEND_UNRELIABLE = 0,
    APP_PEER_SEND_RELIABLE = 1,
} app_peer_send_mode_t;

typedef enum {
    APP_PEER_ACK_PARTIAL = 0,
    APP_PEER_ACK_COMPLETE = 1,
    APP_PEER_ACK_DUP_COMPLETE = 2,
    APP_PEER_ACK_BUSY = 3,
    APP_PEER_ACK_REJECTED = 4,
    APP_PEER_ACK_TIMEOUT = 5,
    APP_PEER_ACK_CANCELED = 6,
} app_peer_ack_status_t;

typedef enum {
    APP_PEER_MSG_TEST = 1,
    APP_PEER_MSG_USER = 2,
} app_peer_msg_type_t;

typedef struct {
    u32 tx_ok;
    u32 tx_fail;
    u32 tx_msg_ok;
    u32 tx_msg_fail;
    u32 tx_frag_sent;
    u32 tx_frag_retx;
    u32 tx_ack_rx;
    u32 tx_ack_match;
    u32 tx_ack_timeout;
    u32 rx_total;
    u32 rx_accept;
    u32 rx_drop;
    u32 rx_dup;
    u32 rx_broadcast;
    u32 rx_direct;
    u32 rx_msg_ok;
    u32 rx_msg_timeout;
    u32 rx_frag_new;
    u32 rx_ack_tx;
    u32 rx_busy;
    u32 rx_rejected;
    u32 rx_debug_drop;
    u16 last_tx_seq;
    u16 last_rx_seq;
    u16 last_tx_msg_len;
    u16 last_rx_msg_len;
    u8 last_status;
    u8 last_tx_type;
    u8 last_tx_len;
    u8 last_rx_type;
    u8 last_rx_len;
    u8 last_rx_src0;
    u8 last_rx_src1;
    s8 last_rssi;
    u8 max_payload_len;
    u8 max_fragments;
    u8 tx_active;
    u8 tx_frag_count;
    u8 tx_pending;
    u8 tx_ack_bits;
    u8 tx_retry_round;
    u8 last_ack_status;
    u8 last_ack_type;
    u8 last_ack_frag_count;
    u8 last_ack_bitmap;
    u8 last_ack_match_flags;
    u8 last_ack_src0;
    u8 last_ack_src1;
    u8 rx_active;
    u8 rx_frag_count;
    u8 rx_bitmap;
    u8 rx_drop_mask;
} app_peer_transport_debug_t;

typedef void (*app_peer_transport_rx_cb_t)(const app_eid_t *src_eid,
                                           app_peer_msg_type_t type,
                                           const u8 *payload,
                                           u16 len,
                                           s8 rssi);
typedef void (*app_peer_transport_tx_cb_t)(const app_eid_t *dst_eid,
                                           app_peer_msg_type_t type,
                                           u32 message_id,
                                           u16 len,
                                           app_status_t status,
                                           u8 flags);

void app_peer_transport_init(void);
void app_peer_transport_poll(void);
app_status_t app_peer_transport_send(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                     const u8 *payload, u8 len);
app_status_t app_peer_transport_send_message(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                             const u8 *payload, u16 len,
                                             app_peer_send_mode_t mode, u8 flags);
app_status_t app_peer_transport_send_test_pattern(const app_eid_t *dst_eid,
                                                  app_peer_msg_type_t type,
                                                  u16 len,
                                                  app_peer_send_mode_t mode,
                                                  u8 flags);
void app_peer_transport_set_rx_callback(app_peer_transport_rx_cb_t cb);
void app_peer_transport_set_tx_callback(app_peer_transport_tx_cb_t cb);
void app_peer_transport_debug_drop_next_rx(u32 fragment_bitmap);
u8 app_peer_transport_on_adv_frame(const app_adv_frame_t *frame, s8 rssi);
void app_peer_transport_debug_reset(void);
void app_peer_transport_get_debug(app_peer_transport_debug_t *debug);

#endif
