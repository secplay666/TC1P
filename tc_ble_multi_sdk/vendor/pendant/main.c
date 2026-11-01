/********************************************************************************************************
 * @file    main.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    2020.06
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
#include "app_config.h"
#include "app.h"
#include "app_ota.h"
#include "app_pendant.h"
#include "pm/app_pm.h"
#include "application/usbstd/usb.h"

#if (FEATURE_TEST_MODE == TEST_EXT_SCAN)




/**
 * @brief   IRQ handler
 * @param   none.
 * @return  none.
 */
_attribute_ram_code_ void irq_handler(void)
{
    DBG_CHN15_HIGH;

	blc_sdk_irq_handler();

	DBG_CHN15_LOW;
}

/**
 * @brief		This is main function
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ int main(void)
{
	app_ota_configure_boot();
	/* No 32K crystal is fitted: sleep timing runs on the internal 32K RC.
	 * This also binds cpu_sleep_wakeup, which is NULL until selected. */
	blc_pm_select_internal_32k_crystal();

	#if(MCU_CORE_TYPE == MCU_CORE_825x)
		cpu_wakeup_init();
	#elif(MCU_CORE_TYPE == MCU_CORE_827x || MCU_CORE_TYPE == MCU_CORE_TC321X)
		cpu_wakeup_init(DCDC_LDO_MODE, INTERNAL_CAP_XTAL24M);
	#endif

	#if(MCU_CORE_TYPE == MCU_CORE_TC321X)
		wd_32k_stop();
	#endif

	/* detect if MCU is wake_up from deep retention mode */
	app_pendant_trace_set(0x01); /* boot trace */
	int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  //MCU deep retention wakeUp

	
	clock_init(SYS_CLK_TYPE);
	app_pendant_trace_set(0x02); /* boot trace */

#if (PENDANT_WATCHDOG_ENABLE)
	/* Start the watchdog before any init that could hang, so a bad image
	 * resets instead of stalling forever (the OTA guard then rolls back). */
	wd_set_interval_ms(PENDANT_WATCHDOG_TIMEOUT_MS, CLOCK_SYS_CLOCK_HZ / 1000);
	wd_clear();
	wd_start();
#endif

	rf_drv_ble_init();
	app_pendant_trace_set(0x03); /* boot trace */

	gpio_init(!deepRetWakeUp);
	app_pendant_trace_set(0x04); /* boot trace */

	app_pm_handle_boot(deepRetWakeUp);

#if (PENDANT_USB_ENABLE)
	usb_init();
#endif


	if(deepRetWakeUp){ //MCU wake_up from deepSleep retention mode
		user_init_deepRetn();
	}
	else{ //MCU power_on or wake_up from deepSleep mode
		user_init_normal();
	}

	irq_enable();

	while(1)
	{
		main_loop();
	}
	return 0;
}

#endif
