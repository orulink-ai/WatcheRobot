#include "platform_i2c.h"

#include "i2c.h"
#include "platform_time.h"

#define PLATFORM_I2C_SCL_PORT GPIOB
#define PLATFORM_I2C_SCL_PIN GPIO_PIN_6
#define PLATFORM_I2C_SDA_PORT GPIOB
#define PLATFORM_I2C_SDA_PIN GPIO_PIN_7

static uint16_t Platform_I2c_ToHalAddress(uint16_t address7bit)
{
    return (uint16_t)(address7bit << 1U);
}

HAL_StatusTypeDef Platform_I2c_IsDeviceReady(uint16_t address7bit, uint32_t trials, uint32_t timeoutMs)
{
    return HAL_I2C_IsDeviceReady(&hi2c1, Platform_I2c_ToHalAddress(address7bit), trials, timeoutMs);
}

HAL_StatusTypeDef Platform_I2c_Write(uint16_t address7bit, const uint8_t *data, uint16_t size, uint32_t timeoutMs)
{
    return HAL_I2C_Master_Transmit(
        &hi2c1,
        Platform_I2c_ToHalAddress(address7bit),
        (uint8_t *)data,
        size,
        timeoutMs);
}

HAL_StatusTypeDef Platform_I2c_Read(uint16_t address7bit, uint8_t *data, uint16_t size, uint32_t timeoutMs)
{
    return HAL_I2C_Master_Receive(
        &hi2c1,
        Platform_I2c_ToHalAddress(address7bit),
        data,
        size,
        timeoutMs);
}

HAL_StatusTypeDef Platform_I2c_WriteMem(uint16_t address7bit, uint8_t memAddress, const uint8_t *data, uint16_t size, uint32_t timeoutMs)
{
    return HAL_I2C_Mem_Write(
        &hi2c1,
        Platform_I2c_ToHalAddress(address7bit),
        memAddress,
        I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)data,
        size,
        timeoutMs);
}

HAL_StatusTypeDef Platform_I2c_ReadMem(uint16_t address7bit, uint8_t memAddress, uint8_t *data, uint16_t size, uint32_t timeoutMs)
{
    return HAL_I2C_Mem_Read(
        &hi2c1,
        Platform_I2c_ToHalAddress(address7bit),
        memAddress,
        I2C_MEMADD_SIZE_8BIT,
        data,
        size,
        timeoutMs);
}

uint32_t Platform_I2c_GetError(void)
{
    return HAL_I2C_GetError(&hi2c1);
}

HAL_I2C_StateTypeDef Platform_I2c_GetState(void)
{
    return HAL_I2C_GetState(&hi2c1);
}

uint8_t Platform_I2c_IsRemapped(void)
{
    return ((AFIO->MAPR & AFIO_MAPR_I2C1_REMAP) != 0U) ? 1U : 0U;
}

void Platform_I2c_ReadRawRegisters(uint32_t *gpiobCrl, uint32_t *gpiobIdr, uint32_t *gpiobOdr, uint32_t *afioMapr)
{
    if (gpiobCrl != NULL) {
        *gpiobCrl = GPIOB->CRL;
    }

    if (gpiobIdr != NULL) {
        *gpiobIdr = GPIOB->IDR;
    }

    if (gpiobOdr != NULL) {
        *gpiobOdr = GPIOB->ODR;
    }

    if (afioMapr != NULL) {
        *afioMapr = AFIO->MAPR;
    }
}

void Platform_I2c_ReadBusLevels(uint8_t *sclHigh, uint8_t *sdaHigh)
{
    if (sclHigh != NULL) {
        *sclHigh = (HAL_GPIO_ReadPin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN) == GPIO_PIN_SET) ? 1U : 0U;
    }

    if (sdaHigh != NULL) {
        *sdaHigh = (HAL_GPIO_ReadPin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN) == GPIO_PIN_SET) ? 1U : 0U;
    }
}

void Platform_I2c_RecoverBus(void)
{
    GPIO_InitTypeDef gpioInit;
    uint8_t index;

    HAL_I2C_DeInit(&hi2c1);

    gpioInit.Pin = PLATFORM_I2C_SCL_PIN | PLATFORM_I2C_SDA_PIN;
    gpioInit.Mode = GPIO_MODE_OUTPUT_OD;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpioInit);

    HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);

    for (index = 0U; index < 9U; index++) {
        HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_RESET);
        Platform_Time_DelayMs(1U);
        HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_SET);
        Platform_Time_DelayMs(1U);
    }

    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_RESET);
    Platform_Time_DelayMs(1U);
    HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);
    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);

    MX_I2C1_Init();
}

void Platform_I2c_PinTest(void (*printLine)(const char *label, uint8_t sclHigh, uint8_t sdaHigh))
{
    GPIO_InitTypeDef gpioInit;
    uint8_t sclHigh = 0U;
    uint8_t sdaHigh = 0U;

    HAL_I2C_DeInit(&hi2c1);

    gpioInit.Pin = PLATFORM_I2C_SCL_PIN | PLATFORM_I2C_SDA_PIN;
    gpioInit.Mode = GPIO_MODE_INPUT;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpioInit);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("INPUT_PULLUP", sclHigh, sdaHigh);
    }

    gpioInit.Pin = PLATFORM_I2C_SCL_PIN | PLATFORM_I2C_SDA_PIN;
    gpioInit.Mode = GPIO_MODE_OUTPUT_OD;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpioInit);

    HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("OD_RELEASE", sclHigh, sdaHigh);
    }

    HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_RESET);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("SCL_DRIVE_LOW", sclHigh, sdaHigh);
    }

    HAL_GPIO_WritePin(PLATFORM_I2C_SCL_PORT, PLATFORM_I2C_SCL_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("SCL_RELEASE", sclHigh, sdaHigh);
    }

    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_RESET);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("SDA_DRIVE_LOW", sclHigh, sdaHigh);
    }

    HAL_GPIO_WritePin(PLATFORM_I2C_SDA_PORT, PLATFORM_I2C_SDA_PIN, GPIO_PIN_SET);
    Platform_Time_DelayMs(1U);
    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    if (printLine != NULL) {
        printLine("SDA_RELEASE", sclHigh, sdaHigh);
    }

    MX_I2C1_Init();
}
