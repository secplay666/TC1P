#pragma once

/*
 * u_printf.h hides the u_printf declaration when UART_PRINT_DEBUG_ENABLE is 0.
 * The pendant firmware initializes the UART print path directly, so keep a
 * small local declaration for debug-only output.
 */
int u_printf(const char *format, ...);

