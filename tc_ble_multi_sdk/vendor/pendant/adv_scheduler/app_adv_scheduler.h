#ifndef APP_ADV_SCHEDULER_H_
#define APP_ADV_SCHEDULER_H_

#include "../common/app_types.h"
#include "../adv_proto/app_adv_proto.h"

void app_adv_scheduler_init(void);
void app_adv_scheduler_poll(void);
app_status_t app_adv_scheduler_request_beacon_update(void);
app_status_t app_adv_scheduler_enqueue_frame(const app_adv_frame_t *frame);
app_status_t app_adv_scheduler_build_next_adv_data(u8 *buf, u8 max_len, u8 *out_len);
void app_adv_scheduler_debug_reset(void);

#endif
