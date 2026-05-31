/********************************************************************************************************
 * @file    main.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "application/uartinterface/uart_interface.h"
#include "app.h"
#include "pendant/app_pendant.h"

_attribute_ram_code_ void irq_handler(void)
{
	irq_blt_sdk_handler();
}

_attribute_ram_code_ int main(void)
{
	blc_pm_select_external_32k_crystal();

	cpu_wakeup_init();
	rf_drv_ble_init();

	gpio_init(1);
	clock_init(SYS_CLK_TYPE);

	controllerInitialization();
	UARTIF_uartinit();
	app_pendant_init();

	irq_enable();

	u_printf("Pendant boot\r\n");

	while (1)
	{
		blt_sdk_main_loop();
		app_pendant_poll();
	}
}
