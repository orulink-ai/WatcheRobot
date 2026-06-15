#ifndef USER_DEVICE_TOUCH_SENSOR_H
#define USER_DEVICE_TOUCH_SENSOR_H

#include <stdint.h>

/*
 * 触摸传感器接口。
 * 当前仍然直接读取 GPIO 输入电平，不额外引入去抖或状态机。
 */

uint8_t TouchSensor_Read(void);

#endif /* USER_DEVICE_TOUCH_SENSOR_H */
