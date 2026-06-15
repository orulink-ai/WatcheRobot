#ifndef USER_DEVICE_QMC6309_H
#define USER_DEVICE_QMC6309_H

#include <stdint.h>

typedef struct {
    int16_t magX;
    int16_t magY;
    int16_t magZ;
} QMC6309_DataTypeDef;

typedef struct {
    uint8_t chipId;
    uint8_t status;
    uint8_t ctrl1;
    uint8_t ctrl2;
} QMC6309_DiagnosticsTypeDef;

uint8_t QMC6309_Init(void);
uint8_t QMC6309_ReadData(QMC6309_DataTypeDef *data);
uint8_t QMC6309_ReadStatus(uint8_t *status);
uint8_t QMC6309_ReadDiagnostics(QMC6309_DiagnosticsTypeDef *diagnostics);

#endif /* USER_DEVICE_QMC6309_H */
