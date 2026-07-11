#ifndef APP_OTA_H_
#define APP_OTA_H_

#include "app_config.h"
#include "common/app_types.h"

void app_ota_configure_boot(void);
void app_ota_init(void);
void app_ota_poll(void);
void app_ota_on_disconnected(void);
u8 app_ota_is_active(void);
u8 app_ota_last_result(void);

#endif
