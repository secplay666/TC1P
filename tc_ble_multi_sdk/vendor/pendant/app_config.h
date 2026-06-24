/********************************************************************************************************
 * @file    app_config.h
 *
 * @brief   This is the header file for BLE SDK
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
#pragma once

#include "config.h"

#define TEST_EXT_SCAN                                   8
#define FEATURE_TEST_MODE                               TEST_EXT_SCAN

#if (FEATURE_TEST_MODE == TEST_EXT_SCAN)


#define ACL_CENTRAL_MAX_NUM								1
#define ACL_PERIPHR_MAX_NUM								1
#define MASTER_ACL_PERIPHR_MAX_NUM						(ACL_CENTRAL_MAX_NUM+ACL_PERIPHR_MAX_NUM)

#define PENDANT_USB_ENABLE                             1
#define PENDANT_USB_POLL_INTERVAL_US                  2000
#define PENDANT_USB_AUTO_OFF_MS                       15000
#define PENDANT_EXT_ADV_ENABLE                        1
#define APP_BLE_ENABLE_DISCOVERY_SCAN                 1
#define APP_HOST_ENABLE_ADV_TRANSPORT                 0
#define PENDANT_BLE_AUTO_START                        1
#define PENDANT_WATCHDOG_ENABLE                       1
#define PENDANT_WATCHDOG_TIMEOUT_MS                   3000
#define PENDANT_DEMO_CENTRAL_CONNECT_ENABLE           0

#if (PENDANT_USB_ENABLE)
#define MODULE_USB_ENABLE                              1
#define USB_PRINTER_ENABLE                             1
#define USB_MOUSE_ENABLE                               0
#define USB_KEYBOARD_ENABLE                            0
#define USB_CUSTOM_HID_REPORT                          0
#define ID_VENDOR                                      0x248a
#define ID_PRODUCT_BASE                                0x8800
#define STRING_VENDOR                                  L"Telink"
#define STRING_PRODUCT                                 L"Glimmer"
#define STRING_SERIAL                                  L"TLSR8258"
#endif




///////////////////////// Feature Configuration////////////////////////////////////////////////
#define ACL_PERIPHR_SMP_ENABLE						0   //1 for smp,  0 no security
#define ACL_CENTRAL_SMP_ENABLE						0   //1 for smp,  0 no security


///////////////////////// DEBUG  Configuration ////////////////////////////////////////////////
#define DEBUG_GPIO_ENABLE							0
#define UART_PRINT_DEBUG_ENABLE                     0  //printf

#define APP_LOG_EN							1
#define APP_SMP_LOG_EN						0
#define APP_KEY_LOG_EN						1
#define APP_CONTR_EVENT_LOG_EN				1  //controller event log
#define APP_HOST_EVENT_LOG_EN				1  //host event log

#define APP_DEFAULT_HID_BATTERY_OTA_ATTRIBUTE_TABLE			0


/////////////////////// Board Select Configuration ///////////////////////////////
#if (CHIP_TYPE == CHIP_TYPE_825x)
	//Only support BOARD_825X_EVK_C1T139A30
	#define BOARD_SELECT							BOARD_825X_EVK_C1T139A30
#elif (CHIP_TYPE == CHIP_TYPE_827x)
	//Only support BOARD_827X_EVK_C1T197A30
	#define BOARD_SELECT							BOARD_827X_EVK_C1T197A30
#elif (CHIP_TYPE == CHIP_TYPE_TC321X)
	//Only support BOARD_TC321X_EVK_C1T357A20 & BOARD_TC321X_EVK_C1T357A20_V2_1
	#define BOARD_SELECT							BOARD_TC321X_EVK_C1T357A20_V2_1
#endif


///////////////////////// UI Configuration ////////////////////////////////////////////////////
#define UI_LED_ENABLE								0
#define	UI_KEYBOARD_ENABLE							0


/////////////////// Clock  /////////////////////////////////
#define CLOCK_SYS_CLOCK_HZ  							32000000




#include "../common/default_config.h"

#endif
