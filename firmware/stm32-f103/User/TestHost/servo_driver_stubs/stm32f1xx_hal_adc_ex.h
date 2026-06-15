#ifndef USER_TESTHOST_SERVO_DRIVER_STUBS_STM32F1XX_HAL_ADC_EX_H
#define USER_TESTHOST_SERVO_DRIVER_STUBS_STM32F1XX_HAL_ADC_EX_H

#include "main.h"

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc);

#endif /* USER_TESTHOST_SERVO_DRIVER_STUBS_STM32F1XX_HAL_ADC_EX_H */
