#ifndef APP_EVENT_H_
#define APP_EVENT_H_

#include "../common/app_types.h"

void app_event_init(void);
app_status_t app_event_post(app_event_id_t id, const void *data, u16 len);
app_status_t app_event_fetch(app_event_t *event);
u8 app_event_pending_count(void);

#endif
