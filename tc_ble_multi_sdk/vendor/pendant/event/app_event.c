#include "app_event.h"
#include "common/string.h"

#define APP_EVENT_QUEUE_SIZE 8

static app_event_t s_queue[APP_EVENT_QUEUE_SIZE];
static u8 s_head;
static u8 s_tail;
static u8 s_count;

void app_event_init(void)
{
    s_head = 0;
    s_tail = 0;
    s_count = 0;
}

app_status_t app_event_post(app_event_id_t id, const void *data, u16 len)
{
    app_event_t *evt;

    if (len > APP_EVENT_DATA_MAX_LEN) {
        return APP_ERR_PARAM;
    }
    if (s_count >= APP_EVENT_QUEUE_SIZE) {
        return APP_ERR_NO_MEM;
    }

    evt = &s_queue[s_tail];
    evt->id = id;
    evt->len = len;
    if (data && len) {
        memcpy(evt->data, data, len);
    }

    s_tail = (u8)((s_tail + 1) & (APP_EVENT_QUEUE_SIZE - 1));
    s_count++;
    return APP_OK;
}

app_status_t app_event_fetch(app_event_t *event)
{
    if (!event) {
        return APP_ERR_PARAM;
    }
    if (!s_count) {
        return APP_ERR_NOT_FOUND;
    }

    *event = s_queue[s_head];
    s_head = (u8)((s_head + 1) & (APP_EVENT_QUEUE_SIZE - 1));
    s_count--;
    return APP_OK;
}

u8 app_event_pending_count(void)
{
    return s_count;
}
