#ifndef USER_DEVICE_BMI160_H
#define USER_DEVICE_BMI160_H

#include <stdint.h>

/*
 * BMI160 设备接口。
 * 当前只保留项目实际用到的初始化和数据读取能力。
 */

typedef struct {
    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;
    int16_t accX;
    int16_t accY;
    int16_t accZ;
} BMI160_DataTypeDef;

void BMI160_Init(void);
void BMI160_ReadData(BMI160_DataTypeDef *data);

#endif /* USER_DEVICE_BMI160_H */
