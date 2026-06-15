#include "bottom_ir.h"

static BottomIr_StatusTypeDef BottomIr_ReadOneRaw(const BottomIr_ConfigTypeDef *config, uint16_t *raw);

BottomIr_StatusTypeDef BottomIr_Init(const BottomIr_ConfigTypeDef *config)
{
    if ((config == NULL) || (config->pAdcHandle == NULL) || (config->sampleIndex == 0U)) {
        return BOTTOM_IR_ERROR_PARAM;
    }

    return (HAL_ADCEx_Calibration_Start(config->pAdcHandle) == HAL_OK) ? BOTTOM_IR_OK : BOTTOM_IR_ERROR_INIT;
}

BottomIr_StatusTypeDef BottomIr_ReadSnapshot(const BottomIr_ConfigTypeDef *config, BottomIr_SnapshotTypeDef *snapshot)
{
    uint32_t sum = 0U;
    uint16_t raw = 0U;
    uint8_t samples;
    uint8_t index;

    if ((config == NULL) || (snapshot == NULL) || (config->pAdcHandle == NULL) || (config->sampleIndex == 0U)) {
        return BOTTOM_IR_ERROR_PARAM;
    }

    samples = (config->averageSamples == 0U) ? 1U : config->averageSamples;
    for (index = 0U; index < samples; index++) {
        if (BottomIr_ReadOneRaw(config, &raw) != BOTTOM_IR_OK) {
            return BOTTOM_IR_ERROR;
        }
        sum += raw;
    }

    snapshot->raw = (uint16_t)(sum / samples);
    snapshot->millivolts = BottomIr_ConvertRawToMv(snapshot->raw);
    snapshot->thresholdRaw = config->thresholdRaw;
    snapshot->aboveThreshold = (snapshot->raw >= config->thresholdRaw) ? 1U : 0U;
    snapshot->active = (config->activeAboveThreshold != 0U) ? snapshot->aboveThreshold : (uint8_t)(snapshot->aboveThreshold == 0U);

    return BOTTOM_IR_OK;
}

uint16_t BottomIr_ConvertRawToMv(uint16_t raw)
{
    if (raw > BOTTOM_IR_ADC_MAX) {
        raw = BOTTOM_IR_ADC_MAX;
    }

    return (uint16_t)(((uint32_t)raw * BOTTOM_IR_VREF_MV) / BOTTOM_IR_ADC_MAX);
}

static BottomIr_StatusTypeDef BottomIr_ReadOneRaw(const BottomIr_ConfigTypeDef *config, uint16_t *raw)
{
    uint8_t index;

    if ((config == NULL) || (raw == NULL) || (config->pAdcHandle == NULL) || (config->sampleIndex == 0U)) {
        return BOTTOM_IR_ERROR_PARAM;
    }

    if (HAL_ADC_Start(config->pAdcHandle) != HAL_OK) {
        return BOTTOM_IR_ERROR;
    }

    for (index = 1U; index <= config->sampleIndex; index++) {
        if (HAL_ADC_PollForConversion(config->pAdcHandle, config->adcTimeoutMs) != HAL_OK) {
            (void)HAL_ADC_Stop(config->pAdcHandle);
            return BOTTOM_IR_ERROR;
        }
        *raw = (uint16_t)HAL_ADC_GetValue(config->pAdcHandle);
    }

    return (HAL_ADC_Stop(config->pAdcHandle) == HAL_OK) ? BOTTOM_IR_OK : BOTTOM_IR_ERROR;
}
