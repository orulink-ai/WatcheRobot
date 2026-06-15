#ifndef USER_DEVICE_BMM150_H
#define USER_DEVICE_BMM150_H

#include <stdint.h>

/*
 * BMM150 设备接口。
 * 当前只保留初始化和原始磁力计数据读取能力。
 */

typedef struct {
    int16_t magX;
    int16_t magY;
    int16_t magZ;
} BMM150_DataTypeDef;

void BMM150_Init(void);
void BMM150_ReadData(BMM150_DataTypeDef *data);

#endif /* USER_DEVICE_BMM150_H */
