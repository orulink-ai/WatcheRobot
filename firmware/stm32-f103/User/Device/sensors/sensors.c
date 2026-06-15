#include "sensors.h"

#include "app_config.h"
#include "platform_i2c.h"
#include "platform_uart.h"

static void Sensors_PrintI2cBusLevels(void);
static void Sensors_PrintI2cPinTestLine(const char *label, uint8_t sclHigh, uint8_t sdaHigh);

void Sensors_Scan(void)
{
    Platform_I2c_RecoverBus();

    Platform_Uart_Print("========== I2C Sensor Scan ==========\r\n");

#if (APP_BMI160_ENABLE != 0U)
    if (Platform_I2c_IsDeviceReady(APP_BMI160_SCAN_ADDR_0, APP_I2C_READY_RETRY_COUNT, APP_I2C_READY_TIMEOUT_MS) == HAL_OK) {
        Platform_Uart_Print("[FOUND] BMI160 @ 0x68\r\n");
    } else if (Platform_I2c_IsDeviceReady(APP_BMI160_SCAN_ADDR_1, APP_I2C_READY_RETRY_COUNT, APP_I2C_READY_TIMEOUT_MS) == HAL_OK) {
        Platform_Uart_Print("[FOUND] BMI160 @ 0x69\r\n");
    } else {
        Platform_Uart_Print("[NOT FOUND] BMI160\r\n");
    }
#else
    Platform_Uart_Print("[SKIP] BMI160 disabled\r\n");
#endif

    if (Platform_I2c_IsDeviceReady(APP_QMC6309_SCAN_ADDR, APP_I2C_READY_RETRY_COUNT, APP_I2C_READY_TIMEOUT_MS) == HAL_OK) {
        Platform_Uart_Print("[FOUND] QMC6309 @ 0x7C\r\n");
    } else {
        Platform_Uart_Print("[NOT FOUND] QMC6309\r\n");
    }

    Platform_Uart_Print("====================================\r\n");
}

void Sensors_ScanAllI2cAddresses(void)
{
    uint16_t address;
    uint8_t foundCount = 0U;

    Platform_Uart_Print("========== I2C Full Scan ==========\r\n");

    for (address = 0x03U; address <= 0x7FU; address++) {
        if (Platform_I2c_IsDeviceReady(address, 1U, APP_I2C_SCAN_TIMEOUT_MS) == HAL_OK) {
            Platform_Uart_Printf("[FOUND] 0x%02X\r\n", address);
            foundCount++;
        }
    }

    if (foundCount == 0U) {
        Platform_Uart_Print("[NOT FOUND] No I2C ACK\r\n");
    }

    Platform_Uart_Print("===================================\r\n");
}

void Sensors_PrintI2cDiagnostics(void)
{
    HAL_StatusTypeDef readyStatus;
    uint32_t gpiobCrl = 0U;
    uint32_t gpiobIdr = 0U;
    uint32_t gpiobOdr = 0U;
    uint32_t afioMapr = 0U;

    Platform_Uart_Print("========== I2C Diagnostics ==========\r\n");
    Sensors_PrintI2cBusLevels();
    Platform_Uart_Printf("I2C1 Remap: %s\r\n", (Platform_I2c_IsRemapped() != 0U) ? "PB8/PB9" : "PB6/PB7");
    Platform_I2c_ReadRawRegisters(&gpiobCrl, &gpiobIdr, &gpiobOdr, &afioMapr);
    Platform_Uart_Printf("GPIOB_CRL : 0x%08lX\r\n", (unsigned long)gpiobCrl);
    Platform_Uart_Printf("GPIOB_IDR : 0x%08lX\r\n", (unsigned long)gpiobIdr);
    Platform_Uart_Printf("GPIOB_ODR : 0x%08lX\r\n", (unsigned long)gpiobOdr);
    Platform_Uart_Printf("AFIO_MAPR : 0x%08lX\r\n", (unsigned long)afioMapr);
    Platform_Uart_Printf("HAL State : 0x%02X\r\n", (unsigned int)Platform_I2c_GetState());
    Platform_Uart_Printf("HAL Error : 0x%08lX\r\n", (unsigned long)Platform_I2c_GetError());

    readyStatus = Platform_I2c_IsDeviceReady(APP_QMC6309_SCAN_ADDR, 1U, APP_I2C_READY_TIMEOUT_MS);
    Platform_Uart_Printf("Probe 0x7C: %s\r\n", (readyStatus == HAL_OK) ? "ACK" : "NO ACK");
    Platform_Uart_Printf("HAL Error : 0x%08lX\r\n", (unsigned long)Platform_I2c_GetError());
    readyStatus = Platform_I2c_IsDeviceReady(0x3EU, 1U, APP_I2C_READY_TIMEOUT_MS);
    Platform_Uart_Printf("Probe 0x3E: %s (address-format cross-check only)\r\n", (readyStatus == HAL_OK) ? "ACK" : "NO ACK");
    Platform_Uart_Printf("HAL Error : 0x%08lX\r\n", (unsigned long)Platform_I2c_GetError());
    Sensors_PrintI2cBusLevels();
    Platform_Uart_Print("Spec note : QMC6309 uses 7-bit address 0x7C (write byte 0xF8), VDD 2.5~3.6V, external pull-ups required\r\n");
    Platform_Uart_Print("=====================================\r\n");
}

void Sensors_RecoverI2cBus(void)
{
    Platform_Uart_Print("========== I2C Recovery ==========\r\n");
    Platform_Uart_Print("Before:\r\n");
    Sensors_PrintI2cBusLevels();
    Platform_I2c_RecoverBus();
    Platform_Uart_Print("After:\r\n");
    Sensors_PrintI2cBusLevels();
    Platform_Uart_Print("==================================\r\n");
}

void Sensors_TestI2cPins(void)
{
    Platform_Uart_Print("========== I2C Pin Test ==========\r\n");
    Platform_I2c_PinTest(Sensors_PrintI2cPinTestLine);
    Platform_Uart_Print("Expected : INPUT_PULLUP/OD_RELEASE/SCL_RELEASE/SDA_RELEASE should be HIGH/HIGH\r\n");
    Platform_Uart_Print("If SCL remains LOW while released, PB6/SCL is externally pulled low or shorted\r\n");
    Platform_Uart_Print("==================================\r\n");
}

static void Sensors_PrintI2cBusLevels(void)
{
    uint8_t sclHigh = 0U;
    uint8_t sdaHigh = 0U;

    Platform_I2c_ReadBusLevels(&sclHigh, &sdaHigh);
    Platform_Uart_Printf("PB6/SCL   : %s\r\n", (sclHigh != 0U) ? "HIGH" : "LOW");
    Platform_Uart_Printf("PB7/SDA   : %s\r\n", (sdaHigh != 0U) ? "HIGH" : "LOW");
}

static void Sensors_PrintI2cPinTestLine(const char *label, uint8_t sclHigh, uint8_t sdaHigh)
{
    Platform_Uart_Printf("%s: SCL=%s SDA=%s\r\n",
                         label,
                         (sclHigh != 0U) ? "HIGH" : "LOW",
                         (sdaHigh != 0U) ? "HIGH" : "LOW");
}
