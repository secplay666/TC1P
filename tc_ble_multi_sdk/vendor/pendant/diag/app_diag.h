#ifndef APP_DIAG_H_
#define APP_DIAG_H_

#include "../common/app_types.h"

typedef struct {
    u16 error_code;
    u16 detail;
    app_system_state_t state;
    u32 tick;
} app_error_record_t;

void app_diag_init(void);
void app_diag_log_error(u16 error_code, u16 detail);
void app_diag_log_info(const char *tag, const char *msg);
void app_diag_get_last_error(app_error_record_t *record);
void app_diag_clear_error(void);
u8 app_diag_has_error(void);

#endif
