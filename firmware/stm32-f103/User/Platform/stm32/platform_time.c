#include "platform_time.h"

#include "main.h"

void Platform_Time_DelayMs(uint32_t delayMs)
{
    HAL_Delay(delayMs);
}

uint32_t Platform_Time_GetTickMs(void)
{
    return HAL_GetTick();
}
