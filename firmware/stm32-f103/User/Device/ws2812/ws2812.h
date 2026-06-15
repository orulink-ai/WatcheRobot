#ifndef USER_DEVICE_WS2812_H
#define USER_DEVICE_WS2812_H

#include <stdint.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *WS2812_HandleTypeDef;

typedef enum {
    WS2812_OK = 0x00U,
    WS2812_ERROR = 0x01U,
    WS2812_ERROR_PARAM = 0x02U,
    WS2812_ERROR_BUSY = 0x03U,
    WS2812_ERROR_TIMEOUT = 0x04U,
    WS2812_ERROR_NO_RESOURCE = 0x05U
} WS2812_StatusTypeDef;

typedef struct {
    TIM_HandleTypeDef *pTimHandle;
    uint32_t TimChannel;
    uint16_t ledCount;
    uint16_t pulseHigh0;
    uint16_t pulseHigh1;
    uint16_t resetSlots;
} WS2812_ConfigTypeDef;

WS2812_HandleTypeDef WS2812_Init(const WS2812_ConfigTypeDef *config);
WS2812_StatusTypeDef WS2812_SetPixel(WS2812_HandleTypeDef ws2812,
                                     uint16_t ledIndex,
                                     uint8_t red,
                                     uint8_t green,
                                     uint8_t blue);
WS2812_StatusTypeDef WS2812_SetLedCount(WS2812_HandleTypeDef ws2812, uint16_t ledCount);
uint16_t WS2812_GetLedCount(WS2812_HandleTypeDef ws2812);
uint16_t WS2812_GetMaxLedCount(WS2812_HandleTypeDef ws2812);
WS2812_StatusTypeDef WS2812_Fill(WS2812_HandleTypeDef ws2812, uint8_t red, uint8_t green, uint8_t blue);
WS2812_StatusTypeDef WS2812_Show(WS2812_HandleTypeDef ws2812, uint32_t timeoutMs);
uint8_t WS2812_IsBusy(WS2812_HandleTypeDef ws2812);

#ifdef __cplusplus
}
#endif

#endif /* USER_DEVICE_WS2812_H */
