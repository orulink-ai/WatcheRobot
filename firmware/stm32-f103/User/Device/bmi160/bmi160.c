#include "bmi160.h"

#include "app_config.h"
#include "platform_i2c.h"
#include "platform_time.h"
#include "platform_uart.h"

#define BMI160_REG_CHIP_ID 0x00U
#define BMI160_REG_GYR_X_L 0x0CU

static void BMI160_WriteRegister(uint8_t reg, uint8_t value);
static uint8_t BMI160_ReadRegister(uint8_t reg);

void BMI160_Init(void)
{
    uint8_t chipId = BMI160_ReadRegister(BMI160_REG_CHIP_ID);

    if (chipId != 0xD1U) {
        Platform_Uart_Print("[BMI160] Chip ID mismatch!\r\n");
        return;
    }

    Platform_Uart_Print("[BMI160] Chip ID OK: 0xD1\r\n");

    /*
     * 初始化顺序保持与原逻辑一致：
     * 先唤醒加速度计，再唤醒陀螺仪，最后写入带宽/量程配置。
     */
    BMI160_WriteRegister(0x7EU, 0x11U);
    Platform_Time_DelayMs(10U);

    BMI160_WriteRegister(0x7EU, 0x15U);
    Platform_Time_DelayMs(10U);

    BMI160_WriteRegister(0x40U, 0x08U);
    Platform_Time_DelayMs(5U);

    BMI160_WriteRegister(0x41U, 0x05U);
    Platform_Time_DelayMs(5U);

    Platform_Uart_Print("[BMI160] Initialized\r\n");
}

void BMI160_ReadData(BMI160_DataTypeDef *data)
{
    uint8_t reg = BMI160_REG_GYR_X_L;
    uint8_t raw[14];

    if (data == NULL) {
        return;
    }

    Platform_I2c_Write(APP_BMI160_ADDR, &reg, 1U, HAL_MAX_DELAY);
    Platform_I2c_Read(APP_BMI160_ADDR, raw, sizeof(raw), HAL_MAX_DELAY);

    data->gyroX = (int16_t)((raw[1] << 8U) | raw[0]);
    data->gyroY = (int16_t)((raw[3] << 8U) | raw[2]);
    data->gyroZ = (int16_t)((raw[5] << 8U) | raw[4]);
    data->accX = (int16_t)((raw[7] << 8U) | raw[6]);
    data->accY = (int16_t)((raw[9] << 8U) | raw[8]);
    data->accZ = (int16_t)((raw[11] << 8U) | raw[10]);
}

static void BMI160_WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};

    Platform_I2c_Write(APP_BMI160_ADDR, payload, 2U, HAL_MAX_DELAY);
}

static uint8_t BMI160_ReadRegister(uint8_t reg)
{
    uint8_t value = 0U;

    Platform_I2c_Write(APP_BMI160_ADDR, &reg, 1U, HAL_MAX_DELAY);
    Platform_I2c_Read(APP_BMI160_ADDR, &value, 1U, HAL_MAX_DELAY);

    return value;
}
