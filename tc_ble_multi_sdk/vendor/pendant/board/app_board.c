#include "app_board.h"
#include "drivers.h"

#ifndef APP_BOARD_HW_REV
#define APP_BOARD_HW_REV APP_HW_REV_NEW
#endif

static app_board_info_t s_board_info;
static u32 s_pins[APP_PIN_COUNT];

void app_board_init(void)
{
    s_board_info.hw_rev = APP_BOARD_HW_REV;
    s_board_info.has_charge_detect = 1;
    s_board_info.has_uart_flow_control = 1;
    s_board_info.has_motor_driver = 1;

    s_pins[APP_PIN_BAT_ADC] = GPIO_PB1;
    s_pins[APP_PIN_MOTION] = GPIO_PC1;
    s_pins[APP_PIN_CHARGE_DET] = GPIO_PA0;
    s_pins[APP_PIN_MOTOR_TRIG] = GPIO_PB5;
    s_pins[APP_PIN_MOTOR_I2C_SDA] = GPIO_PB6;
    s_pins[APP_PIN_MOTOR_I2C_SCL] = GPIO_PD7;
    s_pins[APP_PIN_UART_TX] = GPIO_PC2;
    s_pins[APP_PIN_UART_RX] = GPIO_PC3;
    s_pins[APP_PIN_UART_RTS] = GPIO_PC0;
    s_pins[APP_PIN_UART_CTS] = GPIO_PC4;

    gpio_set_input_en(s_pins[APP_PIN_MOTION], 1);
    gpio_set_input_en(s_pins[APP_PIN_CHARGE_DET], 1);
    gpio_set_output_en(s_pins[APP_PIN_MOTOR_TRIG], 1);
    gpio_write(s_pins[APP_PIN_MOTOR_TRIG], 0);
}

const app_board_info_t *app_board_get_info(void)
{
    return &s_board_info;
}

u32 app_board_get_pin(app_board_pin_id_t pin_id)
{
    if (pin_id >= APP_PIN_COUNT) {
        return 0;
    }
    return s_pins[pin_id];
}

app_status_t app_board_self_check(void)
{
    if (!s_pins[APP_PIN_MOTION] || !s_pins[APP_PIN_MOTOR_TRIG]) {
        return APP_ERR_STATE;
    }
    return APP_OK;
}
