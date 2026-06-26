#ifndef APP_PROFILE_H_
#define APP_PROFILE_H_

#include "../common/app_types.h"

#define APP_PROFILE_VERSION                 1
#define APP_PROFILE_KEY_LEN                 16
#define APP_PROFILE_NICKNAME_MAX_LEN        18
#define APP_PROFILE_SIGNATURE_MAX_LEN       28
#define APP_PROFILE_TAG_MAX_COUNT           6
#define APP_PROFILE_PEER_CACHE_COUNT        1
#define APP_PROFILE_ADV_ENC_OVERHEAD_LEN    10
#define APP_PROFILE_PLAIN_BASE_LEN          (2 + 1 + 1 + 2 + 4 + 1 + 1 + 1 + APP_PROFILE_TAG_MAX_COUNT)
#define APP_PROFILE_PLAIN_MAX_LEN           (APP_PROFILE_PLAIN_BASE_LEN + APP_PROFILE_NICKNAME_MAX_LEN + APP_PROFILE_SIGNATURE_MAX_LEN)
#define APP_PROFILE_ADV_BLOCK_MAX_LEN       (APP_PROFILE_ADV_ENC_OVERHEAD_LEN + APP_PROFILE_PLAIN_MAX_LEN)

#define APP_PROFILE_FLAG_VISIBLE            0x01
#define APP_PROFILE_ADV_FLAG_ENCRYPTED      0x01
#define APP_PROFILE_PEER_FLAG_VALID         0x01
#define APP_PROFILE_PEER_FLAG_TRUNCATED     0x02

typedef struct {
    u8 flags;
    u8 key_id;
    u16 seq;
    u32 avatar_seed;
    u8 tag_count;
    u8 tags[APP_PROFILE_TAG_MAX_COUNT];
    u8 nickname_len;
    u8 signature_len;
    u8 nickname[APP_PROFILE_NICKNAME_MAX_LEN];
    u8 signature[APP_PROFILE_SIGNATURE_MAX_LEN];
} app_profile_summary_t;

typedef struct {
    u8 flags;
    u16 seq;
    u32 avatar_seed;
    u8 tag_count;
    u8 tags[APP_PROFILE_TAG_MAX_COUNT];
    u8 nickname_len;
    u8 signature_len;
    u8 nickname[APP_PROFILE_NICKNAME_MAX_LEN];
    u8 signature[APP_PROFILE_SIGNATURE_MAX_LEN];
} app_peer_profile_t;

typedef struct {
    u8 in_use;
    app_eid_t eid;
    s8 rssi;
    app_peer_profile_t profile;
} app_profile_peer_record_t;

void app_profile_init(void);
app_status_t app_profile_load(void);
app_status_t app_profile_save(void);
const app_profile_summary_t *app_profile_get(void);
app_status_t app_profile_set_summary(const app_profile_summary_t *summary);
u16 app_profile_build_host_payload(u8 *out, u16 max_len);
app_status_t app_profile_parse_host_payload(const u8 *payload, u16 len, app_profile_summary_t *summary);
app_status_t app_profile_build_adv_block(u8 *out, u8 max_len, u8 *out_len);
app_status_t app_profile_parse_adv_block(const u8 *payload, u8 len, app_peer_profile_t *profile);
app_status_t app_profile_cache_peer(const app_eid_t *eid, s8 rssi, const app_peer_profile_t *profile);
const app_peer_profile_t *app_profile_find_peer(const app_eid_t *eid);
u8 app_profile_copy_peers(app_profile_peer_record_t *out, u8 max_count);

#endif
