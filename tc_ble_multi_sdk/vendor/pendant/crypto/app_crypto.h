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

#define APP_CHAT_CRYPTO_VERSION             1
#define APP_CHAT_CRYPTO_HEADER_LEN          12
#define APP_CHAT_CRYPTO_FLAG_ENCRYPTED      0x01

app_status_t app_crypto_chat_encrypt(const app_eid_t *local_eid,
                                     const app_eid_t *peer_eid,
                                     u32 nonce,
                                     const u8 *plain,
                                     u16 plain_len,
                                     u8 *out,
                                     u16 max_len,
                                     u16 *out_len);
app_status_t app_crypto_chat_decrypt(const app_eid_t *local_eid,
                                     const app_eid_t *peer_eid,
                                     const u8 *input,
                                     u16 input_len,
                                     u8 *plain,
                                     u16 max_plain_len,
                                     u16 *plain_len);

#endif
