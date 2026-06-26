#include "app_profile.h"
#include "../storage/app_storage.h"
#include "../crypto/app_crypto.h"
#include "../adv_scheduler/app_adv_scheduler.h"
#include "common/string.h"

#define APP_PROFILE_RECORD_MAGIC       0x504c4747UL
#define APP_PROFILE_RECORD_VERSION     1
#define APP_PROFILE_ADV_MAGIC_LO       0x47
#define APP_PROFILE_ADV_MAGIC_HI       0x50
#define APP_PROFILE_CARD_MAGIC_LO      0x50
#define APP_PROFILE_CARD_MAGIC_HI      0x43
#define APP_PROFILE_HOST_SUMMARY_BASE_LEN (12 + APP_PROFILE_TAG_MAX_COUNT)
#define APP_PROFILE_HOST_SET_BASE_LEN     (8 + APP_PROFILE_TAG_MAX_COUNT)

typedef struct {
    u32 magic;
    u8 version;
    u8 key_id;
    u16 seq;
    u8 key[APP_PROFILE_KEY_LEN];
    app_profile_summary_t summary;
    u16 crc16;
} app_profile_record_t;

static app_profile_summary_t s_profile;
static u8 s_key[APP_PROFILE_KEY_LEN];
static u8 s_loaded;

static app_profile_peer_record_t s_peer_cache[APP_PROFILE_PEER_CACHE_COUNT];
static u8 s_peer_cache_rr;

static const u8 s_default_key[APP_PROFILE_KEY_LEN] = {
    'G', 'l', 'i', 'm', 'm', 'e', 'r', 'B',
    'c', 'a', 's', 't', 'K', 'e', 'y', '1',
};

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u32 rd32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 profile_record_crc(const app_profile_record_t *record)
{
    app_profile_record_t tmp;
    tmp = *record;
    tmp.crc16 = 0;
    return app_crc16(&tmp, sizeof(tmp));
}

static void profile_make_default(void)
{
    static const u8 name[] = {'G', 'l', 'i', 'm', 'm', 'e', 'r'};
    static const u8 line[] = {
        'A', ' ', 'q', 'u', 'i', 'e', 't', ' ', 'h', 'e', 'l', 'l', 'o',
        ' ', 'n', 'e', 'a', 'r', 'b', 'y', '.'
    };

    memset(&s_profile, 0, sizeof(s_profile));
    memcpy(s_key, s_default_key, sizeof(s_key));
    s_profile.flags = APP_PROFILE_FLAG_VISIBLE;
    s_profile.key_id = 1;
    s_profile.seq = 1;
    s_profile.avatar_seed = 0x47504c31UL;
    s_profile.tag_count = 2;
    s_profile.tags[0] = 1;
    s_profile.tags[1] = 2;
    s_profile.nickname_len = sizeof(name);
    memcpy(s_profile.nickname, name, sizeof(name));
    s_profile.signature_len = sizeof(line);
    memcpy(s_profile.signature, line, sizeof(line));
}

void app_profile_init(void)
{
    profile_make_default();
    memset(s_peer_cache, 0, sizeof(s_peer_cache));
    s_peer_cache_rr = 0;
    s_loaded = 0;
}

