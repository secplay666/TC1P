#ifndef APP_DEBUG_SHELL_CMD_H_
#define APP_DEBUG_SHELL_CMD_H_

#include "common/types.h"

typedef void (*app_debug_shell_cmd_write_fn_t)(void *ctx, const char *text, u16 len);
typedef void (*app_debug_shell_cmd_handler_t)(u8 argc, char **argv);

typedef struct {
    const char *name;
    const char *usage;
    const char *help;
    app_debug_shell_cmd_handler_t handler;
} app_debug_shell_cmd_entry_t;

void app_debug_shell_cmd_init(void);
void app_debug_shell_cmd_print_boot_info(void);

/* Register after app_debug_shell_cmd_init(); argv[0] is the command name. */
u8 app_debug_shell_cmd_register(const char *name, const char *usage, const char *help,
                                app_debug_shell_cmd_handler_t handler);
void app_debug_shell_cmd_execute(char *line);
void app_debug_shell_cmd_execute_with_writer(char *line, app_debug_shell_cmd_write_fn_t writer, void *ctx);

void app_debug_shell_cmd_puts(const char *text);
void app_debug_shell_cmd_print_u8(const char *label, u8 value);
void app_debug_shell_cmd_print_s8(const char *label, s8 value);
void app_debug_shell_cmd_print_u32(const char *label, u32 value);

#endif
