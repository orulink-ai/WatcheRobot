#include "servo.h"

#include "platform_pwm.h"
#include "platform_time.h"
#include "stm32f1xx_hal_adc_ex.h"

#include <stddef.h>

/*
 * Keep a fixed servo instance pool to avoid dynamic allocation in the MCU
 * runtime while still supporting multiple timer channels.
 */
typedef struct {
    Servo_ConfigTypeDef config;
    Servo_StateTypeDef state;
    uint8_t inUse;
} Servo_InstanceTypeDef;

static Servo_InstanceTypeDef s_servoPool[SERVO_MAX_INSTANCES];

#define SERVO_ANTI_DROP_PRELOAD_US 18U
#define SERVO_ANTI_DROP_PRELOAD_PERCENT 8U
#define SERVO_ANTI_DROP_HOLD_PERCENT 6U
#define SERVO_ANTI_DROP_MIN_STEP_US 4U

static Servo_InstanceTypeDef *Servo_GetInstance(Servo_HandleTypeDef servo);
static Servo_StatusTypeDef Servo_StartPwmOutput(Servo_ConfigTypeDef *config);
static Servo_StatusTypeDef Servo_WritePulseInternal(Servo_ConfigTypeDef *config, uint16_t pulse);
static Servo_StatusTypeDef Servo_InitFeedback(Servo_ConfigTypeDef *config);
static Servo_StatusTypeDef Servo_ReadFeedbackChannel(ADC_HandleTypeDef *adcHandle,
                                                     uint32_t adcChannel,
                                                     uint16_t *sample);
static uint16_t Servo_ClampPulse(const Servo_ConfigTypeDef *config, uint16_t pulse);
static uint16_t Servo_AngleToPulse(const Servo_ConfigTypeDef *config, uint8_t angle);
static uint16_t Servo_NormalizeMotionUpdatePeriod(uint16_t updatePeriodMs);
static uint16_t Servo_InterpolateSmoothStepPulse(uint16_t startPulse,
                                                 uint16_t targetPulse,
                                                 uint32_t elapsedMs,
                                                 uint16_t durationMs,
                                                 uint8_t easeStrengthPercent);
static uint16_t Servo_InterpolatePulse(uint16_t startPulse,
                                       uint16_t targetPulse,
                                       uint32_t elapsedMs,
                                       uint16_t durationMs,
                                       Servo_MotionProfileTypeDef profile,
                                       uint8_t easeStrengthPercent);
static uint8_t Servo_ShouldSkipSmallAntiDropStep(const Servo_StateTypeDef *state, uint16_t nextPulse, uint8_t complete);
static Servo_StatusTypeDef Servo_UpdateMotionAt(Servo_InstanceTypeDef *instance, uint32_t nowTickMs, uint8_t force);
static void Servo_ArmReleaseDeadline(Servo_InstanceTypeDef *instance, uint32_t nowTickMs);
static void Servo_ReleaseSignalInternal(Servo_InstanceTypeDef *instance);
static uint8_t Servo_HasDeadlineElapsed(uint32_t nowTickMs, uint32_t deadlineTickMs);

