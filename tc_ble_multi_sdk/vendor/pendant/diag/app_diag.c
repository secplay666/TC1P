#include "app_diag.h"
#include "../host_cmd/app_host_cmd.h"
#include "../common/app_debug_print.h"
#include "drivers.h"
#include "timer.h"
#include "application/uartinterface/uart_interface.h"

static app_error_record_t s_last_error;
static u8 s_has_error;

void app_diag_init(void)
{
    s_has_error = 0;
    s_last_error.error_code = 0;
    s_last_error.detail = 0;
    s_last_error.state = SYS_STATE_BOOT;
    s_last_error.tick = 0;
}

void app_diag_log_error(u16 error_code, u16 detail)
{
    s_has_error = 1;
    s_last_error.error_code = error_code;
    s_last_error.detail = detail;
    s_last_error.tick = clock_time();
    u_printf("[PENDANT][ERR]\r\n");
    u_printf(" code=0x%x\r\n", error_code);
    u_printf(" detail=0x%x\r\n", detail);
    app_host_cmd_notify_error(error_code, detail);
}

void app_diag_log_info(const char *tag, const char *msg)
{
    u_printf("[PENDANT]\r\n");
    u_printf(tag);
    u_printf("\r\n");
    u_printf(msg);
    u_printf("\r\n");
    app_host_cmd_log_text(1, tag, msg);
}

void app_diag_get_last_error(app_error_record_t *record)
{
    if (record) {
        *record = s_last_error;
    }
}

void app_diag_clear_error(void)
{
    s_has_error = 0;
    s_last_error.error_code = 0;
    s_last_error.detail = 0;
}

u8 app_diag_has_error(void)
{
    return s_has_error;
}
