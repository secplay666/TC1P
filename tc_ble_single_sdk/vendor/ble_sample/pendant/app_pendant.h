#ifndef APP_PENDANT_H_
#define APP_PENDANT_H_

#include "common/app_types.h"

void app_pendant_init(void);
void app_pendant_poll(void);
void app_pendant_on_adv_report(const u8 *adv_data, u8 adv_len, s8 rssi, const u8 *addr);
void app_pendant_on_app_connected(const u8 *peer_addr, u16 conn_handle);
void app_pendant_on_app_disconnected(u8 reason);

#endif