Servo_HandleTypeDef Servo_Init(const Servo_ConfigTypeDef *config)
{
    Servo_InstanceTypeDef *instance = NULL;
    uint8_t index;
    uint32_t nowTickMs;

    if ((config == NULL) || (config->pTimHandle == NULL)) {
        return NULL;
    }
    if (config->pulseMin >= config->pulseMax) {
        return NULL;
    }
    if ((config->stepSize == 0U) || (config->stepDelayMs == 0U)) {
        return NULL;
    }
    if ((config->pFeedbackAdcHandle != NULL) &&
        ((config->feedbackSampleIndex < 1U) || (config->feedbackSampleIndex > 2U) ||
         (config->feedbackRawMin >= config->feedbackRawMax) ||
         (config->feedbackAngleMin > config->feedbackAngleMax) ||
         ((config->feedbackRawMid != 0U) &&
          ((config->feedbackRawMid <= config->feedbackRawMin) || (config->feedbackRawMid >= config->feedbackRawMax) ||
           (config->feedbackAngleMid <= config->feedbackAngleMin) ||
           (config->feedbackAngleMid >= config->feedbackAngleMax))) ||
         (config->feedbackAngleMax > 180U))) {
        return NULL;
    }

    for (index = 0U; index < SERVO_MAX_INSTANCES; index++) {
        if (s_servoPool[index].inUse == 0U) {
            instance = &s_servoPool[index];
            break;
        }
    }

    if (instance == NULL) {
        return NULL;
    }

    if (Servo_StartPwmOutput((Servo_ConfigTypeDef *)config) != SERVO_OK) {
        return NULL;
    }
    if (Servo_InitFeedback((Servo_ConfigTypeDef *)config) != SERVO_OK) {
        return NULL;
    }

    nowTickMs = Platform_Time_GetTickMs();

    instance->config = *config;
    instance->state.currentPulse = config->pulseCenter;
    instance->state.targetPulse = config->pulseCenter;
    instance->state.motionStartPulse = config->pulseCenter;
    instance->state.motionTargetPulse = config->pulseCenter;
    instance->state.motionDurationMs = 0U;
    instance->state.motionUpdatePeriodMs = SERVO_MOTION_UPDATE_PERIOD_MS;
    instance->state.motionEaseStrengthPercent = SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT;
    instance->state.direction = SERVO_DIRECTION_NONE;
    instance->state.motionProfile = SERVO_MOTION_PROFILE_LINEAR;
    instance->state.isReciprocating = 0U;
    instance->state.isMotionActive = 0U;
    instance->state.isInitialized = 1U;
    instance->state.isSignalActive = 0U;
    instance->state.lastStepTickMs = nowTickMs;
    instance->state.motionStartedAtMs = nowTickMs;
    instance->state.lastMotionUpdateTickMs = nowTickMs;
    instance->state.releaseDeadlineMs = nowTickMs;
    instance->inUse = 1U;

    if (Servo_WritePulseInternal(&instance->config, instance->state.currentPulse) != SERVO_OK) {
        instance->inUse = 0U;
        instance->state.isInitialized = 0U;
        return NULL;
    }

    instance->state.isSignalActive = 1U;
    Servo_ArmReleaseDeadline(instance, nowTickMs);

    return (Servo_HandleTypeDef)instance;
}

Servo_StatusTypeDef Servo_DeInit(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    Servo_ReleaseSignalInternal(instance);
    instance->inUse = 0U;
    instance->state.isInitialized = 0U;

    return SERVO_OK;
}

Servo_StatusTypeDef Servo_SetPulse(Servo_HandleTypeDef servo, uint16_t pulseUs)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    pulseUs = Servo_ClampPulse(&instance->config, pulseUs);

    instance->state.isMotionActive = 0U;
    instance->state.isReciprocating = 0U;
    instance->state.direction = SERVO_DIRECTION_NONE;
    instance->state.currentPulse = pulseUs;
    instance->state.targetPulse = pulseUs;
    instance->state.motionStartPulse = pulseUs;
    instance->state.motionTargetPulse = pulseUs;
    instance->state.motionDurationMs = 0U;
    instance->state.motionUpdatePeriodMs = SERVO_MOTION_UPDATE_PERIOD_MS;
    instance->state.motionEaseStrengthPercent = SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT;

    if (Servo_WritePulseInternal(&instance->config, pulseUs) != SERVO_OK) {
        return SERVO_ERROR_PARAM;
    }

    instance->state.isSignalActive = 1U;
    Servo_ArmReleaseDeadline(instance, Platform_Time_GetTickMs());

    return SERVO_OK;
}

Servo_StatusTypeDef Servo_GetPulse(Servo_HandleTypeDef servo, uint16_t *pulseUs)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if ((instance == NULL) || (pulseUs == NULL)) {
        return SERVO_ERROR_PARAM;
    }

    *pulseUs = instance->state.currentPulse;
    return SERVO_OK;
}

Servo_StatusTypeDef Servo_SetAngle(Servo_HandleTypeDef servo, uint8_t angle)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);
    uint16_t pulseUs;

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    pulseUs = Servo_AngleToPulse(&instance->config, angle);

    return Servo_SetPulse(servo, pulseUs);
}

Servo_StatusTypeDef Servo_MoveToPulseOverTime(Servo_HandleTypeDef servo,
                                              uint16_t pulseUs,
                                              uint16_t durationMs,
                                              Servo_MotionProfileTypeDef profile)
{
    return Servo_MoveToPulseOverTimeWithUpdatePeriod(servo,
                                                     pulseUs,
                                                     durationMs,
                                                     profile,
                                                     SERVO_MOTION_UPDATE_PERIOD_MS);
}

Servo_StatusTypeDef Servo_MoveToPulseOverTimeWithUpdatePeriod(Servo_HandleTypeDef servo,
                                                              uint16_t pulseUs,
                                                              uint16_t durationMs,
                                                              Servo_MotionProfileTypeDef profile,
                                                              uint16_t updatePeriodMs)
{
    return Servo_MoveToPulseOverTimeWithUpdatePeriodAndEaseStrength(servo,
                                                                    pulseUs,
                                                                    durationMs,
                                                                    profile,
                                                                    updatePeriodMs,
                                                                    SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT);
}

