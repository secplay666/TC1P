#include "app_crypto.h"
#include "common/string.h"
#include "drivers.h"

#define APP_CHAT_CRYPTO_MAGIC_LO    0x47
#define APP_CHAT_CRYPTO_MAGIC_HI    0x43

static const u8 s_chat_product_key[16] = {
    'G', 'l', 'i', 'm', 'm', 'e', 'r', 'C',
    'h', 'a', 't', 'K', 'e', 'y', '0', '1',
};

void app_crypto_init(void)
{
    reg_clk_en1 |= FLD_CLK1_AES_EN;
}

u8 app_crc8(const void *data, u16 len)
{
    const u8 *p = (const u8 *)data;
    u8 crc = 0xff;
    u16 i;
    u8 bit;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (u8)((crc << 1) ^ 0x31) : (u8)(crc << 1);
        }
    }
    return crc;
}

u16 app_crc16(const void *data, u16 len)
{
    const u8 *p = (const u8 *)data;
    u16 crc = 0xffff;
    u16 i;
    u8 bit;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (u16)((crc >> 1) ^ 0xa001) : (u16)(crc >> 1);
        }
    }
    return crc;
}

u32 app_crc32(const void *data, u16 len, u32 init)
{
    const u8 *p = (const u8 *)data;
    u32 crc = ~init;
    u16 i;
    u8 bit;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xedb88320UL) : (crc >> 1);
        }
    }
    return ~crc;
}

app_status_t app_crypto_aes128_encrypt(const u8 key[16], const u8 input[16], u8 output[16])
{
    if (!key || !input || !output) {
        return APP_ERR_PARAM;
    }
    aes_encrypt((unsigned char *)key, (unsigned char *)input, output);
    return APP_OK;
}

app_status_t app_crypto_aes128_ctr_xcrypt(const u8 key[16], u32 nonce, const void *input, void *output, u16 len)
{
    const u8 *in = (const u8 *)input;
    u8 *out = (u8 *)output;
    u8 block[16];
    u8 stream[16];
    u16 offset = 0;
    u32 counter = 0;
    u8 i;

    if (!key || (len && (!input || !output))) {
        return APP_ERR_PARAM;
    }

    while (offset < len) {
        memset(block, 0, sizeof(block));
        block[0] = 'G';
        block[1] = 'L';
        block[2] = 'P';
        block[3] = 'F';
        block[4] = (u8)nonce;
        block[5] = (u8)(nonce >> 8);
        block[6] = (u8)(nonce >> 16);
        block[7] = (u8)(nonce >> 24);
        block[12] = (u8)counter;
        block[13] = (u8)(counter >> 8);
        block[14] = (u8)(counter >> 16);
        block[15] = (u8)(counter >> 24);
        if (app_crypto_aes128_encrypt(key, block, stream) != APP_OK) {
            return APP_ERR_UNSUPPORTED;
        }

        for (i = 0; i < sizeof(stream) && offset < len; i++, offset++) {
            out[offset] = in[offset] ^ stream[i];
        }
        counter++;
    }
    return APP_OK;
}

app_status_t app_crypto_cmac(const u8 key[16], const void *data, u16 len, u8 *mic, u8 mic_len)
{
    u32 crc;
    u8 i;
    (void)key;
    if (!data || !mic || mic_len > 4) {
        return APP_ERR_PARAM;
    }
    crc = app_crc32(data, len, 0);
    for (i = 0; i < mic_len; i++) {
        mic[i] = (u8)(crc >> (i * 8));
    }
    return APP_OK;
}

static void chat_wr16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u16 chat_rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static void chat_wr32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u32 chat_rd32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static s8 eid_compare(const app_eid_t *a, const app_eid_t *b)
{
    u8 i;
    for (i = 0; i < APP_EID_LEN; i++) {
        if (a->bytes[i] < b->bytes[i]) {
            return -1;
        }
        if (a->bytes[i] > b->bytes[i]) {
            return 1;
        }
    }
    return 0;
}

