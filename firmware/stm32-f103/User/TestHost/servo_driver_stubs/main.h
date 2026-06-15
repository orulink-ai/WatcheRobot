#ifndef USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H
#define USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H

#include <stdint.h>

typedef struct {
    uint32_t instanceId;
} TIM_HandleTypeDef;

typedef struct {
    uint32_t instanceId;
} ADC_HandleTypeDef;

typedef int HAL_StatusTypeDef;

#define HAL_OK 0
#define HAL_ERROR 1

#define TIM_CHANNEL_1 1U

#endif /* USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H */