Servo_StatusTypeDef Servo_MoveToPulseOverTimeWithUpdatePeriodAndEaseStrength(Servo_HandleTypeDef servo,
                                                                             uint16_t pulseUs,
                                                                             uint16_t durationMs,
                                                                             Servo_MotionProfileTypeDef profile,
                                                                             uint16_t updatePeriodMs,
                                                                             uint8_t easeStrengthPercent)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);
    uint32_t nowTickMs;

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    if ((profile != SERVO_MOTION_PROFILE_LINEAR) &&
        (profile != SERVO_MOTION_PROFILE_EASE_IN_OUT) &&
        (profile != SERVO_MOTION_PROFILE_ANTI_DROP)) {
        return SERVO_ERROR_PARAM;
    }
    if (easeStrengthPercent > SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT) {
        return SERVO_ERROR_PARAM;
    }

    pulseUs = Servo_ClampPulse(&instance->config, pulseUs);
    if ((durationMs == 0U) || (pulseUs == instance->state.currentPulse)) {
        return Servo_SetPulse(servo, pulseUs);
    }

    nowTickMs = Platform_Time_GetTickMs();
    instance->state.isReciprocating = 0U;
    instance->state.direction = SERVO_DIRECTION_NONE;
    instance->state.isMotionActive = 1U;
    instance->state.isSignalActive = 1U;
    instance->state.motionStartPulse = instance->state.currentPulse;
    instance->state.motionTargetPulse = pulseUs;
    instance->state.targetPulse = pulseUs;
    instance->state.motionDurationMs = durationMs;
    instance->state.motionUpdatePeriodMs = Servo_NormalizeMotionUpdatePeriod(updatePeriodMs);
    instance->state.motionEaseStrengthPercent = easeStrengthPercent;
    instance->state.motionProfile = profile;
    instance->state.motionStartedAtMs = nowTickMs;
    instance->state.lastMotionUpdateTickMs = nowTickMs;

    return Servo_WritePulseInternal(&instance->config, instance->state.currentPulse);
}

Servo_StatusTypeDef Servo_MoveToAngleOverTime(Servo_HandleTypeDef servo,
                                              uint8_t angle,
                                              uint16_t durationMs,
                                              Servo_MotionProfileTypeDef profile)
{
    return Servo_MoveToAngleOverTimeWithUpdatePeriod(servo,
                                                     angle,
                                                     durationMs,
                                                     profile,
                                                     SERVO_MOTION_UPDATE_PERIOD_MS);
}

Servo_StatusTypeDef Servo_MoveToAngleOverTimeWithUpdatePeriod(Servo_HandleTypeDef servo,
                                                              uint8_t angle,
                                                              uint16_t durationMs,
                                                              Servo_MotionProfileTypeDef profile,
                                                              uint16_t updatePeriodMs)
{
    return Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(servo,
                                                                    angle,
                                                                    durationMs,
                                                                    profile,
                                                                    updatePeriodMs,
                                                                    SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT);
}

Servo_StatusTypeDef Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(Servo_HandleTypeDef servo,
                                                                             uint8_t angle,
                                                                             uint16_t durationMs,
                                                                             Servo_MotionProfileTypeDef profile,
                                                                             uint16_t updatePeriodMs,
                                                                             uint8_t easeStrengthPercent)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    return Servo_MoveToPulseOverTimeWithUpdatePeriodAndEaseStrength(servo,
                                                                    Servo_AngleToPulse(&instance->config, angle),
                                                                    durationMs,
                                                                    profile,
                                                                    updatePeriodMs,
                                                                    easeStrengthPercent);
}

uint16_t Servo_MotionUpdatePeriodFromSteps(uint16_t durationMs, uint16_t requestedSteps)
{
    uint32_t roundedPeriodMs;

    if ((durationMs == 0U) || (requestedSteps == 0U)) {
        return SERVO_MOTION_UPDATE_PERIOD_MS;
    }

    roundedPeriodMs = ((uint32_t)durationMs + ((uint32_t)requestedSteps / 2U)) / (uint32_t)requestedSteps;
    if (roundedPeriodMs == 0U) {
        roundedPeriodMs = 1U;
    }

    return Servo_NormalizeMotionUpdatePeriod((uint16_t)roundedPeriodMs);
}

