#ifndef APP_ADV_SCHEDULER_H_
#define APP_ADV_SCHEDULER_H_

#include "../common/app_types.h"
#include "../adv_proto/app_adv_proto.h"

typedef struct {
    u32 build_ok;
    u32 build_fail;
    u32 beacon_build_ok;
    u32 data_build_ok;
    u32 enqueue_ok;
    u32 enqueue_full;
    u8 queue_count;
    u8 last_status;
    u8 last_adv_len;
    u8 max_adv_len;
    u8 last_type;
    u8 last_payload_len;
    u8 last_data_adv_len;
    u8 last_data_payload_len;
} app_adv_scheduler_debug_t;

void app_adv_scheduler_init(void);
void app_adv_scheduler_poll(void);
app_status_t app_adv_scheduler_request_beacon_update(void);
app_status_t app_adv_scheduler_enqueue_frame(const app_adv_frame_t *frame);
app_status_t app_adv_scheduler_build_next_adv_data(u8 *buf, u8 max_len, u8 *out_len);
void app_adv_scheduler_debug_reset(void);
void app_adv_scheduler_get_debug(app_adv_scheduler_debug_t *debug);

#endif
