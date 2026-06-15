#ifndef USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_TIME_H
#define USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_TIME_H

#include <stdint.h>

void Platform_Time_DelayMs(uint32_t delayMs);
uint32_t Platform_Time_GetTickMs(void);

#endif /* USER_TESTHOST_SERVO_DRIVER_STUBS_PLATFORM_TIME_H */
