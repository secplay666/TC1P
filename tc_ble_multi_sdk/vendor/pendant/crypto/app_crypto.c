#include "app_crypto.h"
#include "common/string.h"
#include "drivers.h"

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
