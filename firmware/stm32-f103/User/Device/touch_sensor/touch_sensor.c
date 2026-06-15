#include "touch_sensor.h"

#include "board_pins.h"

uint8_t TouchSensor_Read(void)
{
    return (uint8_t)HAL_GPIO_ReadPin(BOARD_TOUCH_SENSOR_PORT, BOARD_TOUCH_SENSOR_PIN);
}
