#include "platform_uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "platform_coproc_uart.h"
#include "usart.h"

static uint8_t s_uartRxByte;
static uint8_t s_uartRxBuffer[APP_UART_RX_BUFFER_SIZE];
static volatile uint16_t s_uartRxHead;
static volatile uint16_t s_uartRxTail;

static void Platform_Uart_StartReceiveIt(void);

void Platform_Uart_Print(const char *text)
{
    if (text == NULL) {
        return;
    }

    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), HAL_MAX_DELAY);
}

void Platform_Uart_Printf(const char *format, ...)
{
    char message[APP_UART_PRINT_BUFFER_SIZE];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    Platform_Uart_Print(message);
}

void Platform_Uart_Init(void)
{
    s_uartRxHead = 0U;
    s_uartRxTail = 0U;
    Platform_Uart_StartReceiveIt();
}

uint8_t Platform_Uart_TryReadByte(uint8_t *byte)
{
    uint16_t nextTail;

    if (byte == NULL) {
        return 0U;
    }

    if (s_uartRxHead == s_uartRxTail) {
        return 0U;
    }

    *byte = s_uartRxBuffer[s_uartRxTail];
    nextTail = (uint16_t)(s_uartRxTail + 1U);
    if (nextTail >= APP_UART_RX_BUFFER_SIZE) {
        nextTail = 0U;
    }
    s_uartRxTail = nextTail;

    return 1U;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t nextHead;

    if (huart != &huart1) {
        return;
    }

    nextHead = (uint16_t)(s_uartRxHead + 1U);
    if (nextHead >= APP_UART_RX_BUFFER_SIZE) {
        nextHead = 0U;
    }

    if (nextHead != s_uartRxTail) {
        s_uartRxBuffer[s_uartRxHead] = s_uartRxByte;
        s_uartRxHead = nextHead;
    }

    Platform_Uart_StartReceiveIt();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        Platform_CoprocUart_HandleError(huart);
        return;
    }

    if (huart != &huart1) {
        return;
    }

    __HAL_UART_CLEAR_OREFLAG(huart);
    Platform_Uart_StartReceiveIt();
}

static void Platform_Uart_StartReceiveIt(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_uartRxByte, 1U);
}
