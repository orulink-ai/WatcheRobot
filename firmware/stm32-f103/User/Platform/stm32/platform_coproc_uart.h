#ifndef USER_PLATFORM_STM32_PLATFORM_COPROC_UART_H
#define USER_PLATFORM_STM32_PLATFORM_COPROC_UART_H

#include "stm32f1xx_hal.h"

void Platform_CoprocUart_Init(void);
void Platform_CoprocUart_Poll(void);
void Platform_CoprocUart_HandleError(UART_HandleTypeDef *huart);

#endif /* USER_PLATFORM_STM32_PLATFORM_COPROC_UART_H */
