#include "app_host_frame.h"
#include "../crypto/app_crypto.h"
#include "common/string.h"

static void wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

app_status_t app_host_frame_encode(const app_host_frame_tx_t *frame, u8 *out, u8 out_max, u8 *out_len)
{
    u16 crc;

    if (!frame || !out || !out_len) {
        return APP_ERR_PARAM;
    }
    if (frame->payload_len > APP_HOST_FRAME_CHUNK_MAX_LEN ||
        out_max < (u8)(APP_HOST_FRAME_HEADER_LEN + frame->payload_len + APP_HOST_FRAME_CRC_LEN)) {
        return APP_ERR_NO_MEM;
    }
    if (!frame->frag_count || frame->frag_index >= frame->frag_count) {
        return APP_ERR_PARAM;
    }
    if (frame->payload_len && !frame->payload) {
        return APP_ERR_PARAM;
    }

    out[0] = APP_HOST_FRAME_MAGIC;
    out[1] = APP_HOST_FRAME_VERSION;
    out[2] = (u8)frame->type;
    out[3] = frame->seq;
    out[4] = frame->cmd;
    out[5] = frame->status;
    out[6] = frame->frag_index;
    out[7] = frame->frag_count;
    out[8] = frame->payload_len;
    if (frame->payload_len) {
        memcpy(&out[APP_HOST_FRAME_HEADER_LEN], frame->payload, frame->payload_len);
    }

    crc = app_crc16(out, (u16)(APP_HOST_FRAME_HEADER_LEN + frame->payload_len));
    wr16(&out[APP_HOST_FRAME_HEADER_LEN + frame->payload_len], crc);
    *out_len = (u8)(APP_HOST_FRAME_HEADER_LEN + frame->payload_len + APP_HOST_FRAME_CRC_LEN);
    return APP_OK;
}

app_status_t app_host_frame_decode(const u8 *data, u8 len, app_host_frame_rx_t *frame)
{
    u8 payload_len;
    u16 crc_calc;
    u16 crc_frame;

    if (!data || !frame) {
        return APP_ERR_PARAM;
    }
    if (len < APP_HOST_FRAME_HEADER_LEN + APP_HOST_FRAME_CRC_LEN) {
        return APP_ERR_PARAM;
    }
    if (data[0] != APP_HOST_FRAME_MAGIC || data[1] != APP_HOST_FRAME_VERSION) {
        return APP_ERR_UNSUPPORTED;
    }

    payload_len = data[8];
    if (payload_len > APP_HOST_FRAME_CHUNK_MAX_LEN ||
        len != (u8)(APP_HOST_FRAME_HEADER_LEN + payload_len + APP_HOST_FRAME_CRC_LEN)) {
        return APP_ERR_PARAM;
    }

    crc_calc = app_crc16(data, (u16)(APP_HOST_FRAME_HEADER_LEN + payload_len));
    crc_frame = rd16(&data[APP_HOST_FRAME_HEADER_LEN + payload_len]);
    if (crc_calc != crc_frame) {
        return APP_ERR_CRC;
    }

    memset(frame, 0, sizeof(*frame));
    frame->type = (app_host_frame_type_t)data[2];
    frame->seq = data[3];
    frame->cmd = data[4];
    frame->status = data[5];
    frame->frag_index = data[6];
    frame->frag_count = data[7];
    frame->payload_len = payload_len;
    if (!frame->frag_count || frame->frag_index >= frame->frag_count) {
        return APP_ERR_PARAM;
    }
    if (payload_len) {
        memcpy(frame->payload, &data[APP_HOST_FRAME_HEADER_LEN], payload_len);
    }
    return APP_OK;
}