static app_status_t derive_chat_key(const app_eid_t *local_eid,
                                    const app_eid_t *peer_eid,
                                    u32 nonce,
                                    u8 key[16])
{
    const app_eid_t *lo;
    const app_eid_t *hi;
    u8 block[16];
    u8 tmp[16];
    u8 i;

    if (!local_eid || !peer_eid || !key) {
        return APP_ERR_PARAM;
    }

    if (eid_compare(local_eid, peer_eid) <= 0) {
        lo = local_eid;
        hi = peer_eid;
    } else {
        lo = peer_eid;
        hi = local_eid;
    }

    for (i = 0; i < APP_EID_LEN; i++) {
        block[i] = (u8)(lo->bytes[i] ^ hi->bytes[APP_EID_LEN - 1 - i]);
    }
    block[0] ^= 'G';
    block[1] ^= 'C';
    block[2] ^= 'H';
    block[3] ^= '1';
    block[4] ^= (u8)nonce;
    block[5] ^= (u8)(nonce >> 8);
    block[6] ^= (u8)(nonce >> 16);
    block[7] ^= (u8)(nonce >> 24);

    if (app_crypto_aes128_encrypt(s_chat_product_key, block, tmp) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }

    for (i = 0; i < APP_EID_LEN; i++) {
        block[i] = (u8)(tmp[i] ^ lo->bytes[APP_EID_LEN - 1 - i] ^ hi->bytes[i]);
    }
    block[0] ^= 'G';
    block[1] ^= 'C';
    block[2] ^= 'H';
    block[3] ^= '2';
    block[12] ^= (u8)nonce;
    block[13] ^= (u8)(nonce >> 8);
    block[14] ^= (u8)(nonce >> 16);
    block[15] ^= (u8)(nonce >> 24);

    return app_crypto_aes128_encrypt(s_chat_product_key, block, key);
}

app_status_t app_crypto_chat_encrypt(const app_eid_t *local_eid,
                                     const app_eid_t *peer_eid,
                                     u32 nonce,
                                     const u8 *plain,
                                     u16 plain_len,
                                     u8 *out,
                                     u16 max_len,
                                     u16 *out_len)
{
    u8 key[16];
    u16 crc;

    if (!local_eid || !peer_eid || !out || !out_len ||
        (plain_len && !plain)) {
        return APP_ERR_PARAM;
    }
    if (max_len < (u16)(APP_CHAT_CRYPTO_HEADER_LEN + plain_len)) {
        return APP_ERR_NO_MEM;
    }

    if (derive_chat_key(local_eid, peer_eid, nonce, key) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }

    out[0] = APP_CHAT_CRYPTO_MAGIC_LO;
    out[1] = APP_CHAT_CRYPTO_MAGIC_HI;
    out[2] = APP_CHAT_CRYPTO_VERSION;
    out[3] = APP_CHAT_CRYPTO_FLAG_ENCRYPTED;
    chat_wr32(&out[4], nonce);
    chat_wr16(&out[8], plain_len);
    crc = app_crc16(plain, plain_len);
    chat_wr16(&out[10], crc);

    if (app_crypto_aes128_ctr_xcrypt(key, nonce, plain,
                                     &out[APP_CHAT_CRYPTO_HEADER_LEN],
                                     plain_len) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }

    *out_len = (u16)(APP_CHAT_CRYPTO_HEADER_LEN + plain_len);
    return APP_OK;
}

app_status_t app_crypto_chat_decrypt(const app_eid_t *local_eid,
                                     const app_eid_t *peer_eid,
                                     const u8 *input,
                                     u16 input_len,
                                     u8 *plain,
                                     u16 max_plain_len,
                                     u16 *plain_len)
{
    u8 key[16];
    u32 nonce;
    u16 len;
    u16 crc;

    if (!local_eid || !peer_eid || !input || !plain || !plain_len) {
        return APP_ERR_PARAM;
    }
    *plain_len = 0;
    if (input_len < APP_CHAT_CRYPTO_HEADER_LEN ||
        input[0] != APP_CHAT_CRYPTO_MAGIC_LO ||
        input[1] != APP_CHAT_CRYPTO_MAGIC_HI ||
        input[2] != APP_CHAT_CRYPTO_VERSION ||
        !(input[3] & APP_CHAT_CRYPTO_FLAG_ENCRYPTED)) {
        return APP_ERR_UNSUPPORTED;
    }

    nonce = chat_rd32(&input[4]);
    len = chat_rd16(&input[8]);
    if (len > max_plain_len ||
        input_len < (u16)(APP_CHAT_CRYPTO_HEADER_LEN + len)) {
        return APP_ERR_PARAM;
    }

    if (derive_chat_key(local_eid, peer_eid, nonce, key) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }
    if (app_crypto_aes128_ctr_xcrypt(key, nonce,
                                     &input[APP_CHAT_CRYPTO_HEADER_LEN],
                                     plain, len) != APP_OK) {
        return APP_ERR_UNSUPPORTED;
    }

    crc = chat_rd16(&input[10]);
    if (crc != app_crc16(plain, len)) {
        return APP_ERR_CRC;
    }

    *plain_len = len;
    return APP_OK;
}
