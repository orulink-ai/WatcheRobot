#ifndef USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H
#define USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H

#include <stdint.h>

typedef struct {
    uint32_t instanceId;
} TIM_HandleTypeDef;

typedef struct {
    uint32_t CR1;
    uint32_t SQR1;
    uint32_t SQR2;
    uint32_t SQR3;
    uint32_t SMPR1;
    uint32_t SMPR2;
} ADC_TypeDef;

typedef struct {
    uint32_t ScanConvMode;
    uint32_t NbrOfConversion;
} ADC_InitTypeDef;

typedef struct {
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;

typedef struct {
    uint32_t instanceId;
    ADC_TypeDef *Instance;
    ADC_InitTypeDef Init;
} ADC_HandleTypeDef;

typedef int HAL_StatusTypeDef;

#define HAL_OK 0
#define HAL_ERROR 1

#define TIM_CHANNEL_1 1U

#define ADC_SCAN_DISABLE 0U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_239CYCLES_5 239U

#endif /* USER_TESTHOST_SERVO_DRIVER_STUBS_MAIN_H */