Servo_StatusTypeDef Servo_StopMotion(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);
    uint32_t nowTickMs;

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    nowTickMs = Platform_Time_GetTickMs();
    if (instance->state.isMotionActive != 0U) {
        if (Servo_UpdateMotionAt(instance, nowTickMs, 1U) != SERVO_OK) {
            return SERVO_ERROR_PARAM;
        }
    }

    instance->state.isMotionActive = 0U;
    instance->state.isReciprocating = 0U;
    instance->state.direction = SERVO_DIRECTION_NONE;
    instance->state.targetPulse = instance->state.currentPulse;
    instance->state.motionTargetPulse = instance->state.currentPulse;
    instance->state.motionDurationMs = 0U;
    instance->state.motionUpdatePeriodMs = SERVO_MOTION_UPDATE_PERIOD_MS;
    instance->state.isSignalActive = 1U;
    Servo_ArmReleaseDeadline(instance, nowTickMs);

    return SERVO_OK;
}

Servo_StatusTypeDef Servo_GetFeedbackRaw(Servo_HandleTypeDef servo, uint16_t *adcRaw)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);
    uint32_t sum = 0U;
    uint16_t sample;
    uint8_t sampleIndex;

    if ((instance == NULL) || (adcRaw == NULL) || (instance->config.pFeedbackAdcHandle == NULL)) {
        return SERVO_ERROR_PARAM;
    }

    for (sampleIndex = 0U; sampleIndex < SERVO_FEEDBACK_AVG_SAMPLES; sampleIndex++) {
        if (Servo_ReadFeedbackChannel(instance->config.pFeedbackAdcHandle,
                                      instance->config.feedbackAdcChannel,
                                      &sample) != SERVO_OK) {
            return SERVO_ERROR;
        }

        sum += sample;
    }

    *adcRaw = (uint16_t)(sum / SERVO_FEEDBACK_AVG_SAMPLES);
    return SERVO_OK;
}

uint16_t Servo_ConvertFeedbackToMv(uint16_t adcRaw)
{
    if (adcRaw > SERVO_FEEDBACK_ADC_MAX) {
        adcRaw = SERVO_FEEDBACK_ADC_MAX;
    }

    return (uint16_t)(((uint32_t)adcRaw * SERVO_FEEDBACK_VREF_MV) / SERVO_FEEDBACK_ADC_MAX);
}

Servo_StatusTypeDef Servo_ConvertFeedbackRawToAngle(Servo_HandleTypeDef servo, uint16_t adcRaw, uint8_t *angle)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);
    uint16_t rawMin;
    uint16_t rawMax;
    uint8_t angleMin;
    uint8_t angleMax;
    uint8_t angleRange;
    uint32_t scaledAngle;

    if ((instance == NULL) || (angle == NULL) || (instance->config.pFeedbackAdcHandle == NULL) ||
        (instance->config.feedbackRawMin >= instance->config.feedbackRawMax) ||
        (instance->config.feedbackAngleMin > instance->config.feedbackAngleMax)) {
        return SERVO_ERROR_PARAM;
    }

    angleMin = instance->config.feedbackAngleMin;
    angleMax = instance->config.feedbackAngleMax;
    rawMin = instance->config.feedbackRawMin;
    rawMax = instance->config.feedbackRawMax;

    if ((instance->config.feedbackRawMid > instance->config.feedbackRawMin) &&
        (instance->config.feedbackRawMid < instance->config.feedbackRawMax) &&
        (instance->config.feedbackAngleMid > instance->config.feedbackAngleMin) &&
        (instance->config.feedbackAngleMid < instance->config.feedbackAngleMax)) {
        if (adcRaw <= instance->config.feedbackRawMid) {
            rawMax = instance->config.feedbackRawMid;
            angleMax = instance->config.feedbackAngleMid;
        } else {
            rawMin = instance->config.feedbackRawMid;
            angleMin = instance->config.feedbackAngleMid;
        }
    }

    angleRange = (uint8_t)(angleMax - angleMin);

    if (adcRaw <= rawMin) {
        *angle = angleMin;
        return SERVO_OK;
    }
    if (adcRaw >= rawMax) {
        *angle = angleMax;
        return SERVO_OK;
    }

    scaledAngle = (uint32_t)angleMin +
                  ((((uint32_t)(adcRaw - rawMin) * angleRange) + ((uint32_t)(rawMax - rawMin) / 2U)) /
                   (uint32_t)(rawMax - rawMin));
    *angle = (uint8_t)scaledAngle;
    return SERVO_OK;
}

