#ifndef APP_ADV_PROTO_H_
#define APP_ADV_PROTO_H_

#include "../common/app_types.h"

typedef enum {
    ADV_FRAME_BEACON = 0x01,
    ADV_FRAME_DATA = 0x10,
    ADV_FRAME_ACK = 0x11,
    ADV_FRAME_CTRL = 0x12,
    ADV_FRAME_ERROR = 0x13,
} app_adv_frame_type_t;

typedef struct {
    app_adv_frame_type_t type;
    u8 flags;
    u8 key_id;
    u8 device_state;
    u16 frame_seq;
    app_eid_t src_eid;
    app_eid_t dst_eid;
    u32 message_id;
    u8 fragment_index;
    u8 fragment_count;
    const u8 *payload;
    u8 payload_len;
} app_adv_frame_t;

void app_adv_proto_init(void);
app_status_t app_adv_proto_encode(const app_adv_frame_t *frame, u8 *out, u8 out_max, u8 *out_len);
app_status_t app_adv_proto_decode(const u8 *data, u8 len, app_adv_frame_t *frame);
u8 app_adv_proto_is_manufacturer_payload(const u8 *ad_data, u8 ad_len, const u8 **payload, u8 *payload_len);

#endif
