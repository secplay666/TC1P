#ifndef APP_PEER_TRANSPORT_H_
#define APP_PEER_TRANSPORT_H_

#include "../common/app_types.h"
#include "../adv_proto/app_adv_proto.h"

#define APP_PEER_TRANSPORT_VERSION              0x01
#define APP_PEER_TRANSPORT_MAGIC_LO             0x50
#define APP_PEER_TRANSPORT_MAGIC_HI             0x54
#define APP_PEER_TRANSPORT_HEADER_LEN           8
#define APP_PEER_TRANSPORT_PAYLOAD_MAX_LEN      (APP_ADV_PAYLOAD_MAX_LEN - APP_PEER_TRANSPORT_HEADER_LEN)

typedef enum {
    APP_PEER_MSG_TEST = 1,
    APP_PEER_MSG_USER = 2,
} app_peer_msg_type_t;

typedef struct {
    u32 tx_ok;
    u32 tx_fail;
    u32 rx_total;
    u32 rx_accept;
    u32 rx_drop;
    u32 rx_dup;
    u32 rx_broadcast;
    u32 rx_direct;
    u16 last_tx_seq;
    u16 last_rx_seq;
    u8 last_status;
    u8 last_tx_type;
    u8 last_tx_len;
    u8 last_rx_type;
    u8 last_rx_len;
    u8 last_rx_src0;
    u8 last_rx_src1;
    s8 last_rssi;
    u8 max_payload_len;
} app_peer_transport_debug_t;

void app_peer_transport_init(void);
void app_peer_transport_poll(void);
app_status_t app_peer_transport_send(const app_eid_t *dst_eid, app_peer_msg_type_t type,
                                     const u8 *payload, u8 len);
u8 app_peer_transport_on_adv_frame(const app_adv_frame_t *frame, s8 rssi);
void app_peer_transport_debug_reset(void);
void app_peer_transport_get_debug(app_peer_transport_debug_t *debug);

#endif
