#ifndef APP_CRYPTO_H_
#define APP_CRYPTO_H_

#include "../common/app_types.h"

void app_crypto_init(void);
u8 app_crc8(const void *data, u16 len);
u16 app_crc16(const void *data, u16 len);
u32 app_crc32(const void *data, u16 len, u32 init);
app_status_t app_crypto_aes128_encrypt(const u8 key[16], const u8 input[16], u8 output[16]);
app_status_t app_crypto_aes128_ctr_xcrypt(const u8 key[16], u32 nonce, const void *input, void *output, u16 len);
app_status_t app_crypto_cmac(const u8 key[16], const void *data, u16 len, u8 *mic, u8 mic_len);

#endif