Servo_StatusTypeDef Servo_GetFeedbackAngle(Servo_HandleTypeDef servo, uint8_t *angle)
{
    uint16_t adcRaw;

    if (Servo_GetFeedbackRaw(servo, &adcRaw) != SERVO_OK) {
        return SERVO_ERROR;
    }

    return Servo_ConvertFeedbackRawToAngle(servo, adcRaw, angle);
}

Servo_StatusTypeDef Servo_StartReciprocating(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    instance->state.isReciprocating = 1U;
    instance->state.isMotionActive = 0U;
    instance->state.direction = SERVO_DIRECTION_FORWARD;
    instance->state.lastStepTickMs = Platform_Time_GetTickMs();
    instance->state.isSignalActive = 1U;

    return Servo_WritePulseInternal(&instance->config, instance->state.currentPulse);
}

Servo_StatusTypeDef Servo_StopReciprocating(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    instance->state.isReciprocating = 0U;
    instance->state.direction = SERVO_DIRECTION_NONE;
    instance->state.isSignalActive = 1U;
    Servo_ArmReleaseDeadline(instance, Platform_Time_GetTickMs());

    return SERVO_OK;
}

Servo_StatusTypeDef Servo_ReleaseOutput(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    instance->state.isReciprocating = 0U;
    instance->state.isMotionActive = 0U;
    instance->state.direction = SERVO_DIRECTION_NONE;
    Servo_ReleaseSignalInternal(instance);

    return SERVO_OK;
}

uint8_t Servo_IsReciprocating(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    return (instance == NULL) ? 0U : instance->state.isReciprocating;
}

uint8_t Servo_IsMotionActive(Servo_HandleTypeDef servo)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    return (instance == NULL) ? 0U : instance->state.isMotionActive;
}

void Servo_Update(void)
{
    uint8_t index;
    uint32_t nowTickMs = Platform_Time_GetTickMs();

    for (index = 0U; index < SERVO_MAX_INSTANCES; index++) {
        Servo_InstanceTypeDef *instance = &s_servoPool[index];

        if ((instance->inUse == 0U) || (instance->state.isInitialized == 0U)) {
            continue;
        }

        if (instance->state.isMotionActive != 0U) {
            (void)Servo_UpdateMotionAt(instance, nowTickMs, 0U);
            continue;
        }

        if (instance->state.isReciprocating != 0U) {
            if ((uint32_t)(nowTickMs - instance->state.lastStepTickMs) < instance->config.stepDelayMs) {
                continue;
            }

            instance->state.lastStepTickMs = nowTickMs;

            if (instance->state.direction == SERVO_DIRECTION_FORWARD) {
                instance->state.currentPulse = (uint16_t)(instance->state.currentPulse + instance->config.stepSize);

                if (instance->state.currentPulse >= instance->config.pulseMax) {
                    instance->state.currentPulse = instance->config.pulseMax;
                    instance->state.direction = SERVO_DIRECTION_BACKWARD;
                }
            } else if (instance->state.direction == SERVO_DIRECTION_BACKWARD) {
                if (instance->state.currentPulse > instance->config.pulseMin) {
                    instance->state.currentPulse =
                        (uint16_t)(instance->state.currentPulse - instance->config.stepSize);

                    if (instance->state.currentPulse <= instance->config.pulseMin) {
                        instance->state.currentPulse = instance->config.pulseMin;
                        instance->state.direction = SERVO_DIRECTION_FORWARD;
                    }
                } else {
                    instance->state.direction = SERVO_DIRECTION_FORWARD;
                }
            }

            instance->state.targetPulse = instance->state.currentPulse;
            (void)Servo_WritePulseInternal(&instance->config, instance->state.currentPulse);
            continue;
        }

        if ((instance->config.commandPulseCount != 0U) && (instance->state.isSignalActive != 0U) &&
            (Servo_HasDeadlineElapsed(nowTickMs, instance->state.releaseDeadlineMs) != 0U)) {
            Servo_ReleaseSignalInternal(instance);
        }
    }
}

Servo_StatusTypeDef Servo_GetState(Servo_HandleTypeDef servo, Servo_StateTypeDef *state)
{
    Servo_InstanceTypeDef *instance = Servo_GetInstance(servo);

    if ((instance == NULL) || (state == NULL)) {
        return SERVO_ERROR_PARAM;
    }

    *state = instance->state;
    return SERVO_OK;
}

static Servo_InstanceTypeDef *Servo_GetInstance(Servo_HandleTypeDef servo)
{
    if (servo == NULL) {
        return NULL;
    }

    return (Servo_InstanceTypeDef *)servo;
}

