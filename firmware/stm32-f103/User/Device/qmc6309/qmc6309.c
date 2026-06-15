#include "qmc6309.h"

#include "app_config.h"
#include "platform_i2c.h"
#include "platform_time.h"
#include "platform_uart.h"

#define QMC6309_REG_CHIP_ID 0x00U
#define QMC6309_REG_DATA_X_L 0x01U
#define QMC6309_REG_STATUS 0x09U
#define QMC6309_REG_CTRL_1 0x0AU
#define QMC6309_REG_CTRL_2 0x0BU

#define QMC6309_CHIP_ID 0x90U
#define QMC6309_STATUS_DRDY 0x01U
#define QMC6309_STATUS_OVFL 0x02U

#define QMC6309_CTRL_1_NORMAL_OSR 0x61U
#define QMC6309_CTRL_2_200HZ_32G 0x40U
#define QMC6309_CTRL_2_STANDBY 0x00U
#define QMC6309_CTRL_2_SOFT_RESET 0x80U

#define QMC6309_DATA_READY_RETRY_COUNT 50U
#define QMC6309_DATA_READY_DELAY_MS 2U

static HAL_StatusTypeDef QMC6309_WriteRegister(uint8_t reg, uint8_t value);
static HAL_StatusTypeDef QMC6309_ReadRegister(uint8_t reg, uint8_t *value);
static HAL_StatusTypeDef QMC6309_ReadRegisters(uint8_t startReg, uint8_t *data, uint16_t size);
static uint8_t QMC6309_WaitDataReady(uint8_t *status);

uint8_t QMC6309_Init(void)
{
    uint8_t chipId = 0U;

    if (QMC6309_ReadRegister(QMC6309_REG_CHIP_ID, &chipId) != HAL_OK) {
        Platform_Uart_Print("[QMC6309] Chip ID read failed\r\n");
        return 0U;
    }

    if (chipId != QMC6309_CHIP_ID) {
        Platform_Uart_Printf("[QMC6309] Chip ID: 0x%02X (expected 0x90)\r\n", chipId);
        return 0U;
    }

    Platform_Uart_Print("[QMC6309] Chip ID OK: 0x90\r\n");

    if (QMC6309_WriteRegister(QMC6309_REG_CTRL_2, QMC6309_CTRL_2_SOFT_RESET) != HAL_OK) {
        Platform_Uart_Print("[QMC6309] Soft reset failed\r\n");
        return 0U;
    }
    Platform_Time_DelayMs(10U);

    if (QMC6309_WriteRegister(QMC6309_REG_CTRL_2, QMC6309_CTRL_2_STANDBY) != HAL_OK) {
        Platform_Uart_Print("[QMC6309] Reset release failed\r\n");
        return 0U;
    }
    Platform_Time_DelayMs(5U);

    if (QMC6309_WriteRegister(QMC6309_REG_CTRL_2, QMC6309_CTRL_2_200HZ_32G) != HAL_OK) {
        Platform_Uart_Print("[QMC6309] CTRL2 write failed\r\n");
        return 0U;
    }

    if (QMC6309_WriteRegister(QMC6309_REG_CTRL_1, QMC6309_CTRL_1_NORMAL_OSR) != HAL_OK) {
        Platform_Uart_Print("[QMC6309] CTRL1 write failed\r\n");
        return 0U;
    }

    Platform_Time_DelayMs(10U);
    Platform_Uart_Print("[QMC6309] Initialized (normal, 200Hz, 32G)\r\n");

    return 1U;
}

uint8_t QMC6309_ReadData(QMC6309_DataTypeDef *data)
{
    uint8_t raw[6];
    uint8_t status = 0U;

    if (data == NULL) {
        return 0U;
    }

    if (QMC6309_WaitDataReady(&status) == 0U) {
        return 0U;
    }

    if ((status & QMC6309_STATUS_OVFL) != 0U) {
        return 0U;
    }

    if (QMC6309_ReadRegisters(QMC6309_REG_DATA_X_L, raw, sizeof(raw)) != HAL_OK) {
        return 0U;
    }

    data->magX = (int16_t)(((uint16_t)raw[1] << 8U) | raw[0]);
    data->magY = (int16_t)(((uint16_t)raw[3] << 8U) | raw[2]);
    data->magZ = (int16_t)(((uint16_t)raw[5] << 8U) | raw[4]);

    return 1U;
}

uint8_t QMC6309_ReadStatus(uint8_t *status)
{
    if (status == NULL) {
        return 0U;
    }

    return (QMC6309_ReadRegister(QMC6309_REG_STATUS, status) == HAL_OK) ? 1U : 0U;
}

uint8_t QMC6309_ReadDiagnostics(QMC6309_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL) {
        return 0U;
    }

    if (QMC6309_ReadRegister(QMC6309_REG_CHIP_ID, &diagnostics->chipId) != HAL_OK) {
        return 0U;
    }

    if (QMC6309_ReadRegister(QMC6309_REG_STATUS, &diagnostics->status) != HAL_OK) {
        return 0U;
    }

    if (QMC6309_ReadRegister(QMC6309_REG_CTRL_1, &diagnostics->ctrl1) != HAL_OK) {
        return 0U;
    }

    if (QMC6309_ReadRegister(QMC6309_REG_CTRL_2, &diagnostics->ctrl2) != HAL_OK) {
        return 0U;
    }

    return 1U;
}

static uint8_t QMC6309_WaitDataReady(uint8_t *status)
{
    uint8_t attempt;
    uint8_t currentStatus = 0U;

    for (attempt = 0U; attempt < QMC6309_DATA_READY_RETRY_COUNT; attempt++) {
        if (QMC6309_ReadStatus(&currentStatus) == 0U) {
            return 0U;
        }

        if ((currentStatus & QMC6309_STATUS_DRDY) != 0U) {
            if (status != NULL) {
                *status = currentStatus;
            }
            return 1U;
        }

        Platform_Time_DelayMs(QMC6309_DATA_READY_DELAY_MS);
    }

    if (status != NULL) {
        *status = currentStatus;
    }

    return 0U;
}

static HAL_StatusTypeDef QMC6309_WriteRegister(uint8_t reg, uint8_t value)
{
    return Platform_I2c_WriteMem(APP_QMC6309_ADDR, reg, &value, 1U, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef QMC6309_ReadRegister(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return HAL_ERROR;
    }

    return Platform_I2c_ReadMem(APP_QMC6309_ADDR, reg, value, 1U, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef QMC6309_ReadRegisters(uint8_t startReg, uint8_t *data, uint16_t size)
{
    if ((data == NULL) || (size == 0U)) {
        return HAL_ERROR;
    }

    return Platform_I2c_ReadMem(APP_QMC6309_ADDR, startReg, data, size, HAL_MAX_DELAY);
}
