#ifndef USER_PLATFORM_STM32_PLATFORM_UART_H
#define USER_PLATFORM_STM32_PLATFORM_UART_H

#include <stdint.h>

/*
 * UART 平台封装。
 * 当前仍使用 USART1，并保持阻塞发送、轮询接收的行为不变。
 */

void Platform_Uart_Print(const char *text);
void Platform_Uart_Printf(const char *format, ...);
void Platform_Uart_Init(void);
uint8_t Platform_Uart_TryReadByte(uint8_t *byte);

#endif /* USER_PLATFORM_STM32_PLATFORM_UART_H */
