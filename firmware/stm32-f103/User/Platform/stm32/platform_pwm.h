#ifndef USER_PLATFORM_STM32_PLATFORM_PWM_H
#define USER_PLATFORM_STM32_PLATFORM_PWM_H

#include <stdint.h>

#include "main.h"

/*
 * PWM 平台封装。
 * 舵机模块只关心“启动 PWM”和“写入脉宽”，不直接依赖 HAL 细节。
 */

HAL_StatusTypeDef Platform_Pwm_Start(TIM_HandleTypeDef *timHandle, uint32_t channel);
HAL_StatusTypeDef Platform_Pwm_WritePulse(TIM_HandleTypeDef *timHandle, uint32_t channel, uint16_t pulse);

#endif /* USER_PLATFORM_STM32_PLATFORM_PWM_H */
