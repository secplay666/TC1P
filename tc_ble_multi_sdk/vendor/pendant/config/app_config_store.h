#ifndef APP_CONFIG_STORE_H_
#define APP_CONFIG_STORE_H_

#include "../common/app_types.h"

void app_config_init(void);
const app_runtime_config_t *app_config_get(void);
app_status_t app_config_set(const app_runtime_config_t *config);
app_status_t app_config_validate(const app_runtime_config_t *config);
app_status_t app_config_load(void);
app_status_t app_config_save(void);
app_status_t app_config_reset_default(void);
u8 app_config_is_dirty(void);

#endif
