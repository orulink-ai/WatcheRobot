#ifndef USER_PLATFORM_STM32_PLATFORM_I2C_H
#define USER_PLATFORM_STM32_PLATFORM_I2C_H

#include <stdint.h>

#include "main.h"

/*
 * I2C 平台封装。
 * 接口统一使用 7bit 设备地址，内部再转换为 HAL 所需的左移地址格式。
 */

HAL_StatusTypeDef Platform_I2c_IsDeviceReady(uint16_t address7bit, uint32_t trials, uint32_t timeoutMs);
HAL_StatusTypeDef Platform_I2c_Write(uint16_t address7bit, const uint8_t *data, uint16_t size, uint32_t timeoutMs);
HAL_StatusTypeDef Platform_I2c_Read(uint16_t address7bit, uint8_t *data, uint16_t size, uint32_t timeoutMs);
HAL_StatusTypeDef Platform_I2c_WriteMem(uint16_t address7bit, uint8_t memAddress, const uint8_t *data, uint16_t size, uint32_t timeoutMs);
HAL_StatusTypeDef Platform_I2c_ReadMem(uint16_t address7bit, uint8_t memAddress, uint8_t *data, uint16_t size, uint32_t timeoutMs);
uint32_t Platform_I2c_GetError(void);
HAL_I2C_StateTypeDef Platform_I2c_GetState(void);
uint8_t Platform_I2c_IsRemapped(void);
void Platform_I2c_ReadRawRegisters(uint32_t *gpiobCrl, uint32_t *gpiobIdr, uint32_t *gpiobOdr, uint32_t *afioMapr);
void Platform_I2c_ReadBusLevels(uint8_t *sclHigh, uint8_t *sdaHigh);
void Platform_I2c_RecoverBus(void);
void Platform_I2c_PinTest(void (*printLine)(const char *label, uint8_t sclHigh, uint8_t sdaHigh));

#endif /* USER_PLATFORM_STM32_PLATFORM_I2C_H */
