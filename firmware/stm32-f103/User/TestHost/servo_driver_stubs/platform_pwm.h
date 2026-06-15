#ifndef USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_PWM_H
#define USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_PWM_H

#include <stdint.h>

#include "main.h"

HAL_StatusTypeDef Platform_Pwm_Start(TIM_HandleTypeDef *timHandle, uint32_t channel);
HAL_StatusTypeDef Platform_Pwm_WritePulse(TIM_HandleTypeDef *timHandle, uint32_t channel, uint16_t pulse);

#endif /* USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_PWM_H */
