#include "ws2812.h"

#include "app_config.h"

#define WS2812_MAX_INSTANCES 2U
#define WS2812_BITS_PER_LED 24U
#define WS2812_COLOR_CHANNELS 3U
#define WS2812_MAX_DMA_BUFFER_SIZE ((APP_WS2812_LED_COUNT * WS2812_BITS_PER_LED) + APP_WS2812_RESET_SLOTS)

typedef struct {
    WS2812_ConfigTypeDef config;
    uint8_t colorBuffer[APP_WS2812_LED_COUNT][WS2812_COLOR_CHANNELS];
    uint16_t dmaBuffer[WS2812_MAX_DMA_BUFFER_SIZE];
    uint16_t activeLedCount;
    volatile uint8_t isBusy;
    uint8_t inUse;
} WS2812_InstanceTypeDef;

static WS2812_InstanceTypeDef s_ws2812Pool[WS2812_MAX_INSTANCES];

static WS2812_InstanceTypeDef *WS2812_GetInstance(WS2812_HandleTypeDef ws2812);
static void WS2812_EncodeDmaBuffer(WS2812_InstanceTypeDef *instance);

WS2812_HandleTypeDef WS2812_Init(const WS2812_ConfigTypeDef *config)
{
    WS2812_InstanceTypeDef *instance = NULL;
    uint8_t index;

    if ((config == NULL) || (config->pTimHandle == NULL)) {
        return NULL;
    }
    if ((config->ledCount == 0U) || (config->ledCount > APP_WS2812_LED_COUNT)) {
        return NULL;
    }
    if ((config->pulseHigh0 == 0U) || (config->pulseHigh1 == 0U) || (config->pulseHigh0 >= config->pulseHigh1)) {
        return NULL;
    }
    if ((config->resetSlots == 0U) || (config->resetSlots > APP_WS2812_RESET_SLOTS)) {
        return NULL;
    }

    for (index = 0U; index < WS2812_MAX_INSTANCES; index++) {
        if (s_ws2812Pool[index].inUse == 0U) {
            instance = &s_ws2812Pool[index];
            break;
        }
    }

    if (instance == NULL) {
        return NULL;
    }

    instance->config = *config;
    instance->activeLedCount = config->ledCount;
    instance->isBusy = 0U;
    instance->inUse = 1U;

    for (index = 0U; index < APP_WS2812_LED_COUNT; index++) {
        instance->colorBuffer[index][0] = 0U;
        instance->colorBuffer[index][1] = 0U;
        instance->colorBuffer[index][2] = 0U;
    }

    return (WS2812_HandleTypeDef)instance;
}

WS2812_StatusTypeDef WS2812_SetPixel(WS2812_HandleTypeDef ws2812,
                                     uint16_t ledIndex,
                                     uint8_t red,
                                     uint8_t green,
                                     uint8_t blue)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);

    if (instance == NULL) {
        return WS2812_ERROR_PARAM;
    }
    if (ledIndex >= instance->activeLedCount) {
        return WS2812_ERROR_PARAM;
    }
    if (instance->isBusy != 0U) {
        return WS2812_ERROR_BUSY;
    }

    instance->colorBuffer[ledIndex][0] = red;
    instance->colorBuffer[ledIndex][1] = green;
    instance->colorBuffer[ledIndex][2] = blue;

    return WS2812_OK;
}

WS2812_StatusTypeDef WS2812_SetLedCount(WS2812_HandleTypeDef ws2812, uint16_t ledCount)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);
    uint16_t ledIndex;

    if (instance == NULL) {
        return WS2812_ERROR_PARAM;
    }
    if ((ledCount == 0U) || (ledCount > instance->config.ledCount)) {
        return WS2812_ERROR_PARAM;
    }
    if (instance->isBusy != 0U) {
        return WS2812_ERROR_BUSY;
    }

    instance->activeLedCount = ledCount;

    for (ledIndex = ledCount; ledIndex < instance->config.ledCount; ledIndex++) {
        instance->colorBuffer[ledIndex][0] = 0U;
        instance->colorBuffer[ledIndex][1] = 0U;
        instance->colorBuffer[ledIndex][2] = 0U;
    }

    return WS2812_OK;
}

uint16_t WS2812_GetLedCount(WS2812_HandleTypeDef ws2812)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);

    return (instance == NULL) ? 0U : instance->activeLedCount;
}

uint16_t WS2812_GetMaxLedCount(WS2812_HandleTypeDef ws2812)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);

    return (instance == NULL) ? 0U : instance->config.ledCount;
}

