#ifndef APP_DISCOVERY_H_
#define APP_DISCOVERY_H_

#include "../common/app_types.h"

typedef struct {
    app_eid_t peer_eid;
    app_peer_level_t old_level;
    app_peer_level_t new_level;
    s8 rssi_avg;
    u8 reason;
} app_discovery_event_t;

void app_discovery_init(void);
void app_discovery_on_beacon(const app_eid_t *eid, s8 rssi, u32 now_tick);
void app_discovery_poll(u32 now_tick);
void app_discovery_reset_peer(const app_eid_t *eid);

#endif