app_status_t app_profile_save(void)
{
    app_profile_record_t record;
    app_profile_record_t verify;

    memset(&record, 0, sizeof(record));
    record.magic = APP_PROFILE_RECORD_MAGIC;
    record.version = APP_PROFILE_RECORD_VERSION;
    record.key_id = s_profile.key_id;
    record.seq = s_profile.seq;
    memcpy(record.key, s_key, sizeof(record.key));
    record.summary = s_profile;
    record.crc16 = profile_record_crc(&record);

    if (app_storage_erase(APP_STORAGE_PART_PROFILE) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (app_storage_write(APP_STORAGE_PART_PROFILE, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (app_storage_read(APP_STORAGE_PART_PROFILE, 0, &verify, sizeof(verify)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (verify.magic != APP_PROFILE_RECORD_MAGIC ||
        verify.version != APP_PROFILE_RECORD_VERSION ||
        verify.crc16 != profile_record_crc(&verify)) {
        return APP_ERR_FLASH;
    }
    s_loaded = 1;
    return APP_OK;
}

app_status_t app_profile_load(void)
{
    app_profile_record_t record;

    if (app_storage_read(APP_STORAGE_PART_PROFILE, 0, &record, sizeof(record)) != APP_OK) {
        return APP_ERR_FLASH;
    }
    if (record.magic != APP_PROFILE_RECORD_MAGIC ||
        record.version != APP_PROFILE_RECORD_VERSION ||
        record.crc16 != profile_record_crc(&record)) {
        profile_make_default();
        return app_profile_save();
    }

    memcpy(s_key, record.key, sizeof(s_key));
    s_profile = record.summary;
    s_profile.key_id = record.key_id;
    s_profile.seq = record.seq;
    if (s_profile.nickname_len > APP_PROFILE_NICKNAME_MAX_LEN ||
        s_profile.signature_len > APP_PROFILE_SIGNATURE_MAX_LEN ||
        s_profile.tag_count > APP_PROFILE_TAG_MAX_COUNT) {
        profile_make_default();
        return app_profile_save();
    }
    s_loaded = 1;
    return APP_OK;
}

const app_profile_summary_t *app_profile_get(void)
{
    if (!s_loaded) {
        app_profile_load();
    }
    return &s_profile;
}

app_status_t app_profile_set_summary(const app_profile_summary_t *summary)
{
    if (!summary ||
        summary->nickname_len > APP_PROFILE_NICKNAME_MAX_LEN ||
        summary->signature_len > APP_PROFILE_SIGNATURE_MAX_LEN ||
        summary->tag_count > APP_PROFILE_TAG_MAX_COUNT) {
        return APP_ERR_PARAM;
    }

    s_profile.flags = summary->flags;
    s_profile.avatar_seed = summary->avatar_seed;
    s_profile.tag_count = summary->tag_count;
    s_profile.nickname_len = summary->nickname_len;
    s_profile.signature_len = summary->signature_len;
    memset(s_profile.tags, 0, sizeof(s_profile.tags));
    memset(s_profile.nickname, 0, sizeof(s_profile.nickname));
    memset(s_profile.signature, 0, sizeof(s_profile.signature));
    if (summary->tag_count) {
        memcpy(s_profile.tags, summary->tags, summary->tag_count);
    }
    if (summary->nickname_len) {
        memcpy(s_profile.nickname, summary->nickname, summary->nickname_len);
    }
    if (summary->signature_len) {
        memcpy(s_profile.signature, summary->signature, summary->signature_len);
    }
    s_profile.seq++;
    if (!s_profile.seq) {
        s_profile.seq = 1;
    }
    return app_profile_save();
}

u16 app_profile_build_host_payload(u8 *out, u16 max_len)
{
    u16 offset = 0;
    const app_profile_summary_t *summary = app_profile_get();

    if (!out || max_len < APP_PROFILE_HOST_SUMMARY_BASE_LEN + summary->nickname_len + summary->signature_len) {
        return 0;
    }

    out[offset++] = APP_PROFILE_VERSION;
    out[offset++] = summary->flags;
    wr16(&out[offset], summary->seq); offset = (u16)(offset + 2);
    out[offset++] = summary->key_id;
    wr32(&out[offset], summary->avatar_seed); offset = (u16)(offset + 4);
    out[offset++] = summary->tag_count;
    out[offset++] = summary->nickname_len;
    out[offset++] = summary->signature_len;
    memcpy(&out[offset], summary->tags, APP_PROFILE_TAG_MAX_COUNT); offset = (u16)(offset + APP_PROFILE_TAG_MAX_COUNT);
    if (summary->nickname_len) {
        memcpy(&out[offset], summary->nickname, summary->nickname_len);
        offset = (u16)(offset + summary->nickname_len);
    }
    if (summary->signature_len) {
        memcpy(&out[offset], summary->signature, summary->signature_len);
        offset = (u16)(offset + summary->signature_len);
    }
    return offset;
}

app_status_t app_profile_parse_host_payload(const u8 *payload, u16 len, app_profile_summary_t *summary)
{
    u16 offset = 0;

    if (!payload || !summary || len < APP_PROFILE_HOST_SET_BASE_LEN) {
        return APP_ERR_PARAM;
    }

    memset(summary, 0, sizeof(*summary));
    summary->flags = payload[offset++];
    summary->avatar_seed = rd32(&payload[offset]); offset = (u16)(offset + 4);
    summary->tag_count = payload[offset++];
    summary->nickname_len = payload[offset++];
    summary->signature_len = payload[offset++];
    if (summary->tag_count > APP_PROFILE_TAG_MAX_COUNT ||
        summary->nickname_len > APP_PROFILE_NICKNAME_MAX_LEN ||
        summary->signature_len > APP_PROFILE_SIGNATURE_MAX_LEN ||
        len < offset + APP_PROFILE_TAG_MAX_COUNT + summary->nickname_len + summary->signature_len) {
        return APP_ERR_PARAM;
    }
    memcpy(summary->tags, &payload[offset], APP_PROFILE_TAG_MAX_COUNT); offset = (u16)(offset + APP_PROFILE_TAG_MAX_COUNT);
    if (summary->nickname_len) {
        memcpy(summary->nickname, &payload[offset], summary->nickname_len);
        offset = (u16)(offset + summary->nickname_len);
    }
    if (summary->signature_len) {
        memcpy(summary->signature, &payload[offset], summary->signature_len);
    }
    summary->key_id = s_profile.key_id;
    summary->seq = s_profile.seq;
    return APP_OK;
}

static u8 build_plain_card(u8 *out, u8 max_len)
{
    u8 offset = 0;
    const app_profile_summary_t *summary = app_profile_get();

    if (!(summary->flags & APP_PROFILE_FLAG_VISIBLE)) {
        return 0;
    }
    if (max_len < APP_PROFILE_PLAIN_BASE_LEN + summary->nickname_len + summary->signature_len) {
        return 0;
    }

    out[offset++] = APP_PROFILE_CARD_MAGIC_LO;
    out[offset++] = APP_PROFILE_CARD_MAGIC_HI;
    out[offset++] = APP_PROFILE_VERSION;
    out[offset++] = summary->flags;
    wr16(&out[offset], summary->seq); offset = (u8)(offset + 2);
    wr32(&out[offset], summary->avatar_seed); offset = (u8)(offset + 4);
    out[offset++] = summary->tag_count;
    out[offset++] = summary->nickname_len;
    out[offset++] = summary->signature_len;
    memcpy(&out[offset], summary->tags, APP_PROFILE_TAG_MAX_COUNT); offset = (u8)(offset + APP_PROFILE_TAG_MAX_COUNT);
    if (summary->nickname_len) {
        memcpy(&out[offset], summary->nickname, summary->nickname_len);
        offset = (u8)(offset + summary->nickname_len);
    }
    if (summary->signature_len) {
        memcpy(&out[offset], summary->signature, summary->signature_len);
        offset = (u8)(offset + summary->signature_len);
    }
    return offset;
}

app_status_t app_profile_build_adv_block(u8 *out, u8 max_len, u8 *out_len)
{
    u8 plain[APP_PROFILE_PLAIN_MAX_LEN];
    u8 plain_len;
    u16 crc;
    const app_profile_summary_t *summary;

    if (!out || !out_len) {
        return APP_ERR_PARAM;
    }
    *out_len = 0;
    summary = app_profile_get();
    plain_len = build_plain_card(plain, sizeof(plain));
    if (!plain_len) {
        return APP_OK;
    }
    if (max_len < APP_PROFILE_ADV_ENC_OVERHEAD_LEN + plain_len) {
        return APP_ERR_NO_MEM;
    }

    out[0] = APP_PROFILE_ADV_MAGIC_LO;
    out[1] = APP_PROFILE_ADV_MAGIC_HI;
    out[2] = APP_PROFILE_VERSION;
    out[3] = APP_PROFILE_ADV_FLAG_ENCRYPTED;
    out[4] = summary->key_id;
    wr16(&out[5], summary->seq);
    out[7] = plain_len;
    crc = app_crc16(plain, plain_len);
    wr16(&out[8], crc);
    if (app_crypto_aes128_ctr_xcrypt(s_key, summary->seq, plain, &out[APP_PROFILE_ADV_ENC_OVERHEAD_LEN], plain_len) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }
    *out_len = (u8)(APP_PROFILE_ADV_ENC_OVERHEAD_LEN + plain_len);
    return APP_OK;
}

app_status_t app_profile_parse_adv_block(const u8 *payload, u8 len, app_peer_profile_t *profile)
{
    u8 plain[APP_PROFILE_PLAIN_MAX_LEN];
    u8 plain_len;
    u16 seq;
    u16 offset;
    u16 crc;

    if (!payload || !profile || len < APP_PROFILE_ADV_ENC_OVERHEAD_LEN) {
        return APP_ERR_PARAM;
    }
    if (payload[0] != APP_PROFILE_ADV_MAGIC_LO ||
        payload[1] != APP_PROFILE_ADV_MAGIC_HI ||
        payload[2] != APP_PROFILE_VERSION ||
        !(payload[3] & APP_PROFILE_ADV_FLAG_ENCRYPTED)) {
        return APP_ERR_UNSUPPORTED;
    }

    seq = rd16(&payload[5]);
    plain_len = payload[7];
    if (!plain_len || plain_len > APP_PROFILE_PLAIN_MAX_LEN ||
        len < APP_PROFILE_ADV_ENC_OVERHEAD_LEN + plain_len) {
        return APP_ERR_PARAM;
    }
    if (app_crypto_aes128_ctr_xcrypt(s_key, seq, &payload[APP_PROFILE_ADV_ENC_OVERHEAD_LEN], plain, plain_len) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }
    crc = rd16(&payload[8]);
    if (crc != app_crc16(plain, plain_len)) {
        return APP_ERR_CRC;
    }
    if (plain_len < APP_PROFILE_PLAIN_BASE_LEN ||
        plain[0] != APP_PROFILE_CARD_MAGIC_LO ||
        plain[1] != APP_PROFILE_CARD_MAGIC_HI ||
        plain[2] != APP_PROFILE_VERSION) {
        return APP_ERR_UNSUPPORTED;
    }

    memset(profile, 0, sizeof(*profile));
    profile->flags = plain[3] | APP_PROFILE_PEER_FLAG_VALID;
    profile->seq = rd16(&plain[4]);
    profile->avatar_seed = rd32(&plain[6]);
    profile->tag_count = plain[10];
    profile->nickname_len = plain[11];
    profile->signature_len = plain[12];
    if (profile->tag_count > APP_PROFILE_TAG_MAX_COUNT ||
        profile->nickname_len > APP_PROFILE_NICKNAME_MAX_LEN ||
        profile->signature_len > APP_PROFILE_SIGNATURE_MAX_LEN ||
        plain_len < APP_PROFILE_PLAIN_BASE_LEN + profile->nickname_len + profile->signature_len) {
        return APP_ERR_PARAM;
    }

    offset = 13;
    memcpy(profile->tags, &plain[offset], APP_PROFILE_TAG_MAX_COUNT); offset = (u16)(offset + APP_PROFILE_TAG_MAX_COUNT);
    if (profile->nickname_len) {
        memcpy(profile->nickname, &plain[offset], profile->nickname_len);
        offset = (u16)(offset + profile->nickname_len);
    }
    if (profile->signature_len) {
        memcpy(profile->signature, &plain[offset], profile->signature_len);
    }
    return APP_OK;
}

app_status_t app_profile_cache_peer(const app_eid_t *eid, s8 rssi, const app_peer_profile_t *profile)
{
    u8 i;
    u8 slot = APP_PROFILE_PEER_CACHE_COUNT;

    if (!eid || !profile || !(profile->flags & APP_PROFILE_PEER_FLAG_VALID)) {
        return APP_ERR_PARAM;
    }

    for (i = 0; i < APP_PROFILE_PEER_CACHE_COUNT; i++) {
        if (s_peer_cache[i].in_use && app_eid_equal(&s_peer_cache[i].eid, eid)) {
            slot = i;
            break;
        }
    }
    if (slot >= APP_PROFILE_PEER_CACHE_COUNT) {
        for (i = 0; i < APP_PROFILE_PEER_CACHE_COUNT; i++) {
            if (!s_peer_cache[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= APP_PROFILE_PEER_CACHE_COUNT) {
        slot = s_peer_cache_rr;
        s_peer_cache_rr = (u8)((s_peer_cache_rr + 1) % APP_PROFILE_PEER_CACHE_COUNT);
    }

    s_peer_cache[slot].in_use = 1;
    s_peer_cache[slot].eid = *eid;
    s_peer_cache[slot].rssi = rssi;
    s_peer_cache[slot].profile = *profile;
    return APP_OK;
}

const app_peer_profile_t *app_profile_find_peer(const app_eid_t *eid)
{
    u8 i;
    if (!eid) {
        return 0;
    }
    for (i = 0; i < APP_PROFILE_PEER_CACHE_COUNT; i++) {
        if (s_peer_cache[i].in_use && app_eid_equal(&s_peer_cache[i].eid, eid)) {
            return &s_peer_cache[i].profile;
        }
    }
    return 0;
}

u8 app_profile_copy_peers(app_profile_peer_record_t *out, u8 max_count)
{
    u8 i;
    u8 count = 0;

    if (!out) {
        return 0;
    }
    for (i = 0; i < APP_PROFILE_PEER_CACHE_COUNT && count < max_count; i++) {
        if (s_peer_cache[i].in_use) {
            out[count++] = s_peer_cache[i];
        }
    }
    return count;
}
