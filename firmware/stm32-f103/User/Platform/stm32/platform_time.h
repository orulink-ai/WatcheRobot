#ifndef USER_PLATFORM_STM32_PLATFORM_TIME_H
#define USER_PLATFORM_STM32_PLATFORM_TIME_H

#include <stdint.h>

/*
 * 时间平台封装。
 * 当前保持 HAL_Delay/HAL_GetTick 语义不变，方便后续切换非阻塞调度时集中修改。
 */

void Platform_Time_DelayMs(uint32_t delayMs);
uint32_t Platform_Time_GetTickMs(void);

#endif /* USER_PLATFORM_STM32_PLATFORM_TIME_H */
