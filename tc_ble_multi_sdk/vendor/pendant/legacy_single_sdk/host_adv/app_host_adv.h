#ifndef APP_HOST_ADV_H_
#define APP_HOST_ADV_H_

#include "../common/app_types.h"
#include "../adv_proto/app_adv_proto.h"
#include "../host_frame/app_host_frame.h"

#define APP_HOST_ADV_VERSION            0x01
#define APP_HOST_ADV_MAGIC_LO           0x48
#define APP_HOST_ADV_MAGIC_HI           0x41
#define APP_HOST_ADV_HEADER_LEN         14
#define APP_HOST_ADV_CHUNK_MAX_LEN      (APP_ADV_PAYLOAD_MAX_LEN - APP_HOST_ADV_HEADER_LEN)

void app_host_adv_init(void);
void app_host_adv_poll(void);
void app_host_adv_on_adv_frame(const app_adv_frame_t *frame, s8 rssi);
u8 app_host_adv_is_ready(void);
app_status_t app_host_adv_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len);
app_status_t app_host_adv_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len);

#endif
