/******************************************************************************
 * Copyright (C) 2021, 
 *
 *  
 *  
 *  
 *  
 *
 ******************************************************************************/
 
/******************************************************************************
 ** @file uart_interface.c
 **
 ** @brief Source file for uart_interface functions
 **
 ** @author MADS Team 
 **
 ******************************************************************************/

/******************************************************************************
 * Include files
 ******************************************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "uart.h"
#include "common/types.h"

#define BUFF_DATA_LEN     256
volatile u8 rec_buff[BUFF_DATA_LEN] __attribute__((aligned(4))) = {0};


void UARTIF_uartinit(void)
{
	//note: dma addr must be set first before any other uart initialization!
	uart_recbuff_init( (u8 *)rec_buff, BUFF_DATA_LEN);

	uart_gpio_set(UART_TX_PB1, UART_RX_PB0);

	uart_reset();  //will reset uart digital registers from 0x90 ~ 0x9f, so uart setting must set after this reset

	//baud rate: 115200
	// #if (CLOCK_SYS_CLOCK_HZ == 16000000)
		uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);
	// #elif (CLOCK_SYS_CLOCK_HZ == 24000000)
	// 	uart_init(12, 15, PARITY_NONE, STOP_BIT_ONE);
	// #elif (CLOCK_SYS_CLOCK_HZ == 32000000)
	// 	uart_init(30, 8, PARITY_NONE, STOP_BIT_ONE);
	// #elif (CLOCK_SYS_CLOCK_HZ == 48000000)
	// 	uart_init(25, 15, PARITY_NONE, STOP_BIT_ONE);
	// #endif

	// uart_dma_enable(1, 1); 	//uart data in hardware buffer moved by dma, so we need enable them first

	// irq_set_mask(FLD_IRQ_DMA_EN);
	// dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);   	//uart Rx/Tx dma irq enable

	uart_irq_enable(0, 0);  	//uart Rx/Tx irq no need, disable them
}

// void UARTIF_uartPrintf(const char *format, ...)
// {
//     char buffer[256]; // 缓冲区，用于存储格式化后的字符串
//     va_list args;     // 可变参数列表
//     u8 len = 0;
//     u8 i = 0;

//     // 初始化可变参数
//     va_start(args, format);

//     // 格式化字符串并存入 buffer 中
//     len = vsnprintf(buffer, sizeof(buffer), format, args);

//     // 清理可变参数列表
//     va_end(args);

//     // 如果格式化成功，逐字节发送字符串
//     if (len > 0) {
//         for (i = 0; i < len; i++) 
//         {
//             // 调用 uart_ndma_send_byte 发送字符
//             uart_ndma_send_byte((u8)buffer[i]);
//         }
//     }
// }
