#include "app_peer_table.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define PEER_TIMEOUT_US 30000000

static app_peer_record_t s_peers[APP_PEER_MAX_COUNT];

void app_peer_table_init(void)
{
    memset(s_peers, 0, sizeof(s_peers));
}

void app_peer_table_clear(void)
{
    memset(s_peers, 0, sizeof(s_peers));
}

app_peer_record_t *app_peer_table_find(const app_eid_t *eid)
{
    u8 i;
    if (!eid) {
        return 0;
    }
    for (i = 0; i < APP_PEER_MAX_COUNT; i++) {
        if (s_peers[i].in_use && app_eid_equal(&s_peers[i].eid, eid)) {
            return &s_peers[i];
        }
    }
    return 0;
}

app_peer_record_t *app_peer_table_find_by_short_id(u32 short_id)
{
    u8 i;
    if (!short_id) {
        return 0;
    }
    for (i = 0; i < APP_PEER_MAX_COUNT; i++) {
        if (s_peers[i].in_use && s_peers[i].short_id == short_id) {
            return &s_peers[i];
        }
    }
    return 0;
}

app_peer_record_t *app_peer_table_find_or_alloc(const app_eid_t *eid)
{
    u8 i;
    app_peer_record_t *peer;
    if (!eid || app_eid_is_zero(eid)) {
        return 0;
    }
    peer = app_peer_table_find(eid);
    if (peer) {
        return peer;
    }
    for (i = 0; i < APP_PEER_MAX_COUNT; i++) {
        if (!s_peers[i].in_use) {
            memset(&s_peers[i], 0, sizeof(s_peers[i]));
            s_peers[i].in_use = 1;
            s_peers[i].eid = *eid;
            s_peers[i].level = PEER_LEVEL_NONE;
            s_peers[i].first_seen_tick = clock_time();
            s_peers[i].last_seen_tick = s_peers[i].first_seen_tick;
            s_peers[i].short_id = ((u32)eid->bytes[0]) |
                                  ((u32)eid->bytes[1] << 8) |
                                  ((u32)eid->bytes[2] << 16) |
                                  ((u32)eid->bytes[3] << 24);
            return &s_peers[i];
        }
    }
    return 0;
}

app_status_t app_peer_table_remove(const app_eid_t *eid)
{
    app_peer_record_t *peer = app_peer_table_find(eid);
    if (!peer) {
        return APP_ERR_NOT_FOUND;
    }
    memset(peer, 0, sizeof(*peer));
    return APP_OK;
}

u8 app_peer_table_count(void)
{
    u8 i;
    u8 count = 0;
    for (i = 0; i < APP_PEER_MAX_COUNT; i++) {
        if (s_peers[i].in_use) {
            count++;
        }
    }
    return count;
}

u8 app_peer_table_copy(app_peer_record_t *out, u8 max_count)
{
    u8 i;
    u8 count = 0;
    if (!out) {
        return 0;
    }
    for (i = 0; i < APP_PEER_MAX_COUNT && count < max_count; i++) {
        if (s_peers[i].in_use) {
            out[count++] = s_peers[i];
        }
    }
    return count;
}

void app_peer_table_poll_timeout(u32 now_tick)
{
    u8 i;
    (void)now_tick;
    for (i = 0; i < APP_PEER_MAX_COUNT; i++) {
        if (s_peers[i].in_use && clock_time_exceed(s_peers[i].last_seen_tick, PEER_TIMEOUT_US)) {
            memset(&s_peers[i], 0, sizeof(s_peers[i]));
        }
    }
}