static Servo_StatusTypeDef Servo_StartPwmOutput(Servo_ConfigTypeDef *config)
{
    return (Platform_Pwm_Start(config->pTimHandle, config->TimChannel) == HAL_OK) ? SERVO_OK : SERVO_ERROR_INIT;
}

static Servo_StatusTypeDef Servo_WritePulseInternal(Servo_ConfigTypeDef *config, uint16_t pulse)
{
    return (Platform_Pwm_WritePulse(config->pTimHandle, config->TimChannel, pulse) == HAL_OK)
               ? SERVO_OK
               : SERVO_ERROR_PARAM;
}

static uint16_t Servo_ClampPulse(const Servo_ConfigTypeDef *config, uint16_t pulse)
{
    if (config == NULL) {
        return pulse;
    }

    if (pulse < config->pulseMin) {
        return config->pulseMin;
    }
    if (pulse > config->pulseMax) {
        return config->pulseMax;
    }
    return pulse;
}

static uint16_t Servo_AngleToPulse(const Servo_ConfigTypeDef *config, uint8_t angle)
{
    uint16_t pulseRange;

    if (config == NULL) {
        return SERVO_DEFAULT_PULSE_CENTER;
    }

    if (angle > 180U) {
        angle = 180U;
    }

    pulseRange = (uint16_t)(config->pulseMax - config->pulseMin);
    return (uint16_t)(config->pulseMin + ((uint32_t)angle * pulseRange / 180U));
}

static uint16_t Servo_NormalizeMotionUpdatePeriod(uint16_t updatePeriodMs)
{
    if (updatePeriodMs == 0U) {
        return SERVO_MOTION_UPDATE_PERIOD_MS;
    }
    if (updatePeriodMs < SERVO_MOTION_UPDATE_PERIOD_MIN_MS) {
        return SERVO_MOTION_UPDATE_PERIOD_MIN_MS;
    }
    return updatePeriodMs;
}

static uint16_t Servo_InterpolateSmoothStepPulse(uint16_t startPulse,
                                                 uint16_t targetPulse,
                                                 uint32_t elapsedMs,
                                                 uint16_t durationMs,
                                                 uint8_t easeStrengthPercent)
{
    int32_t delta = (int32_t)targetPulse - (int32_t)startPulse;
    int32_t step;
    uint64_t elapsed;
    uint64_t duration;
    uint64_t linearNumerator;
    uint64_t smoothNumerator;
    uint64_t blendedNumerator;
    uint64_t denominator;
    uint32_t strength;

    if ((durationMs == 0U) || (elapsedMs >= durationMs)) {
        return targetPulse;
    }

    elapsed = elapsedMs;
    duration = durationMs;
    strength = easeStrengthPercent;
    if (strength > SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT) {
        strength = SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT;
    }

    linearNumerator = elapsed * duration * duration;
    smoothNumerator = elapsed * elapsed * ((3ULL * duration) - (2ULL * elapsed));
    blendedNumerator = ((linearNumerator * (SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT - strength)) +
                        (smoothNumerator * strength) +
                        (SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT / 2U)) /
                       SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT;
    denominator = duration * duration * duration;

    if (delta >= 0) {
        step = (int32_t)(((uint64_t)delta * blendedNumerator + (denominator / 2ULL)) / denominator);
    } else {
        step = -(int32_t)(((uint64_t)(-delta) * blendedNumerator + (denominator / 2ULL)) / denominator);
    }

    return (uint16_t)((int32_t)startPulse + step);
}