WS2812_StatusTypeDef WS2812_Fill(WS2812_HandleTypeDef ws2812, uint8_t red, uint8_t green, uint8_t blue)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);
    uint16_t ledIndex;

    if (instance == NULL) {
        return WS2812_ERROR_PARAM;
    }
    if (instance->isBusy != 0U) {
        return WS2812_ERROR_BUSY;
    }

    for (ledIndex = 0U; ledIndex < instance->activeLedCount; ledIndex++) {
        instance->colorBuffer[ledIndex][0] = red;
        instance->colorBuffer[ledIndex][1] = green;
        instance->colorBuffer[ledIndex][2] = blue;
    }

    for (ledIndex = instance->activeLedCount; ledIndex < instance->config.ledCount; ledIndex++) {
        instance->colorBuffer[ledIndex][0] = 0U;
        instance->colorBuffer[ledIndex][1] = 0U;
        instance->colorBuffer[ledIndex][2] = 0U;
    }

    return WS2812_OK;
}

WS2812_StatusTypeDef WS2812_Show(WS2812_HandleTypeDef ws2812, uint32_t timeoutMs)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);
    uint32_t startTickMs;

    if (instance == NULL) {
        return WS2812_ERROR_PARAM;
    }
    if (instance->isBusy != 0U) {
        return WS2812_ERROR_BUSY;
    }

    WS2812_EncodeDmaBuffer(instance);

    instance->isBusy = 1U;
    if (HAL_TIM_PWM_Start_DMA(instance->config.pTimHandle,
                              instance->config.TimChannel,
                              (uint32_t *)instance->dmaBuffer,
                              (uint16_t)((instance->config.ledCount * WS2812_BITS_PER_LED) + instance->config.resetSlots)) != HAL_OK) {
        instance->isBusy = 0U;
        return WS2812_ERROR;
    }

    startTickMs = HAL_GetTick();
    while (instance->isBusy != 0U) {
        if ((HAL_GetTick() - startTickMs) > timeoutMs) {
            (void)HAL_TIM_PWM_Stop_DMA(instance->config.pTimHandle, instance->config.TimChannel);
            __HAL_TIM_SET_COMPARE(instance->config.pTimHandle, instance->config.TimChannel, 0U);
            instance->isBusy = 0U;
            return WS2812_ERROR_TIMEOUT;
        }
    }

    (void)HAL_TIM_PWM_Stop_DMA(instance->config.pTimHandle, instance->config.TimChannel);
    __HAL_TIM_SET_COMPARE(instance->config.pTimHandle, instance->config.TimChannel, 0U);

    return WS2812_OK;
}

uint8_t WS2812_IsBusy(WS2812_HandleTypeDef ws2812)
{
    WS2812_InstanceTypeDef *instance = WS2812_GetInstance(ws2812);

    return (instance == NULL) ? 0U : instance->isBusy;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    uint8_t index;

    for (index = 0U; index < WS2812_MAX_INSTANCES; index++) {
        if ((s_ws2812Pool[index].inUse != 0U) && (s_ws2812Pool[index].config.pTimHandle == htim)) {
            s_ws2812Pool[index].isBusy = 0U;
            return;
        }
    }
}

static WS2812_InstanceTypeDef *WS2812_GetInstance(WS2812_HandleTypeDef ws2812)
{
    if (ws2812 == NULL) {
        return NULL;
    }

    return (WS2812_InstanceTypeDef *)ws2812;
}

static void WS2812_EncodeDmaBuffer(WS2812_InstanceTypeDef *instance)
{
    uint16_t ledIndex;
    uint16_t bitIndex;
    uint16_t dmaIndex = 0U;

    for (ledIndex = 0U; ledIndex < instance->config.ledCount; ledIndex++) {
        uint32_t grbValue = ((uint32_t)instance->colorBuffer[ledIndex][1] << 16U) |
                            ((uint32_t)instance->colorBuffer[ledIndex][0] << 8U) |
                            (uint32_t)instance->colorBuffer[ledIndex][2];

        for (bitIndex = 0U; bitIndex < WS2812_BITS_PER_LED; bitIndex++) {
            uint32_t mask = 1UL << (23U - bitIndex);

            instance->dmaBuffer[dmaIndex++] = ((grbValue & mask) != 0U) ? instance->config.pulseHigh1
                                                                         : instance->config.pulseHigh0;
        }
    }

    while (dmaIndex < ((instance->config.ledCount * WS2812_BITS_PER_LED) + instance->config.resetSlots)) {
        instance->dmaBuffer[dmaIndex++] = 0U;
    }
}
