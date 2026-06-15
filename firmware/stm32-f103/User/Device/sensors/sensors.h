#ifndef USER_DEVICE_SENSORS_H
#define USER_DEVICE_SENSORS_H

/*
 * 传感器辅助接口。
 * 当前仅负责启动阶段的 I2C 设备扫描打印。
 */

void Sensors_Scan(void);
void Sensors_ScanAllI2cAddresses(void);
void Sensors_PrintI2cDiagnostics(void);
void Sensors_RecoverI2cBus(void);
void Sensors_TestI2cPins(void);

#endif /* USER_DEVICE_SENSORS_H */
