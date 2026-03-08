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
		// u_u_printf("Current ll state is %d\n", blc_ll_getCurrentState());
		blc_ll_setScanEnable (BLC_SCAN_ENABLE, DUP_FILTER_ENABLE);
		if (sleepCounter <= 60) 
		{
			// u_u_printf("sleepCounter is %d\n", sleepCounter);
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

	//---------------------iic-----------------------------//
		    unsigned char calib_result = 0; //store empty \0
		    unsigned char middle_result = 0;
		    //quit-standby
		    i2c_write_byte(0x07, 1, (unsigned char)0x48);
		    middle_result = i2c_read_byte(0x07, 1);
		    u_printf("write 0 in 0x07, read value is 0x%02x\n", middle_result);
		    //auto-calibration
		    i2c_write_byte(0x07, 1, (unsigned char)0x4B); // set auto-calib
		    middle_result = i2c_read_byte(0x07, 1);
		    u_printf("write 3 in 0x07, read value is 0x%02x\n", middle_result);
		    i2c_write_byte(0x08, 1, (unsigned char)0x80); // LRA_ERM SELECT
		    middle_result = i2c_read_byte(0x08, 1);
		    u_printf("write 0x80 in 0x08, read value is 0x%02x\n", middle_result);
		    i2c_write_byte(0x1F, 1, (unsigned char)0x46); // RATED_VOLTAGE
		    middle_result = i2c_read_byte(0x1F, 1);
		    u_printf("write 0x46 in 0x1F, read value is 0x%02x\n", middle_result);
		    i2c_write_byte(0x20, 1, (unsigned char)0x5C); // OD_CLAMP 0x20 in manual
		    middle_result = i2c_read_byte(0x20, 1);
		    u_printf("write 0x0A in 0x20, read value is 0x%02x\n", middle_result);
		    i2c_write_byte(0x27, 1, (unsigned char)0x10); // DRIVE_TIME 0x27 in manual
		    middle_result = i2c_read_byte(0x27, 1);
		    u_printf("write 0x10 in 0x27, read value is 0x%02x\n", middle_result);
		    i2c_write_byte(0x0C, 1, (unsigned char)0x01); // set Go
		    middle_result = i2c_read_byte(0x0C, 1);
		    u_printf("write 0x01 in 0x0C, read value is 0x%02x\n", middle_result);

		    calib_result = i2c_read_byte(0x01, 1);
		    u_printf("read 0x01, read value is 0x%02x\n", calib_result);

		    if ((calib_result & 0x80) != (unsigned char)0x00){
		    	u_printf("error, value is %c\n", calib_result);
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    	u_printf("calib failed!\n");
		    }

		    calib_result = i2c_read_byte(0x01, 1);
		    u_printf("read 0x01 again, read value is 0x%02x\n", calib_result);

		    char write_result = 0;
		    //read 0x07
			write_result = i2c_read_byte(0x07, 1);
			u_printf("read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
			i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
			u_printf("read 0x07, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x08, 1, 0x88);

			//write wave form
			i2c_write_byte(0x0F, 1, 0x2F);//wave form
			write_result = i2c_read_byte(0x0F, 1);
			u_printf("read 0x0F, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x10, 1, 0x82);//delay
			write_result = i2c_read_byte(0x10, 1);
			u_printf("read 0x10, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x11, 1, 0x2F);//wave form
			write_result = i2c_read_byte(0x11, 1);
			u_printf("read 0x11, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x12, 1, 0x00);//delay
			write_result = i2c_read_byte(0x12, 1);
			u_printf("read 0x12, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x19, 1, 0x01);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("read 0x19, read value is 0x%02x\n", write_result);
			// set Go
			i2c_write_byte(0x0C, 1, (unsigned char)0x01);
			write_result = i2c_read_byte(0x0C, 1);
			u_printf("write 0x01 in 0x0C, read value is 0x%02x\n", write_result);
			//---------------------iic-----------------------------//

			//--------------------GPIO-------------------------//

			    //--set GPIO read and ADC pin--//
				gpio_set_func(GPIO_PB5 ,AS_GPIO); // motor control
			    gpio_set_func(GPIO_PA0 ,AS_GPIO); // vibrate sensor
			    //gpio_set_func(GPIO_PB1 ,AS_GPIO); //MCU Vbatt check
			    gpio_set_input_en(GPIO_PA0 ,1);
			    gpio_setup_up_down_resistor(GPIO_PA0, PM_PIN_PULLUP_10K);
			    gpio_set_input_en(GPIO_PB5 ,0);
			    //gpio_set_input_en(GPIO_PB1 ,1);
			    gpio_set_output_en(GPIO_PA0 ,0);
			    gpio_set_output_en(GPIO_PB5 ,1);
			    //gpio_set_output_en(GPIO_PB1 ,0);

			    gpio_write(GPIO_PB5, 1);
			//--------------------GPIO-------------------------//

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

