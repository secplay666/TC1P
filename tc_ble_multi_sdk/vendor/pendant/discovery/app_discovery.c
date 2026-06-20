#include "app_discovery.h"
#include "../config/app_config_store.h"
#include "../peer_table/app_peer_table.h"
#include "../event/app_event.h"
#include "../motor/app_motor.h"
#include "../host_cmd/app_host_cmd.h"
#include "common/string.h"
#include "drivers.h"
#include "timer.h"

#define DISCOVERY_REASON_ENTER 1
#define DISCOVERY_REASON_EXIT  2

static app_discovery_debug_t s_debug;

static app_peer_level_t level_for_rssi(s8 rssi)
{
    const app_runtime_config_t *cfg = app_config_get();
    if (rssi >= cfg->rssi_t3) {
        return PEER_LEVEL_S3;
    }
    if (rssi >= cfg->rssi_t2) {
        return PEER_LEVEL_S2;
    }
    if (rssi >= cfg->rssi_t1) {
        return PEER_LEVEL_S1;
    }
    return PEER_LEVEL_NONE;
}

static void notify_level_change(app_peer_record_t *peer, app_peer_level_t old_level, app_peer_level_t new_level, u8 reason)
{
    app_discovery_event_t event;
    event.peer_eid = peer->eid;
    event.old_level = old_level;
    event.new_level = new_level;
    event.rssi_avg = peer->rssi_avg;
    event.reason = reason;
    app_event_post(new_level == PEER_LEVEL_NONE ? APP_EVT_PEER_LEVEL_CHANGED : APP_EVT_PEER_FOUND,
                   &event,
                   sizeof(event) <= APP_EVENT_DATA_MAX_LEN ? sizeof(event) : APP_EVENT_DATA_MAX_LEN);
    app_host_cmd_notify_peer_level(&peer->eid, (u8)old_level, (u8)new_level, peer->rssi_avg, reason);

    app_peer_level_t pulse_level = new_level > old_level ? new_level : old_level;
    if (pulse_level == PEER_LEVEL_S1) {
        app_motor_play(MOTOR_PATTERN_ONE);
    } else if (pulse_level == PEER_LEVEL_S2) {
        app_motor_play(MOTOR_PATTERN_TWO);
    } else if (pulse_level == PEER_LEVEL_S3) {
        app_motor_play(MOTOR_PATTERN_THREE);
    }
}

void app_discovery_init(void)
{
    memset(&s_debug, 0, sizeof(s_debug));
}

void app_discovery_on_beacon(const app_eid_t *eid, s8 rssi, u32 now_tick)
{
    app_peer_record_t *peer;
    app_peer_level_t target_level;
    const app_runtime_config_t *cfg = app_config_get();

    s_debug.beacon_rx++;
    s_debug.last_eid0 = eid ? eid->bytes[0] : 0;
    s_debug.last_eid1 = eid ? eid->bytes[1] : 0;
    s_debug.last_rssi = rssi;
    s_debug.last_tin_ms = cfg->tin_ms;
    s_debug.last_tout_ms = cfg->tout_ms;

    peer = app_peer_table_find_or_alloc(eid);
    if (!peer) {
        s_debug.alloc_fail++;
        s_debug.peer_count = app_peer_table_count();
        return;
    }
    s_debug.alloc_ok++;

    peer->rssi = rssi;
    if (!peer->rssi_avg) {
        peer->rssi_avg = rssi;
    } else {
        peer->rssi_avg = (s8)(((s16)peer->rssi_avg * 3 + rssi) / 4);
    }
    peer->last_seen_tick = now_tick;

    target_level = level_for_rssi(peer->rssi_avg);
    s_debug.last_avg = peer->rssi_avg;
    s_debug.last_target_level = (u8)target_level;

    if (target_level > peer->level) {
        if (!peer->enter_start_tick) {
            peer->enter_start_tick = now_tick;
        }
        peer->exit_start_tick = 0;
        if (clock_time_exceed(peer->enter_start_tick, (u32)cfg->tin_ms * 1000)) {
            app_peer_level_t old = peer->level;
            peer->level = (app_peer_level_t)(old + 1);
            if (peer->level > target_level) {
                peer->level = target_level;
            }
            peer->enter_start_tick = 0;
            notify_level_change(peer, old, peer->level, DISCOVERY_REASON_ENTER);
        }
    } else if (target_level < peer->level) {
        if (!peer->exit_start_tick) {
            peer->exit_start_tick = now_tick;
        }
        peer->enter_start_tick = 0;
        if (clock_time_exceed(peer->exit_start_tick, (u32)cfg->tout_ms * 1000)) {
            app_peer_level_t old = peer->level;
            peer->level = (app_peer_level_t)(old - 1);
            if (peer->level < target_level) {
                peer->level = target_level;
            }
            peer->exit_start_tick = 0;
            notify_level_change(peer, old, peer->level, DISCOVERY_REASON_EXIT);
            if (peer->level == PEER_LEVEL_NONE) {
                app_peer_table_remove(&peer->eid);
            }
        }
    } else {
        peer->enter_start_tick = 0;
        peer->exit_start_tick = 0;
    }

    s_debug.last_peer_level = (u8)peer->level;
    s_debug.peer_count = app_peer_table_count();
}

void app_discovery_poll(u32 now_tick)
{
    s_debug.poll_count++;
    app_peer_table_poll_timeout(now_tick);
    s_debug.peer_count = app_peer_table_count();
}

void app_discovery_reset_peer(const app_eid_t *eid)
{
    app_peer_table_remove(eid);
    s_debug.peer_count = app_peer_table_count();
}

void app_discovery_get_debug(app_discovery_debug_t *debug)
{
    if (debug) {
        *debug = s_debug;
    }
}
