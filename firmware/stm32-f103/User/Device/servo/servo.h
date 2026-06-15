#ifndef USER_DEVICE_SERVO_H
#define USER_DEVICE_SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/*
 * 通用多通道舵机驱动接口。
 * 当前版本保持原有“上层定时调用 Servo_Update()”的节拍模型不变。
 */

#define SERVO_DEFAULT_PERIOD_US 20000U
#define SERVO_DEFAULT_PULSE_MIN 1000U
#define SERVO_DEFAULT_PULSE_MAX 2000U
#define SERVO_DEFAULT_PULSE_CENTER 1500U
#define SERVO_PWM_FRAME_PERIOD_MS 20U
#define SERVO_MOTION_UPDATE_PERIOD_MS 20U
#define SERVO_MOTION_UPDATE_PERIOD_MIN_MS SERVO_PWM_FRAME_PERIOD_MS
#define SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT 100U
#define SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT 100U
#define SERVO_RELEASE_PULSE_US 0U
#define SERVO_FEEDBACK_ADC_MAX 4095U
#define SERVO_FEEDBACK_VREF_MV 3300U
#define SERVO_FEEDBACK_AVG_SAMPLES 8U
#define SERVO_FEEDBACK_ADC_TIMEOUT_MS 10U

#define SERVO_MAX_INSTANCES 4U

typedef void *Servo_HandleTypeDef;

typedef enum {
    SERVO_OK = 0x00U,
    SERVO_ERROR = 0x01U,
    SERVO_ERROR_INIT = 0x02U,
    SERVO_ERROR_PARAM = 0x03U,
    SERVO_ERROR_BUSY = 0x04U,
    SERVO_ERROR_NO_RESOURCE = 0x05U
} Servo_StatusTypeDef;

typedef enum {
    SERVO_DIRECTION_NONE = 0x00U,
    SERVO_DIRECTION_FORWARD = 0x01U,
    SERVO_DIRECTION_BACKWARD = 0x02U
} Servo_DirectionTypeDef;

typedef enum {
    SERVO_MOTION_PROFILE_LINEAR = 0x00U,
    SERVO_MOTION_PROFILE_EASE_IN_OUT = 0x01U,
    SERVO_MOTION_PROFILE_ANTI_DROP = 0x02U
} Servo_MotionProfileTypeDef;

typedef struct {
    TIM_HandleTypeDef *pTimHandle;
    uint32_t TimChannel;
    ADC_HandleTypeDef *pFeedbackAdcHandle;
    uint8_t feedbackSampleIndex;
    uint16_t feedbackRawMin;
    uint16_t feedbackRawMax;
    uint16_t pulseMin;
    uint16_t pulseMax;
    uint16_t pulseCenter;
    uint16_t commandPulseCount;
    uint16_t stepSize;
    uint16_t stepDelayMs;
} Servo_ConfigTypeDef;

typedef struct {
    uint16_t currentPulse;
    /*
     * 预留给后续命令驱动模式。
     * 本轮重构不改变行为，因此当前仍以 currentPulse 和往复方向为主。
     */
    uint16_t targetPulse;
    uint16_t motionStartPulse;
    uint16_t motionTargetPulse;
    uint16_t motionDurationMs;
    uint16_t motionUpdatePeriodMs;
    uint8_t motionEaseStrengthPercent;
    Servo_DirectionTypeDef direction;
    Servo_MotionProfileTypeDef motionProfile;
    uint8_t isReciprocating;
    uint8_t isMotionActive;
    uint8_t isInitialized;
    uint8_t isSignalActive;
    uint32_t lastStepTickMs;
    uint32_t motionStartedAtMs;
    uint32_t lastMotionUpdateTickMs;
    uint32_t releaseDeadlineMs;
} Servo_StateTypeDef;

Servo_HandleTypeDef Servo_Init(const Servo_ConfigTypeDef *config);
Servo_StatusTypeDef Servo_DeInit(Servo_HandleTypeDef servo);
Servo_StatusTypeDef Servo_SetPulse(Servo_HandleTypeDef servo, uint16_t pulseUs);
Servo_StatusTypeDef Servo_GetPulse(Servo_HandleTypeDef servo, uint16_t *pulseUs);
Servo_StatusTypeDef Servo_SetAngle(Servo_HandleTypeDef servo, uint8_t angle);
Servo_StatusTypeDef Servo_MoveToPulseOverTime(Servo_HandleTypeDef servo,
                                              uint16_t pulseUs,
                                              uint16_t durationMs,
                                              Servo_MotionProfileTypeDef profile);
Servo_StatusTypeDef Servo_MoveToPulseOverTimeWithUpdatePeriod(Servo_HandleTypeDef servo,
                                                              uint16_t pulseUs,
                                                              uint16_t durationMs,
                                                              Servo_MotionProfileTypeDef profile,
                                                              uint16_t updatePeriodMs);
Servo_StatusTypeDef Servo_MoveToPulseOverTimeWithUpdatePeriodAndEaseStrength(Servo_HandleTypeDef servo,
                                                                             uint16_t pulseUs,
                                                                             uint16_t durationMs,
                                                                             Servo_MotionProfileTypeDef profile,
                                                                             uint16_t updatePeriodMs,
                                                                             uint8_t easeStrengthPercent);
Servo_StatusTypeDef Servo_MoveToAngleOverTime(Servo_HandleTypeDef servo,
                                              uint8_t angle,
                                              uint16_t durationMs,
                                              Servo_MotionProfileTypeDef profile);
Servo_StatusTypeDef Servo_MoveToAngleOverTimeWithUpdatePeriod(Servo_HandleTypeDef servo,
                                                              uint8_t angle,
                                                              uint16_t durationMs,
                                                              Servo_MotionProfileTypeDef profile,
                                                              uint16_t updatePeriodMs);
Servo_StatusTypeDef Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(Servo_HandleTypeDef servo,
                                                                             uint8_t angle,
                                                                             uint16_t durationMs,
                                                                             Servo_MotionProfileTypeDef profile,
                                                                             uint16_t updatePeriodMs,
                                                                             uint8_t easeStrengthPercent);
uint16_t Servo_MotionUpdatePeriodFromSteps(uint16_t durationMs, uint16_t requestedSteps);
Servo_StatusTypeDef Servo_StopMotion(Servo_HandleTypeDef servo);
Servo_StatusTypeDef Servo_GetFeedbackRaw(Servo_HandleTypeDef servo, uint16_t *adcRaw);
uint16_t Servo_ConvertFeedbackToMv(uint16_t adcRaw);
Servo_StatusTypeDef Servo_GetFeedbackAngle(Servo_HandleTypeDef servo, uint8_t *angle);
Servo_StatusTypeDef Servo_StartReciprocating(Servo_HandleTypeDef servo);
Servo_StatusTypeDef Servo_StopReciprocating(Servo_HandleTypeDef servo);
Servo_StatusTypeDef Servo_ReleaseOutput(Servo_HandleTypeDef servo);
uint8_t Servo_IsReciprocating(Servo_HandleTypeDef servo);
uint8_t Servo_IsMotionActive(Servo_HandleTypeDef servo);
void Servo_Update(void);
Servo_StatusTypeDef Servo_GetState(Servo_HandleTypeDef servo, Servo_StateTypeDef *state);

#ifdef __cplusplus
}
#endif

#endif /* USER_DEVICE_SERVO_H */
