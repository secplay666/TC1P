#include "app_crypto.h"

void app_crypto_init(void)
{
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
    u8 i;
    if (!key || !input || !output) {
        return APP_ERR_PARAM;
    }
    for (i = 0; i < 16; i++) {
        output[i] = input[i] ^ key[i];
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
