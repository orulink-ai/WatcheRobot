#ifndef USER_BOARD_BOARD_PINS_H
#define USER_BOARD_BOARD_PINS_H

#include "main.h"

/*
 * 板级引脚定义。
 * 这里仅保留“板子如何接线”的信息，不放设备协议和业务逻辑。
 */

/* 身体板与 Watcher 头部板通信使用的 UART2 引脚。 */
#define BOARD_USART2_TX_PORT GPIOA
#define BOARD_USART2_TX_PIN  GPIO_PIN_2
#define BOARD_USART2_RX_PORT GPIOA
#define BOARD_USART2_RX_PIN  GPIO_PIN_3

/* IP5306 IRQ 输入引脚。 */
#define BOARD_IP5306_IRQ_PORT IP5306_IRQ_GPIO_Port
#define BOARD_IP5306_IRQ_PIN  IP5306_IRQ_Pin

/* IP5306 KEY 控制引脚，当前用于驱动外部 NMOS 的栅极。 */
#define BOARD_IP5306_KEY_PORT IP5306_KEY_GPIO_Port
#define BOARD_IP5306_KEY_PIN  IP5306_KEY_Pin

/* 触摸传感器输入引脚。 */
#define BOARD_TOUCH_SENSOR_PORT GPIOA
#define BOARD_TOUCH_SENSOR_PIN GPIO_PIN_4

#endif /* USER_BOARD_BOARD_PINS_H */
