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
#include "gpio.h"
#include "timer.h"
#include "application/uartinterface/uart_interface.h"
#include "app.h"


static u32 tick250ms = 0;

static void task250ms(void)
{
	static u8 counter = 0;
	static u8 sleepCounter = 0;

	if (counter % 4 == 0) 
	{
		// u_printf("Current ll state is %d\n", blc_ll_getCurrentState());
		blc_ll_setScanEnable (BLC_SCAN_ENABLE, DUP_FILTER_ENABLE);
		if (sleepCounter <= 60) 
		{
			// u_printf("sleepCounter is %d\n", sleepCounter);
			updateAdvData(sleepCounter, 20u);

			sleepCounter++;
		}
		else 
		{
			u_printf("Go to sleep !\n");
			sleep_us(500);
			sleepCounter = 0;
			cpu_set_gpio_wakeup (GPIO_PD1, Level_High,1);  //button pin pad low wakeUp suspend/deepSleep
			cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepSleep
		}
	}
	counter++;
}

/**
 * @brief   IRQ handler
 * @param   none.
 * @return  none.
 */
_attribute_ram_code_ void irq_handler(void)
{

	irq_blt_sdk_handler();

}


/**
 * @brief		This is main function
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ int main (void)    //must run in ramcode
{
	blc_pm_select_external_32k_crystal();

	cpu_wakeup_init();
	rf_drv_ble_init();

	gpio_init(1);
	clock_init(SYS_CLK_TYPE);

	// #if (MODULE_WATCHDOG_ENABLE)
	// 	wd_set_interval_ms(WATCHDOG_INIT_TIMEOUT,CLOCK_SYS_CLOCK_1MS);
	// 	wd_start();
	// #endif

	controllerInitialization();

    irq_enable();
	UARTIF_uartinit();

	gpio_set_input_en(GPIO_PD1, 1);

	u_printf("Wellcome !!\n");

	tick250ms = clock_time();
	while (1)
	{
		// #if (MODULE_WATCHDOG_ENABLE)
		// 	#if (MCU_CORE_TYPE == MCU_CORE_TC321X)
		// 		if (g_chip_version != CHIP_VERSION_A0)
		// 	#endif
		// 		{
		// 			wd_clear(); //clear watch dog
		// 		}
		// #endif
		// 	main_loop();
		blt_sdk_main_loop();

		if(clock_time_exceed(tick250ms, 250000))
		{
			tick250ms = clock_time();
			task250ms();
		}
	}
}

