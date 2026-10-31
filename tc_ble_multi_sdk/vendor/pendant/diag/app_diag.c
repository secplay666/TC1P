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
    u_printf("[Glimmer][ERR]\r\n");
    u_printf(" code=0x%x\r\n", error_code);
    u_printf(" detail=0x%x\r\n", detail);
    app_host_cmd_notify_error(error_code, detail);
}