static uint16_t Servo_InterpolatePulse(uint16_t startPulse,
                                       uint16_t targetPulse,
                                       uint32_t elapsedMs,
                                       uint16_t durationMs,
                                       Servo_MotionProfileTypeDef profile,
                                       uint8_t easeStrengthPercent)
{
    int32_t delta = (int32_t)targetPulse - (int32_t)startPulse;
    int32_t step;

    if ((durationMs == 0U) || (elapsedMs >= durationMs)) {
        return targetPulse;
    }

    if (profile == SERVO_MOTION_PROFILE_ANTI_DROP) {
        uint16_t preloadDurationMs = (uint16_t)(((uint32_t)durationMs * SERVO_ANTI_DROP_PRELOAD_PERCENT) / 100U);
        uint16_t holdDurationMs = (uint16_t)(((uint32_t)durationMs * SERVO_ANTI_DROP_HOLD_PERCENT) / 100U);
        uint16_t descentDurationMs;
        uint16_t preloadPulse;
        uint32_t descentElapsedMs;

        if (delta >= 0) {
            return Servo_InterpolateSmoothStepPulse(startPulse,
                                                    targetPulse,
                                                    elapsedMs,
                                                    durationMs,
                                                    easeStrengthPercent);
        }

        if (preloadDurationMs < SERVO_MOTION_UPDATE_PERIOD_MS) {
            preloadDurationMs = SERVO_MOTION_UPDATE_PERIOD_MS;
        }
        if (holdDurationMs < SERVO_MOTION_UPDATE_PERIOD_MS) {
            holdDurationMs = SERVO_MOTION_UPDATE_PERIOD_MS;
        }
        if ((uint32_t)preloadDurationMs + (uint32_t)holdDurationMs >= durationMs) {
            preloadDurationMs = 0U;
            holdDurationMs = 0U;
        }

        preloadPulse = (uint16_t)(startPulse + SERVO_ANTI_DROP_PRELOAD_US);
        if (elapsedMs <= preloadDurationMs) {
            return Servo_InterpolateSmoothStepPulse(startPulse,
                                                    preloadPulse,
                                                    elapsedMs,
                                                    preloadDurationMs,
                                                    easeStrengthPercent);
        }
        if (elapsedMs <= ((uint32_t)preloadDurationMs + holdDurationMs)) {
            return preloadPulse;
        }

        descentElapsedMs = elapsedMs - preloadDurationMs - holdDurationMs;
        descentDurationMs = (uint16_t)(durationMs - preloadDurationMs - holdDurationMs);
        return Servo_InterpolateSmoothStepPulse(preloadPulse,
                                                targetPulse,
                                                descentElapsedMs,
                                                descentDurationMs,
                                                easeStrengthPercent);
    }

    if (profile == SERVO_MOTION_PROFILE_EASE_IN_OUT) {
        return Servo_InterpolateSmoothStepPulse(startPulse,
                                                targetPulse,
                                                elapsedMs,
                                                durationMs,
                                                easeStrengthPercent);
    } else if (delta >= 0) {
        step = (int32_t)(((uint32_t)delta * elapsedMs + (durationMs / 2U)) / durationMs);
    } else {
        step = -(int32_t)((((uint32_t)(-delta)) * elapsedMs + (durationMs / 2U)) / durationMs);
    }

    return (uint16_t)((int32_t)startPulse + step);
}

static uint8_t Servo_ShouldSkipSmallAntiDropStep(const Servo_StateTypeDef *state, uint16_t nextPulse, uint8_t complete)
{
    uint16_t diff;

    if ((state == NULL) || (complete != 0U) || (state->motionProfile != SERVO_MOTION_PROFILE_ANTI_DROP)) {
        return 0U;
    }

    diff = (nextPulse >= state->currentPulse) ? (uint16_t)(nextPulse - state->currentPulse)
                                              : (uint16_t)(state->currentPulse - nextPulse);
    return (diff < SERVO_ANTI_DROP_MIN_STEP_US) ? 1U : 0U;
}

static Servo_StatusTypeDef Servo_UpdateMotionAt(Servo_InstanceTypeDef *instance, uint32_t nowTickMs, uint8_t force)
{
    uint32_t elapsedMs;
    uint16_t nextPulse;
    uint16_t updatePeriodMs;
    uint8_t complete;

    if (instance == NULL) {
        return SERVO_ERROR_PARAM;
    }

    if (instance->state.isMotionActive == 0U) {
        return SERVO_OK;
    }

    elapsedMs = nowTickMs - instance->state.motionStartedAtMs;
    complete = (elapsedMs >= instance->state.motionDurationMs) ? 1U : 0U;
    updatePeriodMs = Servo_NormalizeMotionUpdatePeriod(instance->state.motionUpdatePeriodMs);
    if ((force == 0U) && (complete == 0U) &&
        ((uint32_t)(nowTickMs - instance->state.lastMotionUpdateTickMs) < updatePeriodMs)) {
        return SERVO_OK;
    }

    nextPulse = Servo_InterpolatePulse(instance->state.motionStartPulse,
                                      instance->state.motionTargetPulse,
                                      elapsedMs,
                                      instance->state.motionDurationMs,
                                      instance->state.motionProfile,
                                      instance->state.motionEaseStrengthPercent);
    nextPulse = Servo_ClampPulse(&instance->config, nextPulse);
    if ((force == 0U) && (Servo_ShouldSkipSmallAntiDropStep(&instance->state, nextPulse, complete) != 0U)) {
        return SERVO_OK;
    }
    instance->state.currentPulse = nextPulse;
    instance->state.lastMotionUpdateTickMs = nowTickMs;
    if (complete != 0U) {
        instance->state.isMotionActive = 0U;
        instance->state.targetPulse = instance->state.motionTargetPulse;
        instance->state.currentPulse = instance->state.motionTargetPulse;
        instance->state.motionDurationMs = 0U;
        instance->state.motionUpdatePeriodMs = updatePeriodMs;
    }

    if (Servo_WritePulseInternal(&instance->config, instance->state.currentPulse) != SERVO_OK) {
        return SERVO_ERROR_PARAM;
    }

    instance->state.isSignalActive = 1U;
    Servo_ArmReleaseDeadline(instance, nowTickMs);
    return SERVO_OK;
}

