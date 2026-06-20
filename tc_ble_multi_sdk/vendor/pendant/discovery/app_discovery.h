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

typedef struct {
    u32 beacon_rx;
    u32 alloc_ok;
    u32 alloc_fail;
    u32 poll_count;
    u8 peer_count;
    u8 last_eid0;
    u8 last_eid1;
    s8 last_rssi;
    s8 last_avg;
    u8 last_target_level;
    u8 last_peer_level;
    u16 last_tin_ms;
    u16 last_tout_ms;
} app_discovery_debug_t;

void app_discovery_init(void);
void app_discovery_on_beacon(const app_eid_t *eid, s8 rssi, u32 now_tick);
void app_discovery_poll(u32 now_tick);
void app_discovery_reset_peer(const app_eid_t *eid);
void app_discovery_get_debug(app_discovery_debug_t *debug);

#endif
