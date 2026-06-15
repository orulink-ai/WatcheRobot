#include "platform_pwm.h"

HAL_StatusTypeDef Platform_Pwm_Start(TIM_HandleTypeDef *timHandle, uint32_t channel)
{
    return HAL_TIM_PWM_Start(timHandle, channel);
}

HAL_StatusTypeDef Platform_Pwm_WritePulse(TIM_HandleTypeDef *timHandle, uint32_t channel, uint16_t pulse)
{
    __HAL_TIM_SET_COMPARE(timHandle, channel, pulse);
    return HAL_OK;
}
