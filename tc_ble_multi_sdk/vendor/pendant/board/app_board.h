#ifndef APP_BOARD_H_
#define APP_BOARD_H_

#include "../common/app_types.h"

typedef enum {
    APP_HW_REV_UNKNOWN = 0,
    APP_HW_REV_OLD = 1,
    APP_HW_REV_NEW = 2,
} app_hw_rev_t;

typedef enum {
    APP_PIN_BAT_ADC = 0,
    APP_PIN_MOTION,
    APP_PIN_CHARGE_DET,
    APP_PIN_MOTOR_TRIG,
    APP_PIN_MOTOR_I2C_SDA,
    APP_PIN_MOTOR_I2C_SCL,
    APP_PIN_UART_TX,
    APP_PIN_UART_RX,
    APP_PIN_UART_RTS,
    APP_PIN_UART_CTS,
    APP_PIN_COUNT,
} app_board_pin_id_t;

typedef struct {
    app_hw_rev_t hw_rev;
    u8 has_charge_detect;
    u8 has_uart_flow_control;
    u8 has_motor_driver;
} app_board_info_t;

void app_board_init(void);
const app_board_info_t *app_board_get_info(void);
u32 app_board_get_pin(app_board_pin_id_t pin_id);
app_status_t app_board_self_check(void);

#endif
