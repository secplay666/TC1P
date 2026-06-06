#ifndef APP_HOST_TRANSPORT_H_
#define APP_HOST_TRANSPORT_H_

#include "../common/app_types.h"
#include "../host_frame/app_host_frame.h"

void app_host_transport_init(void);
void app_host_transport_poll(void);
u8 app_host_transport_is_ready(void);
app_status_t app_host_transport_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len);
app_status_t app_host_transport_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len);

#endif
