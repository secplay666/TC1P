#include "app_host_transport.h"
#include "../host_adv/app_host_adv.h"
#include "../host_gatt/app_host_gatt.h"

#ifndef APP_HOST_ENABLE_GATT_TRANSPORT
#define APP_HOST_ENABLE_GATT_TRANSPORT 1
#endif

#ifndef APP_HOST_ENABLE_ADV_TRANSPORT
#define APP_HOST_ENABLE_ADV_TRANSPORT 1
#endif

static u8 s_transport_seq;

static u8 next_seq(void)
{
    u8 seq = s_transport_seq;
    s_transport_seq = (u8)(s_transport_seq >= 255 ? 1 : s_transport_seq + 1);
    return seq;
}

void app_host_transport_init(void)
{
    s_transport_seq = 1;
#if APP_HOST_ENABLE_ADV_TRANSPORT
    app_host_adv_init();
#endif
}

void app_host_transport_poll(void)
{
#if APP_HOST_ENABLE_ADV_TRANSPORT
    app_host_adv_poll();
#endif
}

u8 app_host_transport_is_ready(void)
{
#if APP_HOST_ENABLE_ADV_TRANSPORT
    if (app_host_adv_is_ready()) {
        return 1;
    }
#endif
#if APP_HOST_ENABLE_GATT_TRANSPORT
    if (app_host_gatt_is_ready()) {
        return 1;
    }
#endif
    return 0;
}

app_status_t app_host_transport_send_message_with_seq(app_host_frame_type_t type, u8 seq, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    app_status_t last_status = APP_ERR_STATE;
    u8 sent = 0;

#if APP_HOST_ENABLE_ADV_TRANSPORT
    last_status = app_host_adv_send_message_with_seq(type, seq, cmd, status, payload, len);
    if (last_status == APP_OK) {
        sent = 1;
    }
#endif

#if APP_HOST_ENABLE_GATT_TRANSPORT
    last_status = app_host_gatt_send_message_with_seq(type, seq, cmd, status, payload, len);
    if (last_status == APP_OK) {
        sent = 1;
    }
#endif

    return sent ? APP_OK : last_status;
}

app_status_t app_host_transport_send_message(app_host_frame_type_t type, u8 cmd, u8 status, const u8 *payload, u16 len)
{
    return app_host_transport_send_message_with_seq(type, next_seq(), cmd, status, payload, len);
}
