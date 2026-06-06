#ifndef APP_PEER_TABLE_H_
#define APP_PEER_TABLE_H_

#include "../common/app_types.h"

typedef struct {
    app_eid_t eid;
    u32 short_id;
    app_peer_level_t level;
    s8 rssi;
    s8 rssi_avg;
    u32 first_seen_tick;
    u32 last_seen_tick;
    u16 capability_flags;
    u8 flags;
    u8 in_use;
    u32 enter_start_tick;
    u32 exit_start_tick;
} app_peer_record_t;

void app_peer_table_init(void);
void app_peer_table_clear(void);
app_peer_record_t *app_peer_table_find(const app_eid_t *eid);
app_peer_record_t *app_peer_table_find_or_alloc(const app_eid_t *eid);
app_status_t app_peer_table_remove(const app_eid_t *eid);
u8 app_peer_table_count(void);
u8 app_peer_table_copy(app_peer_record_t *out, u8 max_count);
void app_peer_table_poll_timeout(u32 now_tick);

#endif
