#ifndef USER_APP_APP_H
#define USER_APP_APP_H

#include <stdint.h>

#include "servo.h"

/*
 * 应用层入口。
 * main.c 只负责初始化 HAL/CubeMX，并把业务初始化与主循环调度交给这里。
 */

void App_Init(void);
void App_RunOnce(void);

typedef struct {
    uint8_t servoIndex;
    uint8_t commandAngle;
    uint16_t commandPulseUs;
    uint8_t feedbackValid;
    uint16_t feedbackRaw;
    uint16_t feedbackMv;
    uint8_t feedbackAngle;
} App_ServoFeedbackSnapshotTypeDef;

typedef struct {
    uint16_t raw;
    uint16_t millivolts;
    uint16_t thresholdRaw;
    uint8_t aboveThreshold;
    uint8_t active;
} App_BottomIrTestSnapshotTypeDef;

uint8_t App_SetServoAngle(uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs);
uint8_t App_SetServoDegX10(uint8_t servoIndex, int16_t degX10, uint16_t *pulseUs);
uint8_t App_MoveServoToAngleOverTime(uint8_t servoIndex,
                                     uint8_t angle,
                                     uint16_t durationMs,
                                     Servo_MotionProfileTypeDef profile,
                                     uint16_t *targetPulseUs);
uint8_t App_MoveServoToAngleOverTimeWithSteps(uint8_t servoIndex,
                                              uint8_t angle,
                                              uint16_t durationMs,
                                              uint16_t requestedSteps,
                                              Servo_MotionProfileTypeDef profile,
                                              uint16_t *targetPulseUs,
                                              uint16_t *effectiveUpdatePeriodMs);
uint8_t App_MoveServoToAngleOverTimeWithStepsAndEaseStrength(uint8_t servoIndex,
                                                             uint8_t angle,
                                                             uint16_t durationMs,
                                                             uint16_t requestedSteps,
                                                             Servo_MotionProfileTypeDef profile,
                                                             uint8_t easeStrengthPercent,
                                                             uint16_t *targetPulseUs,
                                                             uint16_t *effectiveUpdatePeriodMs);
uint8_t App_StopServoMotion(uint8_t servoIndex);
uint8_t App_SetServoLimit(uint8_t servoIndex, uint8_t minAngle, uint8_t maxAngle);
uint8_t App_GetServoLimit(uint8_t servoIndex, uint8_t *minAngle, uint8_t *maxAngle);
uint8_t App_GetServoCommandAngle(uint8_t servoIndex, uint8_t *angle);
uint8_t App_GetServoFeedbackSnapshot(uint8_t servoIndex, App_ServoFeedbackSnapshotTypeDef *snapshot);
uint8_t App_GetServoFeedbackRaw(uint8_t servoIndex, uint16_t *adcRaw);
uint16_t App_ConvertServoFeedbackToMv(uint16_t adcRaw);
uint8_t App_GetServoFeedbackAngle(uint8_t servoIndex, uint8_t *angle);
uint8_t App_ReadBottomIrTest(App_BottomIrTestSnapshotTypeDef *snapshot);
uint8_t App_ReleaseServo(uint8_t servoIndex);
uint8_t App_StartServoReciprocating(uint8_t servoIndex);
uint8_t App_GetServoState(uint8_t servoIndex, Servo_StateTypeDef *state);
uint8_t App_SetWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue);
uint8_t App_SetWs2812LedCount(uint16_t ledCount);
uint16_t App_GetWs2812LedCount(void);
uint16_t App_GetWs2812MaxLedCount(void);
uint8_t App_StartWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue);
uint8_t App_StartWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs);
uint8_t App_StartWs2812Rainbow(void);
uint8_t App_StartWs2812RainbowTimed(uint8_t step, uint8_t intervalMs);
uint8_t App_SetBottomWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue);
uint8_t App_SetBottomWs2812LedCount(uint16_t ledCount);
uint16_t App_GetBottomWs2812LedCount(void);
uint16_t App_GetBottomWs2812MaxLedCount(void);
uint8_t App_StartBottomWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue);
uint8_t App_StartBottomWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs);
uint8_t App_StartBottomWs2812Rainbow(void);
uint8_t App_StartBottomWs2812RainbowTimed(uint8_t step, uint8_t intervalMs);
uint8_t App_RunBottomLightTest(void);
void App_StopWs2812Effect(void);
void App_StopBottomWs2812Effect(void);
uint8_t App_SetPower5V(uint8_t enabled, uint8_t sourceTag);

#endif /* USER_APP_APP_H */
