#include "app_adv_proto.h"
#include "../crypto/app_crypto.h"
#include "common/string.h"

#define AD_TYPE_MANUFACTURER_SPECIFIC 0xff

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

void app_adv_proto_init(void)
{
}

u8 app_adv_proto_is_manufacturer_payload(const u8 *ad_data, u8 ad_len, const u8 **payload, u8 *payload_len)
{
    u8 idx = 0;
    while (idx < ad_len) {
        u8 len = ad_data[idx++];
        u8 type;
        if (!len || idx + len > ad_len) {
            break;
        }
        type = ad_data[idx++];
        if (type == AD_TYPE_MANUFACTURER_SPECIFIC && len >= 3) {
            u16 company = rd16(&ad_data[idx]);
            if (company == APP_ADV_COMPANY_ID_DEV) {
                *payload = &ad_data[idx + 2];
                *payload_len = (u8)(len - 3);
                return 1;
            }
        }
        idx = (u8)(idx + len - 1);
    }
    return 0;
}

app_status_t app_adv_proto_encode(const app_adv_frame_t *frame, u8 *out, u8 out_max, u8 *out_len)
{
    u8 *p;
    u8 vendor_len;
    u16 header_crc_off;
    u32 frame_crc;

    if (!frame || !out || !out_len || frame->payload_len > APP_ADV_PAYLOAD_MAX_LEN) {
        return APP_ERR_PARAM;
    }

    vendor_len = (u8)(APP_ADV_HEADER_LEN + frame->payload_len + 4);
    if ((u16)vendor_len + 4 > out_max) {
        return APP_ERR_NO_MEM;
    }

    out[0] = (u8)(vendor_len + 3);
    out[1] = AD_TYPE_MANUFACTURER_SPECIFIC;
    wr16(&out[2], APP_ADV_COMPANY_ID_DEV);
    p = &out[4];

    p[0] = APP_ADV_MAGIC_LO;
    p[1] = APP_ADV_MAGIC_HI;
    p[2] = APP_ADV_PROTOCOL_VERSION;
    p[3] = APP_ADV_HEADER_LEN;
    p[4] = (u8)frame->type;
    p[5] = frame->flags;
    p[6] = frame->key_id;
    p[7] = frame->device_state;
    wr16(&p[8], frame->frame_seq);
    memcpy(&p[10], frame->src_eid.bytes, APP_EID_LEN);
    memcpy(&p[26], frame->dst_eid.bytes, APP_EID_LEN);
    wr32(&p[42], frame->message_id);
    p[46] = frame->fragment_index;
    p[47] = frame->fragment_count;
    p[48] = frame->payload_len;
    header_crc_off = 49;
    p[header_crc_off] = app_crc8(p, header_crc_off);
    if (frame->payload_len && frame->payload) {
        memcpy(&p[APP_ADV_HEADER_LEN], frame->payload, frame->payload_len);
    }
    frame_crc = app_crc32(p, (u16)(APP_ADV_HEADER_LEN + frame->payload_len), 0);
    wr32(&p[APP_ADV_HEADER_LEN + frame->payload_len], frame_crc);
    *out_len = (u8)(vendor_len + 4);
    return APP_OK;
}

app_status_t app_adv_proto_decode(const u8 *data, u8 len, app_adv_frame_t *frame)
{
    const u8 *payload;
    u8 payload_len;
    u32 crc_calc;
    u32 crc_frame;

    if (!data || !frame) {
        return APP_ERR_PARAM;
    }
    if (!app_adv_proto_is_manufacturer_payload(data, len, &payload, &payload_len)) {
        return APP_ERR_NOT_FOUND;
    }
    if (payload_len < APP_ADV_HEADER_LEN + 4) {
        return APP_ERR_PARAM;
    }
    if (payload[0] != APP_ADV_MAGIC_LO || payload[1] != APP_ADV_MAGIC_HI ||
        payload[2] != APP_ADV_PROTOCOL_VERSION || payload[3] != APP_ADV_HEADER_LEN) {
        return APP_ERR_UNSUPPORTED;
    }
    if (app_crc8(payload, 49) != payload[49]) {
        return APP_ERR_CRC;
    }

    frame->payload_len = payload[48];
    if ((u16)APP_ADV_HEADER_LEN + frame->payload_len + 4 > payload_len) {
        return APP_ERR_PARAM;
    }

    crc_calc = app_crc32(payload, (u16)(APP_ADV_HEADER_LEN + frame->payload_len), 0);
    crc_frame = rd32(&payload[APP_ADV_HEADER_LEN + frame->payload_len]);
    if (crc_calc != crc_frame) {
        return APP_ERR_CRC;
    }

    frame->type = (app_adv_frame_type_t)payload[4];
    frame->flags = payload[5];
    frame->key_id = payload[6];
    frame->device_state = payload[7];
    frame->frame_seq = rd16(&payload[8]);
    memcpy(frame->src_eid.bytes, &payload[10], APP_EID_LEN);
    memcpy(frame->dst_eid.bytes, &payload[26], APP_EID_LEN);
    frame->message_id = rd32(&payload[42]);
    frame->fragment_index = payload[46];
    frame->fragment_count = payload[47];
    frame->payload = &payload[APP_ADV_HEADER_LEN];
    return APP_OK;
}
