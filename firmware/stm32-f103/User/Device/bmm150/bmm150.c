#include "bmm150.h"

#include "app_config.h"
#include "platform_i2c.h"
#include "platform_time.h"
#include "platform_uart.h"

#define BMM150_REG_CHIP_ID 0x40U
#define BMM150_REG_DATA_X_L 0x42U

static void BMM150_WriteRegister(uint8_t reg, uint8_t value);
static uint8_t BMM150_ReadRegister(uint8_t reg);

void BMM150_Init(void)
{
    uint8_t chipId = BMM150_ReadRegister(BMM150_REG_CHIP_ID);

    if (chipId != 0x32U) {
        Platform_Uart_Printf("[BMM150] Chip ID: 0x%02X (expected 0x32)\r\n", chipId);
        return;
    }

    Platform_Uart_Print("[BMM150] Chip ID OK: 0x32\r\n");

    BMM150_WriteRegister(0x4BU, 0x01U);
    Platform_Time_DelayMs(50U);

    BMM150_WriteRegister(0x4CU, 0x02U);
    Platform_Time_DelayMs(50U);

    Platform_Uart_Print("[BMM150] Initialized\r\n");
}

void BMM150_ReadData(BMM150_DataTypeDef *data)
{
    uint8_t reg = BMM150_REG_DATA_X_L;
    uint8_t raw[8];

    if (data == NULL) {
        return;
    }

    /*
     * 维持原有 forced mode 读取流程：
     * 每次读取前都重新上电并触发一次测量。
     */
    BMM150_WriteRegister(0x4BU, 0x01U);
    Platform_Time_DelayMs(10U);

    BMM150_WriteRegister(0x4CU, 0x02U);
    Platform_Time_DelayMs(15U);

    Platform_I2c_Write(APP_BMM150_ADDR, &reg, 1U, HAL_MAX_DELAY);
    Platform_I2c_Read(APP_BMM150_ADDR, raw, sizeof(raw), HAL_MAX_DELAY);

    data->magX = (int16_t)((raw[1] << 8U) | raw[0]);
    data->magY = (int16_t)((raw[3] << 8U) | raw[2]);
    data->magZ = (int16_t)((raw[5] << 8U) | raw[4]);
}

static void BMM150_WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};

    Platform_I2c_Write(APP_BMM150_ADDR, payload, 2U, HAL_MAX_DELAY);
}

static uint8_t BMM150_ReadRegister(uint8_t reg)
{
    uint8_t value = 0U;

    Platform_I2c_Write(APP_BMM150_ADDR, &reg, 1U, HAL_MAX_DELAY);
    Platform_I2c_Read(APP_BMM150_ADDR, &value, 1U, HAL_MAX_DELAY);

    return value;
}
