#ifndef USER_DEVICE_BOTTOM_IR_H
#define USER_DEVICE_BOTTOM_IR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

#define BOTTOM_IR_ADC_MAX 4095U
#define BOTTOM_IR_VREF_MV 3300U

typedef enum {
    BOTTOM_IR_OK = 0x00U,
    BOTTOM_IR_ERROR = 0x01U,
    BOTTOM_IR_ERROR_PARAM = 0x02U,
    BOTTOM_IR_ERROR_INIT = 0x03U
} BottomIr_StatusTypeDef;

typedef struct {
    ADC_HandleTypeDef *pAdcHandle;
    uint8_t sampleIndex;
    uint8_t averageSamples;
    uint16_t thresholdRaw;
    uint8_t activeAboveThreshold;
    uint32_t adcTimeoutMs;
} BottomIr_ConfigTypeDef;

typedef struct {
    uint16_t raw;
    uint16_t millivolts;
    uint16_t thresholdRaw;
    uint8_t aboveThreshold;
    uint8_t active;
} BottomIr_SnapshotTypeDef;

BottomIr_StatusTypeDef BottomIr_Init(const BottomIr_ConfigTypeDef *config);
BottomIr_StatusTypeDef BottomIr_ReadSnapshot(const BottomIr_ConfigTypeDef *config, BottomIr_SnapshotTypeDef *snapshot);
uint16_t BottomIr_ConvertRawToMv(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* USER_DEVICE_BOTTOM_IR_H */
