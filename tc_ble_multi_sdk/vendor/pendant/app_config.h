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


#define ACL_CENTRAL_MAX_NUM								0
#define ACL_PERIPHR_MAX_NUM								1
#define MASTER_ACL_PERIPHR_MAX_NUM						(ACL_CENTRAL_MAX_NUM+ACL_PERIPHR_MAX_NUM)

#define PENDANT_USB_ENABLE                             0
#define PENDANT_USB_POLL_INTERVAL_US                  2000
#define PENDANT_USB_AUTO_OFF_MS                       15000
#define PENDANT_EXT_ADV_ENABLE                        1
#define APP_BLE_ENABLE_DISCOVERY_SCAN                 0
// Product boards may fail to advertise if PC2/PC3 are switched to UART.
// Enable only on boards where those pins are dedicated debug UART pins.
#define PENDANT_UART_PINMUX_ENABLE                    0
// Keep RX opt-in: a noisy or floating PC3 can flood the debug shell and starve BLE.
#define PENDANT_UART_RX_PINMUX_ENABLE                 0
#define APP_HOST_ENABLE_ADV_TRANSPORT                 0
// 1 = development build: UART/BLE debug shell, PC-tool-only host commands
// (state/flash/motor/log/sleep/identity/factory) and LOG frames.
// 0 = release build: only what the Android app and OTA tooling use.
#ifndef PENDANT_DEBUG_BUILD
#define PENDANT_DEBUG_BUILD                           0
#endif
// 1 = RF test build: host command SET_TX_POWER changes the radio TX level
// at runtime (RAM only, back to +3 dBm on reboot). Independent of the debug
// build so power management stays as in release.
#ifndef PENDANT_RF_TEST_ENABLE
#define PENDANT_RF_TEST_ENABLE                        0
#endif
#define PENDANT_BLE_AUTO_START                        1
// Let the BLE stack suspend the MCU between advertising/connection events.
// Off in debug builds, whose UART shell is polled from the main loop.
// Idle deep sleep after PENDANT_IDLE_SLEEP_S with no motion, no app
// connection, not charging and no nearby pendant. Wakes on the vibration
// switch (confirmed by PENDANT_MOTION_WAKE_*) or on charging start.
#ifndef PENDANT_IDLE_SLEEP_ENABLE
#define PENDANT_IDLE_SLEEP_ENABLE                     1
#endif
#ifndef PENDANT_IDLE_SLEEP_S
#define PENDANT_IDLE_SLEEP_S                          300
#endif
// After a pin wake, stay awake only if switch toggles show up in at least
// MIN_SLOTS different SLOT_MS slots within WINDOW_MS. A single knock (one
// burst of contact bounce) or a glitch goes straight back to sleep.
#ifndef PENDANT_MOTION_WAKE_WINDOW_MS
#define PENDANT_MOTION_WAKE_WINDOW_MS                 2000
#endif
#ifndef PENDANT_MOTION_WAKE_SLOT_MS
#define PENDANT_MOTION_WAKE_SLOT_MS                   100
#endif
#ifndef PENDANT_MOTION_WAKE_MIN_SLOTS
#define PENDANT_MOTION_WAKE_MIN_SLOTS                 2
#endif
// Also suspend between connection events. Off by default: one dev board
// (SDK crystal-start warning set) advertised fine but could not hold a
// connection with it, while the product board and another dev board could.
#ifndef PENDANT_BLE_PM_CONN_SLEEP
#define PENDANT_BLE_PM_CONN_SLEEP                     0
#endif
#ifndef PENDANT_BLE_PM_ENABLE
#define PENDANT_BLE_PM_ENABLE                         (!PENDANT_DEBUG_BUILD)
#endif
#define PENDANT_WATCHDOG_ENABLE                       1
#define PENDANT_WATCHDOG_TIMEOUT_MS                   10000
#define PENDANT_DEMO_CENTRAL_CONNECT_ENABLE           0
// Keep BLE startup independent from optional haptic hardware probing.
#define PENDANT_MOTOR_BOOT_INIT_ENABLE                0
#define PENDANT_OTA_ENABLE                            1
/* Slot1 is 0x40000..0x6ffff on 512K flash. Telink OTA needs one 4K
 * scratch/reserved sector after the declared firmware area, so keep the
 * firmware cap at 184K and leave 0x6e000 for the OTA stack. 0x6f000 is
 * then available for Glimmer rollback backup metadata.
 */
#define PENDANT_OTA_FIRMWARE_MAX_K                    184
#define PENDANT_OTA_GUARD_ENABLE                      1
#define PENDANT_OTA_CONFIRM_DELAY_MS                  15000
#define PENDANT_OTA_MAX_PENDING_BOOT_ATTEMPTS         2
/* 0=off, 1=bypass all OTA writes, 2=pass START to otaWrite and bypass DATA,
 * 3=pass START/DATA0 to otaWrite and bypass DATA1+,
 * 4=pass START and first PENDANT_OTA_DIAG_REAL_DATA_LIMIT DATA packets. */
#define PENDANT_OTA_DIAG_BYPASS_OTAWRITE              0
#define PENDANT_OTA_DIAG_REAL_DATA_LIMIT              64

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