static Servo_StatusTypeDef Servo_InitFeedback(Servo_ConfigTypeDef *config)
{
    if (config->pFeedbackAdcHandle == NULL) {
        return SERVO_OK;
    }

    return (HAL_ADCEx_Calibration_Start(config->pFeedbackAdcHandle) == HAL_OK) ? SERVO_OK : SERVO_ERROR_INIT;
}

static Servo_StatusTypeDef Servo_ReadFeedbackChannel(ADC_HandleTypeDef *adcHandle, uint32_t adcChannel, uint16_t *sample)
{
    ADC_TypeDef *adcInstance;
    ADC_ChannelConfTypeDef adcConfig = {0};
    uint32_t savedCr1;
    uint32_t savedSqr1;
    uint32_t savedSqr2;
    uint32_t savedSqr3;
    uint32_t savedSmpr1;
    uint32_t savedSmpr2;
    uint32_t savedScanConvMode;
    uint32_t savedNbrOfConversion;
    Servo_StatusTypeDef status = SERVO_ERROR;

    if ((adcHandle == NULL) || (sample == NULL)) {
        return SERVO_ERROR_PARAM;
    }

    adcInstance = adcHandle->Instance;
    savedCr1 = adcInstance->CR1;
    savedSqr1 = adcInstance->SQR1;
    savedSqr2 = adcInstance->SQR2;
    savedSqr3 = adcInstance->SQR3;
    savedSmpr1 = adcInstance->SMPR1;
    savedSmpr2 = adcInstance->SMPR2;
    savedScanConvMode = adcHandle->Init.ScanConvMode;
    savedNbrOfConversion = adcHandle->Init.NbrOfConversion;

    adcHandle->Init.ScanConvMode = ADC_SCAN_DISABLE;
    adcHandle->Init.NbrOfConversion = 1U;
    adcConfig.Channel = adcChannel;
    adcConfig.Rank = ADC_REGULAR_RANK_1;
    adcConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if ((HAL_ADC_Init(adcHandle) == HAL_OK) && (HAL_ADC_ConfigChannel(adcHandle, &adcConfig) == HAL_OK) &&
        (HAL_ADC_Start(adcHandle) == HAL_OK)) {
        if (HAL_ADC_PollForConversion(adcHandle, SERVO_FEEDBACK_ADC_TIMEOUT_MS) == HAL_OK) {
            *sample = (uint16_t)HAL_ADC_GetValue(adcHandle);
            status = SERVO_OK;
        }
        (void)HAL_ADC_Stop(adcHandle);
    }

    adcInstance->CR1 = savedCr1;
    adcInstance->SQR1 = savedSqr1;
    adcInstance->SQR2 = savedSqr2;
    adcInstance->SQR3 = savedSqr3;
    adcInstance->SMPR1 = savedSmpr1;
    adcInstance->SMPR2 = savedSmpr2;
    adcHandle->Init.ScanConvMode = savedScanConvMode;
    adcHandle->Init.NbrOfConversion = savedNbrOfConversion;

    return status;
}


static void Servo_ArmReleaseDeadline(Servo_InstanceTypeDef *instance, uint32_t nowTickMs)
{
    if (instance->config.commandPulseCount == 0U) {
        instance->state.releaseDeadlineMs = 0U;
        return;
    }

    instance->state.releaseDeadlineMs =
        nowTickMs + ((uint32_t)instance->config.commandPulseCount * SERVO_PWM_FRAME_PERIOD_MS);
}

static void Servo_ReleaseSignalInternal(Servo_InstanceTypeDef *instance)
{
    (void)Servo_WritePulseInternal(&instance->config, SERVO_RELEASE_PULSE_US);
    instance->state.isSignalActive = 0U;
}

static uint8_t Servo_HasDeadlineElapsed(uint32_t nowTickMs, uint32_t deadlineTickMs)
{
    return ((int32_t)(nowTickMs - deadlineTickMs) >= 0) ? 1U : 0U;
}
