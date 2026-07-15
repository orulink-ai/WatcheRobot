#include "app.h"

#include "adc.h"
#include "app_config.h"
#include "bmi160.h"
#include "bottom_ir.h"
#include "cli.h"
#include "ip5306_irq.h"
#include "ip5306_key.h"
#include "main.h"
#include "platform_coproc_uart.h"
#include "platform_time.h"
#include "platform_uart.h"
#include "qmc6309.h"
#include "sensors.h"
#include "servo.h"
#include "tim.h"
#include "ws2812.h"

#if !defined(WATCHER_STRESS_BUILD)
/*
 * Normal build keeps the local board demo peripherals alive while USART2
 * carries the coprocessor protocol bring-up path.
 */
static Servo_HandleTypeDef s_servo1Handle = NULL;
static Servo_HandleTypeDef s_servo2Handle = NULL;
static uint8_t s_bottomIrInitialized;
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
#define APP_SERVO_FEEDBACK_RAW_LOCK_DEADBAND 24U
#define APP_SERVO_FEEDBACK_CONFIRM_COUNT 2U
#define APP_SERVO_FEEDBACK_ANGLE_STEP_DEG 2U

typedef struct {
    uint8_t initialized;
    uint8_t candidateCount;
    int8_t candidateDirection;
    uint16_t stableRaw;
    uint16_t candidateRaw;
    uint8_t stableAngle;
    uint8_t candidateAngle;
} App_ServoFeedbackFilterTypeDef;
#endif
typedef struct {
    uint8_t minAngle;
    uint8_t maxAngle;
} App_ServoLimitTypeDef;

static App_ServoLimitTypeDef s_servoLimits[2] = {
    {100U, 130U},
    {30U, 150U}
};
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
static App_ServoFeedbackFilterTypeDef s_servoFeedbackFilters[2];
#endif
#if (APP_LED_ENABLE != 0U)
static WS2812_HandleTypeDef s_ws2812SideHandle = NULL;
static WS2812_HandleTypeDef s_ws2812BottomHandle = NULL;
#endif

static Servo_HandleTypeDef App_GetServoHandle(uint8_t servoIndex);
static uint8_t App_ClampServoAngle(uint8_t servoIndex, uint8_t angle);
static uint8_t App_PulseToAngle(Servo_HandleTypeDef servoHandle, uint16_t pulseUs, uint8_t *angle);
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
static uint8_t App_QuantizeServoFeedbackAngle(uint8_t angle);
static void App_FilterServoFeedback(uint8_t servoIndex, uint16_t raw, uint8_t angle, uint16_t *filteredRaw, uint8_t *filteredAngle);
#endif
static void App_InitIp5306Power(void);
static void App_RunPower5VServoProbe(void);
#if (APP_LED_ENABLE != 0U)
typedef struct {
    uint8_t isBreathing;
    uint8_t isRainbow;
    uint8_t baseRed;
    uint8_t baseGreen;
    uint8_t baseBlue;
    uint8_t brightness;
    uint8_t isRising;
    uint8_t step;
    uint8_t intervalMs;
    uint8_t rainbowOffset;
    uint8_t rainbowStep;
    uint8_t rainbowIntervalMs;
    uint32_t lastUpdateTickMs;
} App_Ws2812EffectStateTypeDef;

static uint8_t App_ApplyWs2812Rgb(WS2812_HandleTypeDef ws2812, uint8_t red, uint8_t green, uint8_t blue);
static uint8_t App_ApplyWs2812Rainbow(WS2812_HandleTypeDef ws2812, uint8_t offset);
static void App_UpdateWs2812Effect(WS2812_HandleTypeDef ws2812, App_Ws2812EffectStateTypeDef *effectState);
static uint8_t App_StartWs2812BreatheForHandle(WS2812_HandleTypeDef ws2812,
                                               App_Ws2812EffectStateTypeDef *effectState,
                                               uint8_t red,
                                               uint8_t green,
                                               uint8_t blue,
                                               uint8_t step,
                                               uint8_t intervalMs);
static uint8_t App_StartWs2812RainbowForHandle(WS2812_HandleTypeDef ws2812,
                                               App_Ws2812EffectStateTypeDef *effectState,
                                               uint8_t step,
                                               uint8_t intervalMs);
static uint8_t App_RunWs2812ColorTest(WS2812_HandleTypeDef ws2812, App_Ws2812EffectStateTypeDef *effectState);
static uint8_t App_ScaleWs2812Channel(uint8_t channel, uint8_t brightness);
static void App_GetRainbowColor(uint8_t position, uint8_t *red, uint8_t *green, uint8_t *blue);
#endif

static const Servo_ConfigTypeDef s_servo1Config = {
    .pTimHandle = &htim3,
    .TimChannel = TIM_CHANNEL_1,
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
    .pFeedbackAdcHandle = &hadc1,
    .feedbackSampleIndex = 1U,
    .feedbackAdcChannel = ADC_CHANNEL_0,
#else
    .pFeedbackAdcHandle = NULL,
    .feedbackSampleIndex = 0U,
    .feedbackAdcChannel = 0U,
#endif
    /* Installed robot calibration through the PA0 feedback divider. */
    .feedbackRawMin = 1433U,
    .feedbackRawMid = 1600U,
    .feedbackRawMax = 1661U,
    .feedbackAngleMin = 100U,
    .feedbackAngleMid = 115U,
    .feedbackAngleMax = 130U,
    .pulseMin = 500U,
    .pulseMax = 2500U,
    .pulseCenter = 1500U,
    .commandPulseCount = APP_SERVO_COMMAND_PULSE_COUNT,
    .stepSize = 66U,
    .stepDelayMs = APP_SERVO_UPDATE_PERIOD_MS
};

static const Servo_ConfigTypeDef s_servo2Config = {
    .pTimHandle = &htim3,
    .TimChannel = TIM_CHANNEL_2,
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
    .pFeedbackAdcHandle = &hadc1,
    .feedbackSampleIndex = 2U,
    .feedbackAdcChannel = ADC_CHANNEL_5,
#else
    .pFeedbackAdcHandle = NULL,
    .feedbackSampleIndex = 0U,
    .feedbackAdcChannel = 0U,
#endif
    /* Installed robot calibration through the PA5 feedback divider. */
    .feedbackRawMin = 1018U,
    .feedbackRawMid = 2068U,
    .feedbackRawMax = 3113U,
    .feedbackAngleMin = 30U,
    .feedbackAngleMid = 90U,
    .feedbackAngleMax = 150U,
    .pulseMin = 500U,
    .pulseMax = 2500U,
    .pulseCenter = 1500U,
    .commandPulseCount = APP_SERVO_COMMAND_PULSE_COUNT,
    .stepSize = 10U,
    .stepDelayMs = APP_SERVO_UPDATE_PERIOD_MS
};

static const BottomIr_ConfigTypeDef s_bottomIrConfig = {
    .pAdcHandle = &hadc1,
    .sampleIndex = APP_BOTTOM_IR_ADC_SAMPLE_INDEX,
    .averageSamples = APP_BOTTOM_IR_ADC_AVG_SAMPLES,
    .thresholdRaw = APP_BOTTOM_IR_THRESHOLD_RAW,
    .activeAboveThreshold = APP_BOTTOM_IR_ACTIVE_ABOVE_THRESHOLD,
    .adcTimeoutMs = APP_BOTTOM_IR_ADC_TIMEOUT_MS
};

#if (APP_LED_ENABLE != 0U)
static const WS2812_ConfigTypeDef s_ws2812SideConfig = {
    .pTimHandle = &htim1,
    .TimChannel = TIM_CHANNEL_1,
    .ledCount = APP_WS2812_SIDE_LED_COUNT,
    .pulseHigh0 = APP_WS2812_BIT_0_PULSE,
    .pulseHigh1 = APP_WS2812_BIT_1_PULSE,
    .resetSlots = APP_WS2812_RESET_SLOTS
};

static const WS2812_ConfigTypeDef s_ws2812BottomConfig = {
    .pTimHandle = &htim2,
    .TimChannel = TIM_CHANNEL_3,
    .ledCount = APP_WS2812_BOTTOM_LED_COUNT,
    .pulseHigh0 = APP_WS2812_BIT_0_PULSE,
    .pulseHigh1 = APP_WS2812_BIT_1_PULSE,
    .resetSlots = APP_WS2812_RESET_SLOTS
};

static App_Ws2812EffectStateTypeDef s_ws2812SideEffectState;
static App_Ws2812EffectStateTypeDef s_ws2812BottomEffectState;
#endif
#endif

void App_Init(void)
{
    Platform_Uart_Init();

#if !defined(WATCHER_STRESS_BUILD)
    App_InitIp5306Power();

    s_servo1Handle = Servo_Init(&s_servo1Config);
    if (s_servo1Handle == NULL) {
        Error_Handler();
    }

    s_servo2Handle = Servo_Init(&s_servo2Config);
    if (s_servo2Handle == NULL) {
        Error_Handler();
    }

#if (APP_LED_ENABLE != 0U)
    s_ws2812SideHandle = WS2812_Init(&s_ws2812SideConfig);
    if (s_ws2812SideHandle == NULL) {
        Error_Handler();
    }
    s_ws2812BottomHandle = WS2812_Init(&s_ws2812BottomConfig);
    if (s_ws2812BottomHandle == NULL) {
        Error_Handler();
    }
    if (App_SetWs2812LedCount(APP_WS2812_SIDE_DEFAULT_ACTIVE_LED_COUNT) == 0U) {
        Error_Handler();
    }
    if (App_SetWs2812Rgb(0U, 0U, 0U) == 0U) {
        Error_Handler();
    }
    if (App_SetBottomWs2812LedCount(APP_WS2812_BOTTOM_DEFAULT_ACTIVE_LED_COUNT) == 0U) {
        Error_Handler();
    }
    if (App_SetBottomWs2812Rgb(0U, 0U, 0U) == 0U) {
        Error_Handler();
    }
#endif

#if (APP_SENSOR_REPORT_ENABLE != 0U)
    Sensors_Scan();
#endif
#if (APP_BMI160_ENABLE != 0U)
    BMI160_Init();
#endif
#if (APP_QMC6309_ENABLE != 0U)
    (void)QMC6309_Init();
#endif
#endif

    Platform_CoprocUart_Init();

#if !defined(WATCHER_STRESS_BUILD)
    Platform_Uart_Print("\r\n=== STM32 CLI Ready ===\r\n");
    Platform_Uart_Print("USART1 @ 115200 8N1\r\n");
    Platform_Uart_Print("Commands: help, servo 90, servo_off, servo_fb, servo_angle, ws red, ws_bottom red\r\n");
    Platform_Uart_Print("> ");
#endif
}

#if !defined(WATCHER_STRESS_BUILD)
static void App_InitIp5306Power(void)
{
    IP5306_Irq_Init();
    IP5306_Key_Init();

#if (APP_IP5306_BOOT_AUTO_ENABLE != 0U)
    Platform_Time_DelayMs(APP_IP5306_BOOT_ENABLE_DELAY_MS);
    IP5306_Key_ShortPress();
#endif

    Platform_Time_DelayMs(APP_I2C_SENSOR_POWER_STABLE_DELAY_MS);
}
#endif

void App_RunOnce(void)
{
#if defined(WATCHER_STRESS_BUILD)
    Platform_CoprocUart_Poll();
    Platform_Time_DelayMs(1U);
#else
    uint32_t cycleStartMs = Platform_Time_GetTickMs();

    while ((Platform_Time_GetTickMs() - cycleStartMs) < APP_SERVO_UPDATE_PERIOD_MS) {
        Servo_Update();
#if (APP_LED_ENABLE != 0U)
        App_UpdateWs2812Effect(s_ws2812SideHandle, &s_ws2812SideEffectState);
        App_UpdateWs2812Effect(s_ws2812BottomHandle, &s_ws2812BottomEffectState);
#endif
        Cli_ProcessInput();
        Platform_CoprocUart_Poll();
    }
#endif
}

#if !defined(WATCHER_STRESS_BUILD)
uint8_t App_SetServoAngle(uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if (servoHandle == NULL) {
        return 0U;
    }

    angle = App_ClampServoAngle(servoIndex, angle);
    Servo_StopReciprocating(servoHandle);

    if (Servo_SetAngle(servoHandle, angle) != SERVO_OK) {
        return 0U;
    }

    if (pulseUs != NULL) {
        if (Servo_GetPulse(servoHandle, pulseUs) != SERVO_OK) {
            return 0U;
        }
    }

    return 1U;
}

uint8_t App_SetServoDegX10(uint8_t servoIndex, int16_t degX10, uint16_t *pulseUs)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);
    const Servo_ConfigTypeDef *servoConfig;
    uint16_t pulseRange;
    uint16_t targetPulseUs;
    int16_t minDegX10;
    int16_t maxDegX10;

    if (servoHandle == NULL) {
        return 0U;
    }

    if (servoIndex == 1U) {
        servoConfig = &s_servo1Config;
    } else if (servoIndex == 2U) {
        servoConfig = &s_servo2Config;
    } else {
        return 0U;
    }

    minDegX10 = (int16_t)((uint16_t)s_servoLimits[servoIndex - 1U].minAngle * 10U);
    maxDegX10 = (int16_t)((uint16_t)s_servoLimits[servoIndex - 1U].maxAngle * 10U);
    if (degX10 < minDegX10) {
        degX10 = minDegX10;
    } else if (degX10 > maxDegX10) {
        degX10 = maxDegX10;
    }

    pulseRange = (uint16_t)(servoConfig->pulseMax - servoConfig->pulseMin);
    targetPulseUs = (uint16_t)(servoConfig->pulseMin +
                               (((uint32_t)(uint16_t)degX10 * pulseRange + 900U) / 1800U));

    if (Servo_SetPulse(servoHandle, targetPulseUs) != SERVO_OK) {
        return 0U;
    }

    if (pulseUs != NULL) {
        *pulseUs = targetPulseUs;
    }

    return 1U;
}

uint8_t App_MoveServoToAngleOverTime(uint8_t servoIndex,
                                     uint8_t angle,
                                     uint16_t durationMs,
                                     Servo_MotionProfileTypeDef profile,
                                     uint16_t *targetPulseUs)
{
    return App_MoveServoToAngleOverTimeWithSteps(servoIndex,
                                                 angle,
                                                 durationMs,
                                                 0U,
                                                 profile,
                                                 targetPulseUs,
                                                 NULL);
}

uint8_t App_MoveServoToAngleOverTimeWithSteps(uint8_t servoIndex,
                                              uint8_t angle,
                                              uint16_t durationMs,
                                              uint16_t requestedSteps,
                                              Servo_MotionProfileTypeDef profile,
                                              uint16_t *targetPulseUs,
                                              uint16_t *effectiveUpdatePeriodMs)
{
    return App_MoveServoToAngleOverTimeWithStepsAndEaseStrength(servoIndex,
                                                                angle,
                                                                durationMs,
                                                                requestedSteps,
                                                                profile,
                                                                SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT,
                                                                targetPulseUs,
                                                                effectiveUpdatePeriodMs);
}

uint8_t App_MoveServoToAngleOverTimeWithStepsAndEaseStrength(uint8_t servoIndex,
                                                             uint8_t angle,
                                                             uint16_t durationMs,
                                                             uint16_t requestedSteps,
                                                             Servo_MotionProfileTypeDef profile,
                                                             uint8_t easeStrengthPercent,
                                                             uint16_t *targetPulseUs,
                                                             uint16_t *effectiveUpdatePeriodMs)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);
    Servo_StateTypeDef state;
    uint16_t updatePeriodMs;

    if ((servoHandle == NULL) || (durationMs == 0U) ||
        (easeStrengthPercent > SERVO_MOTION_EASE_STRENGTH_MAX_PERCENT)) {
        return 0U;
    }

    angle = App_ClampServoAngle(servoIndex, angle);
    updatePeriodMs = Servo_MotionUpdatePeriodFromSteps(durationMs, requestedSteps);
    if (Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(servoHandle,
                                                                 angle,
                                                                 durationMs,
                                                                 profile,
                                                                 updatePeriodMs,
                                                                 easeStrengthPercent) != SERVO_OK) {
        return 0U;
    }

    if ((targetPulseUs != NULL) || (effectiveUpdatePeriodMs != NULL)) {
        if (Servo_GetState(servoHandle, &state) != SERVO_OK) {
            return 0U;
        }
    }
    if (targetPulseUs != NULL) {
        *targetPulseUs = state.motionTargetPulse;
    }
    if (effectiveUpdatePeriodMs != NULL) {
        *effectiveUpdatePeriodMs = state.motionUpdatePeriodMs;
    }

    return 1U;
}

uint8_t App_StopServoMotion(uint8_t servoIndex)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if (servoHandle == NULL) {
        return 0U;
    }

    return (Servo_StopMotion(servoHandle) == SERVO_OK) ? 1U : 0U;
}

uint8_t App_SetServoLimit(uint8_t servoIndex, uint8_t minAngle, uint8_t maxAngle)
{
    if ((servoIndex < 1U) || (servoIndex > 2U) || (minAngle > maxAngle) || (maxAngle > 180U)) {
        return 0U;
    }

    s_servoLimits[servoIndex - 1U].minAngle = minAngle;
    s_servoLimits[servoIndex - 1U].maxAngle = maxAngle;
    return 1U;
}

uint8_t App_GetServoLimit(uint8_t servoIndex, uint8_t *minAngle, uint8_t *maxAngle)
{
    if ((servoIndex < 1U) || (servoIndex > 2U) || (minAngle == NULL) || (maxAngle == NULL)) {
        return 0U;
    }

    *minAngle = s_servoLimits[servoIndex - 1U].minAngle;
    *maxAngle = s_servoLimits[servoIndex - 1U].maxAngle;
    return 1U;
}

uint8_t App_GetServoCommandAngle(uint8_t servoIndex, uint8_t *angle)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);
    Servo_StateTypeDef state;

    if ((servoHandle == NULL) || (angle == NULL)) {
        return 0U;
    }

    if (Servo_GetState(servoHandle, &state) != SERVO_OK) {
        return 0U;
    }

    return App_PulseToAngle(servoHandle, state.currentPulse, angle);
}

#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
static uint8_t App_QuantizeServoFeedbackAngle(uint8_t angle)
{
    uint8_t step = APP_SERVO_FEEDBACK_ANGLE_STEP_DEG;
    uint16_t rounded;

    if (step <= 1U) {
        return angle;
    }

    rounded = (uint16_t)(((uint16_t)angle + (step / 2U)) / step) * step;
    return (rounded > 180U) ? 180U : (uint8_t)rounded;
}

static void App_FilterServoFeedback(uint8_t servoIndex, uint16_t raw, uint8_t angle, uint16_t *filteredRaw, uint8_t *filteredAngle)
{
    App_ServoFeedbackFilterTypeDef *filter;
    uint16_t delta;
    int8_t direction;
    uint8_t quantizedAngle;

    if ((servoIndex == 0U) || (servoIndex > 2U) || (filteredRaw == NULL) || (filteredAngle == NULL)) {
        return;
    }

    quantizedAngle = App_QuantizeServoFeedbackAngle(angle);
    filter = &s_servoFeedbackFilters[servoIndex - 1U];
    if (filter->initialized == 0U) {
        filter->initialized = 1U;
        filter->stableRaw = raw;
        filter->stableAngle = quantizedAngle;
        filter->candidateRaw = raw;
        filter->candidateAngle = quantizedAngle;
        filter->candidateCount = 0U;
        filter->candidateDirection = 0;
    }

    delta = (raw >= filter->stableRaw) ? (uint16_t)(raw - filter->stableRaw) : (uint16_t)(filter->stableRaw - raw);
    if (delta <= APP_SERVO_FEEDBACK_RAW_LOCK_DEADBAND) {
        filter->candidateCount = 0U;
        filter->candidateDirection = 0;
    } else {
        direction = (raw > filter->stableRaw) ? 1 : -1;
        if (filter->candidateDirection == direction) {
            if (filter->candidateCount < APP_SERVO_FEEDBACK_CONFIRM_COUNT) {
                filter->candidateCount++;
            }
        } else {
            filter->candidateDirection = direction;
            filter->candidateCount = 1U;
        }
        filter->candidateRaw = raw;
        filter->candidateAngle = quantizedAngle;
        if (filter->candidateCount >= APP_SERVO_FEEDBACK_CONFIRM_COUNT) {
            filter->stableRaw = filter->candidateRaw;
            filter->stableAngle = filter->candidateAngle;
            filter->candidateCount = 0U;
            filter->candidateDirection = 0;
        }
    }

    *filteredRaw = filter->stableRaw;
    *filteredAngle = filter->stableAngle;
}
#endif

uint8_t App_GetServoFeedbackSnapshot(uint8_t servoIndex, App_ServoFeedbackSnapshotTypeDef *snapshot)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);
    Servo_StateTypeDef state;
    uint8_t commandAngle;
#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
    uint16_t feedbackRaw;
    uint8_t feedbackAngle;
#endif

    if ((servoHandle == NULL) || (snapshot == NULL)) {
        return 0U;
    }

    if (Servo_GetState(servoHandle, &state) != SERVO_OK ||
        App_PulseToAngle(servoHandle, state.currentPulse, &commandAngle) == 0U) {
        return 0U;
    }

    snapshot->servoIndex = servoIndex;
    snapshot->commandAngle = commandAngle;
    snapshot->commandPulseUs = state.currentPulse;
    snapshot->feedbackValid = 0U;
    snapshot->feedbackRaw = 0U;
    snapshot->feedbackMv = 0U;
    snapshot->feedbackAngle = commandAngle;

#if (APP_SERVO_FEEDBACK_ENABLE != 0U)
    if (Servo_GetFeedbackRaw(servoHandle, &feedbackRaw) == SERVO_OK) {
        if (Servo_ConvertFeedbackRawToAngle(servoHandle, feedbackRaw, &feedbackAngle) == SERVO_OK) {
            App_FilterServoFeedback(servoIndex, feedbackRaw, feedbackAngle, &snapshot->feedbackRaw, &snapshot->feedbackAngle);
            snapshot->feedbackValid = 1U;
            snapshot->feedbackMv = Servo_ConvertFeedbackToMv(snapshot->feedbackRaw);
        }
    }
#endif

    return 1U;
}

uint8_t App_GetServoFeedbackRaw(uint8_t servoIndex, uint16_t *adcRaw)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if ((servoHandle == NULL) || (adcRaw == NULL)) {
        return 0U;
    }

    return (Servo_GetFeedbackRaw(servoHandle, adcRaw) == SERVO_OK) ? 1U : 0U;
}

uint16_t App_ConvertServoFeedbackToMv(uint16_t adcRaw)
{
    return Servo_ConvertFeedbackToMv(adcRaw);
}

uint8_t App_ReadBottomIrTest(App_BottomIrTestSnapshotTypeDef *snapshot)
{
#if (APP_BOTTOM_IR_ENABLE != 0U)
    BottomIr_SnapshotTypeDef bottomIrSnapshot;

    if (snapshot == NULL) {
        return 0U;
    }

    if (s_bottomIrInitialized == 0U) {
        if (BottomIr_Init(&s_bottomIrConfig) != BOTTOM_IR_OK) {
            return 0U;
        }
        s_bottomIrInitialized = 1U;
    }

    if (BottomIr_ReadSnapshot(&s_bottomIrConfig, &bottomIrSnapshot) != BOTTOM_IR_OK) {
        return 0U;
    }

    snapshot->raw = bottomIrSnapshot.raw;
    snapshot->millivolts = bottomIrSnapshot.millivolts;
    snapshot->thresholdRaw = bottomIrSnapshot.thresholdRaw;
    snapshot->aboveThreshold = bottomIrSnapshot.aboveThreshold;
    snapshot->active = bottomIrSnapshot.active;

    return 1U;
#else
    (void)snapshot;
    return 0U;
#endif
}

uint8_t App_GetServoFeedbackAngle(uint8_t servoIndex, uint8_t *angle)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if ((servoHandle == NULL) || (angle == NULL)) {
        return 0U;
    }

    return (Servo_GetFeedbackAngle(servoHandle, angle) == SERVO_OK) ? 1U : 0U;
}

uint8_t App_ReleaseServo(uint8_t servoIndex)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if (servoHandle == NULL) {
        return 0U;
    }

    return (Servo_ReleaseOutput(servoHandle) == SERVO_OK) ? 1U : 0U;
}

uint8_t App_StartServoReciprocating(uint8_t servoIndex)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if (servoHandle == NULL) {
        return 0U;
    }

    return (Servo_StartReciprocating(servoHandle) == SERVO_OK) ? 1U : 0U;
}

uint8_t App_GetServoState(uint8_t servoIndex, Servo_StateTypeDef *state)
{
    Servo_HandleTypeDef servoHandle = App_GetServoHandle(servoIndex);

    if ((servoHandle == NULL) || (state == NULL)) {
        return 0U;
    }

    return (Servo_GetState(servoHandle, state) == SERVO_OK) ? 1U : 0U;
}

#if (APP_LED_ENABLE != 0U)
uint8_t App_SetWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    App_StopWs2812Effect();

    return App_ApplyWs2812Rgb(s_ws2812SideHandle, red, green, blue);
}

uint8_t App_SetWs2812LedCount(uint16_t ledCount)
{
    if (s_ws2812SideHandle == NULL) {
        return 0U;
    }

    if (WS2812_SetLedCount(s_ws2812SideHandle, ledCount) != WS2812_OK) {
        return 0U;
    }

    return App_ApplyWs2812Rgb(s_ws2812SideHandle, 0U, 0U, 0U);
}

uint16_t App_GetWs2812LedCount(void)
{
    return (s_ws2812SideHandle == NULL) ? 0U : WS2812_GetLedCount(s_ws2812SideHandle);
}

uint16_t App_GetWs2812MaxLedCount(void)
{
    return (s_ws2812SideHandle == NULL) ? 0U : WS2812_GetMaxLedCount(s_ws2812SideHandle);
}

uint8_t App_StartWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    return App_StartWs2812BreatheTimed(red, green, blue, APP_WS2812_BREATHE_STEP, APP_WS2812_BREATHE_INTERVAL_MS);
}

uint8_t App_StartWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    return App_StartWs2812BreatheForHandle(s_ws2812SideHandle,
                                           &s_ws2812SideEffectState,
                                           red,
                                           green,
                                           blue,
                                           step,
                                           intervalMs);
}

uint8_t App_StartWs2812Rainbow(void)
{
    return App_StartWs2812RainbowTimed(APP_WS2812_RAINBOW_STEP, APP_WS2812_RAINBOW_INTERVAL_MS);
}

uint8_t App_StartWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    return App_StartWs2812RainbowForHandle(s_ws2812SideHandle, &s_ws2812SideEffectState, step, intervalMs);
}

void App_StopWs2812Effect(void)
{
    s_ws2812SideEffectState.isBreathing = 0U;
    s_ws2812SideEffectState.isRainbow = 0U;
}

uint8_t App_SetBottomWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    App_StopBottomWs2812Effect();

    return App_ApplyWs2812Rgb(s_ws2812BottomHandle, red, green, blue);
}

uint8_t App_SetBottomWs2812LedCount(uint16_t ledCount)
{
    if (s_ws2812BottomHandle == NULL) {
        return 0U;
    }

    if (WS2812_SetLedCount(s_ws2812BottomHandle, ledCount) != WS2812_OK) {
        return 0U;
    }

    return App_ApplyWs2812Rgb(s_ws2812BottomHandle, 0U, 0U, 0U);
}

uint16_t App_GetBottomWs2812LedCount(void)
{
    return (s_ws2812BottomHandle == NULL) ? 0U : WS2812_GetLedCount(s_ws2812BottomHandle);
}

uint16_t App_GetBottomWs2812MaxLedCount(void)
{
    return (s_ws2812BottomHandle == NULL) ? 0U : WS2812_GetMaxLedCount(s_ws2812BottomHandle);
}

uint8_t App_StartBottomWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    return App_StartBottomWs2812BreatheTimed(red, green, blue, APP_WS2812_BREATHE_STEP, APP_WS2812_BREATHE_INTERVAL_MS);
}

uint8_t App_StartBottomWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    return App_StartWs2812BreatheForHandle(s_ws2812BottomHandle,
                                           &s_ws2812BottomEffectState,
                                           red,
                                           green,
                                           blue,
                                           step,
                                           intervalMs);
}

uint8_t App_StartBottomWs2812Rainbow(void)
{
    return App_StartBottomWs2812RainbowTimed(APP_WS2812_RAINBOW_STEP, APP_WS2812_RAINBOW_INTERVAL_MS);
}

uint8_t App_StartBottomWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    return App_StartWs2812RainbowForHandle(s_ws2812BottomHandle, &s_ws2812BottomEffectState, step, intervalMs);
}

void App_StopBottomWs2812Effect(void)
{
    s_ws2812BottomEffectState.isBreathing = 0U;
    s_ws2812BottomEffectState.isRainbow = 0U;
}

uint8_t App_RunBottomLightTest(void)
{
    return App_RunWs2812ColorTest(s_ws2812BottomHandle, &s_ws2812BottomEffectState);
}

static uint8_t App_RunWs2812ColorTest(WS2812_HandleTypeDef ws2812, App_Ws2812EffectStateTypeDef *effectState)
{
    static const uint8_t testColors[][3] = {
        {255U, 0U, 0U},
        {0U, 255U, 0U},
        {0U, 0U, 255U},
        {255U, 255U, 255U},
        {0U, 0U, 0U}
    };
    uint8_t index;

    if ((ws2812 == NULL) || (effectState == NULL)) {
        return 0U;
    }

    effectState->isBreathing = 0U;
    effectState->isRainbow = 0U;

    for (index = 0U; index < (uint8_t)(sizeof(testColors) / sizeof(testColors[0])); index++) {
        if (App_ApplyWs2812Rgb(ws2812, testColors[index][0], testColors[index][1], testColors[index][2]) == 0U) {
            return 0U;
        }
        Platform_Time_DelayMs(APP_WS2812_TEST_STEP_DELAY_MS);
    }

    return 1U;
}

#endif

uint8_t App_SetPower5V(uint8_t enabled, uint8_t sourceTag)
{
    (void)sourceTag;

    if (enabled != 0U) {
        IP5306_Key_ShortPress();
        App_RunPower5VServoProbe();
    } else {
        IP5306_Key_DoubleShortPress();
    }

    return 1U;
}

static void App_RunPower5VServoProbe(void)
{
#if (APP_POWER5V_SERVO_PROBE_ENABLE != 0U)
    uint16_t servo1PulseUs = 0U;
    uint16_t servo2PulseUs = 0U;
    uint8_t servo1Ok;
    uint8_t servo2Ok;

    Platform_Time_DelayMs(APP_POWER5V_SERVO_PROBE_DELAY_MS);

    servo1Ok = App_SetServoAngle(1U, APP_POWER5V_SERVO1_PROBE_ANGLE, &servo1PulseUs);
    servo2Ok = App_SetServoAngle(2U, APP_POWER5V_SERVO2_PROBE_ANGLE, &servo2PulseUs);

    Platform_Uart_Print("========== POWER5V SERVO PROBE ==========\r\n");
    Platform_Uart_Printf("  Delay  : %lu ms after POWER enable\r\n", (uint32_t)APP_POWER5V_SERVO_PROBE_DELAY_MS);
    Platform_Uart_Printf("  Servo1 : %s angle=%lu deg pulse=%u us\r\n",
                         (servo1Ok != 0U) ? "OK" : "FAIL",
                         (uint32_t)APP_POWER5V_SERVO1_PROBE_ANGLE,
                         servo1PulseUs);
    Platform_Uart_Printf("  Servo2 : %s angle=%lu deg pulse=%u us\r\n",
                         (servo2Ok != 0U) ? "OK" : "FAIL",
                         (uint32_t)APP_POWER5V_SERVO2_PROBE_ANGLE,
                         servo2PulseUs);
    Platform_Uart_Print("=========================================\r\n");
#endif
}

static Servo_HandleTypeDef App_GetServoHandle(uint8_t servoIndex)
{
    if (servoIndex == 1U) {
        return s_servo1Handle;
    }

    if (servoIndex == 2U) {
        return s_servo2Handle;
    }

    return NULL;
}

static uint8_t App_ClampServoAngle(uint8_t servoIndex, uint8_t angle)
{
    App_ServoLimitTypeDef limit;

    if ((servoIndex < 1U) || (servoIndex > 2U)) {
        return angle;
    }

    limit = s_servoLimits[servoIndex - 1U];
    if (angle < limit.minAngle) {
        return limit.minAngle;
    }
    if (angle > limit.maxAngle) {
        return limit.maxAngle;
    }
    return angle;
}

static uint8_t App_PulseToAngle(Servo_HandleTypeDef servoHandle, uint16_t pulseUs, uint8_t *angle)
{
    Servo_StateTypeDef state;
    uint16_t pulseRange;

    if ((servoHandle == NULL) || (angle == NULL) || (Servo_GetState(servoHandle, &state) != SERVO_OK)) {
        return 0U;
    }

    if (pulseUs <= s_servo1Config.pulseMin) {
        *angle = 0U;
        return 1U;
    }
    if (pulseUs >= s_servo1Config.pulseMax) {
        *angle = 180U;
        return 1U;
    }

    (void)state;
    pulseRange = (uint16_t)(s_servo1Config.pulseMax - s_servo1Config.pulseMin);
    *angle = (uint8_t)(((uint32_t)(pulseUs - s_servo1Config.pulseMin) * 180U + (pulseRange / 2U)) / pulseRange);
    return 1U;
}

#if (APP_LED_ENABLE != 0U)
static uint8_t App_ApplyWs2812Rgb(WS2812_HandleTypeDef ws2812, uint8_t red, uint8_t green, uint8_t blue)
{
    if (ws2812 == NULL) {
        return 0U;
    }

    if (WS2812_Fill(ws2812, red, green, blue) != WS2812_OK) {
        return 0U;
    }

    return (WS2812_Show(ws2812, APP_WS2812_SHOW_TIMEOUT_MS) == WS2812_OK) ? 1U : 0U;
}

static uint8_t App_ApplyWs2812Rainbow(WS2812_HandleTypeDef ws2812, uint8_t offset)
{
    uint16_t ledCount;
    uint16_t ledIndex;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t position;

    if (ws2812 == NULL) {
        return 0U;
    }

    ledCount = WS2812_GetLedCount(ws2812);
    if (ledCount == 0U) {
        return 0U;
    }

    for (ledIndex = 0U; ledIndex < ledCount; ledIndex++) {
        position = (uint8_t)((((uint32_t)ledIndex * 256U) / ledCount + offset) & 0xFFU);
        App_GetRainbowColor(position, &red, &green, &blue);
        if (WS2812_SetPixel(ws2812, ledIndex, red, green, blue) != WS2812_OK) {
            return 0U;
        }
    }

    return (WS2812_Show(ws2812, APP_WS2812_SHOW_TIMEOUT_MS) == WS2812_OK) ? 1U : 0U;
}

static void App_UpdateWs2812Effect(WS2812_HandleTypeDef ws2812, App_Ws2812EffectStateTypeDef *effectState)
{
    uint32_t nowTickMs;
    uint16_t nextBrightness;
    uint8_t intervalMs;

    if ((ws2812 == NULL) || (effectState == NULL) ||
        ((effectState->isBreathing == 0U) && (effectState->isRainbow == 0U))) {
        return;
    }

    nowTickMs = Platform_Time_GetTickMs();
    intervalMs = (effectState->isRainbow != 0U) ? effectState->rainbowIntervalMs : effectState->intervalMs;
    if ((nowTickMs - effectState->lastUpdateTickMs) < intervalMs) {
        return;
    }

    effectState->lastUpdateTickMs = nowTickMs;

    if (effectState->isRainbow != 0U) {
        effectState->rainbowOffset = (uint8_t)(effectState->rainbowOffset + effectState->rainbowStep);
        (void)App_ApplyWs2812Rainbow(ws2812, effectState->rainbowOffset);
        return;
    }

    if (effectState->isRising != 0U) {
        nextBrightness = (uint16_t)effectState->brightness + effectState->step;
        if (nextBrightness >= 255U) {
            effectState->brightness = 255U;
            effectState->isRising = 0U;
        } else {
            effectState->brightness = (uint8_t)nextBrightness;
        }
    } else {
        if (effectState->brightness <= effectState->step) {
            effectState->brightness = 0U;
            effectState->isRising = 1U;
        } else {
            effectState->brightness = (uint8_t)(effectState->brightness - effectState->step);
        }
    }

    (void)App_ApplyWs2812Rgb(ws2812,
                             App_ScaleWs2812Channel(effectState->baseRed, effectState->brightness),
                             App_ScaleWs2812Channel(effectState->baseGreen, effectState->brightness),
                             App_ScaleWs2812Channel(effectState->baseBlue, effectState->brightness));
}

static uint8_t App_StartWs2812BreatheForHandle(WS2812_HandleTypeDef ws2812,
                                               App_Ws2812EffectStateTypeDef *effectState,
                                               uint8_t red,
                                               uint8_t green,
                                               uint8_t blue,
                                               uint8_t step,
                                               uint8_t intervalMs)
{
    if ((ws2812 == NULL) || (effectState == NULL)) {
        return 0U;
    }

    if ((step == 0U) || (intervalMs == 0U)) {
        return 0U;
    }

    effectState->isBreathing = 1U;
    effectState->isRainbow = 0U;
    effectState->baseRed = red;
    effectState->baseGreen = green;
    effectState->baseBlue = blue;
    effectState->brightness = 0U;
    effectState->isRising = 1U;
    effectState->step = step;
    effectState->intervalMs = intervalMs;
    effectState->lastUpdateTickMs = 0U;

    return App_ApplyWs2812Rgb(ws2812, 0U, 0U, 0U);
}

static uint8_t App_StartWs2812RainbowForHandle(WS2812_HandleTypeDef ws2812,
                                               App_Ws2812EffectStateTypeDef *effectState,
                                               uint8_t step,
                                               uint8_t intervalMs)
{
    if ((ws2812 == NULL) || (effectState == NULL)) {
        return 0U;
    }

    if ((step == 0U) || (intervalMs == 0U)) {
        return 0U;
    }

    effectState->isBreathing = 0U;
    effectState->isRainbow = 1U;
    effectState->rainbowOffset = 0U;
    effectState->rainbowStep = step;
    effectState->rainbowIntervalMs = intervalMs;
    effectState->lastUpdateTickMs = 0U;

    return App_ApplyWs2812Rainbow(ws2812, effectState->rainbowOffset);
}

static uint8_t App_ScaleWs2812Channel(uint8_t channel, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)channel * brightness) / 255U);
}

static void App_GetRainbowColor(uint8_t position, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    uint8_t distance;
    uint8_t inverseDistance;

    if ((red == NULL) || (green == NULL) || (blue == NULL)) {
        return;
    }

    distance = (position <= 127U) ? position : (uint8_t)(255U - position);

    if (distance < 8U) {
        inverseDistance = (uint8_t)(8U - distance);
        *red = (uint8_t)(20U + (inverseDistance * 8U));
        *green = (uint8_t)(160U + (inverseDistance * 11U));
        *blue = 255U;
    } else if (distance < 28U) {
        inverseDistance = (uint8_t)(28U - distance);
        *red = (uint8_t)(inverseDistance / 2U);
        *green = (uint8_t)(32U + (inverseDistance * 4U));
        *blue = (uint8_t)(96U + (inverseDistance * 5U));
    } else if (distance < 62U) {
        inverseDistance = (uint8_t)(62U - distance);
        *red = (uint8_t)(8U + (inverseDistance / 2U));
        *green = 0U;
        *blue = (uint8_t)(28U + inverseDistance);
    } else {
        *red = 0U;
        *green = 0U;
        *blue = 6U;
    }
}
#else
uint8_t App_SetWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_SetWs2812LedCount(uint16_t ledCount)
{
    (void)ledCount;
    return 0U;
}

uint16_t App_GetWs2812LedCount(void)
{
    return 0U;
}

uint16_t App_GetWs2812MaxLedCount(void)
{
    return 0U;
}

uint8_t App_StartWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_StartWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 0U;
}

uint8_t App_StartWs2812Rainbow(void)
{
    return 0U;
}

uint8_t App_StartWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    return 0U;
}

void App_StopWs2812Effect(void)
{
}

uint8_t App_SetBottomWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_SetBottomWs2812LedCount(uint16_t ledCount)
{
    (void)ledCount;
    return 0U;
}

uint16_t App_GetBottomWs2812LedCount(void)
{
    return 0U;
}

uint16_t App_GetBottomWs2812MaxLedCount(void)
{
    return 0U;
}

uint8_t App_StartBottomWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_StartBottomWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 0U;
}

uint8_t App_StartBottomWs2812Rainbow(void)
{
    return 0U;
}

uint8_t App_StartBottomWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    return 0U;
}

void App_StopBottomWs2812Effect(void)
{
}

uint8_t App_RunBottomLightTest(void)
{
    return 0U;
}
#endif
#else
uint8_t App_SetServoAngle(uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs)
{
    (void)servoIndex;
    (void)angle;
    (void)pulseUs;
    return 0U;
}

uint8_t App_SetServoDegX10(uint8_t servoIndex, int16_t degX10, uint16_t *pulseUs)
{
    (void)servoIndex;
    (void)degX10;
    (void)pulseUs;
    return 0U;
}

uint8_t App_MoveServoToAngleOverTime(uint8_t servoIndex,
                                     uint8_t angle,
                                     uint16_t durationMs,
                                     Servo_MotionProfileTypeDef profile,
                                     uint16_t *targetPulseUs)
{
    (void)servoIndex;
    (void)angle;
    (void)durationMs;
    (void)profile;
    (void)targetPulseUs;
    return 0U;
}

uint8_t App_MoveServoToAngleOverTimeWithSteps(uint8_t servoIndex,
                                              uint8_t angle,
                                              uint16_t durationMs,
                                              uint16_t requestedSteps,
                                              Servo_MotionProfileTypeDef profile,
                                              uint16_t *targetPulseUs,
                                              uint16_t *effectiveUpdatePeriodMs)
{
    return App_MoveServoToAngleOverTimeWithStepsAndEaseStrength(servoIndex,
                                                                angle,
                                                                durationMs,
                                                                requestedSteps,
                                                                profile,
                                                                SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT,
                                                                targetPulseUs,
                                                                effectiveUpdatePeriodMs);
}

uint8_t App_MoveServoToAngleOverTimeWithStepsAndEaseStrength(uint8_t servoIndex,
                                                             uint8_t angle,
                                                             uint16_t durationMs,
                                                             uint16_t requestedSteps,
                                                             Servo_MotionProfileTypeDef profile,
                                                             uint8_t easeStrengthPercent,
                                                             uint16_t *targetPulseUs,
                                                             uint16_t *effectiveUpdatePeriodMs)
{
    (void)servoIndex;
    (void)angle;
    (void)durationMs;
    (void)requestedSteps;
    (void)profile;
    (void)easeStrengthPercent;
    (void)targetPulseUs;
    (void)effectiveUpdatePeriodMs;
    return 0U;
}

uint8_t App_StopServoMotion(uint8_t servoIndex)
{
    (void)servoIndex;
    return 0U;
}

uint8_t App_SetServoLimit(uint8_t servoIndex, uint8_t minAngle, uint8_t maxAngle)
{
    (void)servoIndex;
    (void)minAngle;
    (void)maxAngle;
    return 0U;
}

uint8_t App_GetServoLimit(uint8_t servoIndex, uint8_t *minAngle, uint8_t *maxAngle)
{
    (void)servoIndex;
    (void)minAngle;
    (void)maxAngle;
    return 0U;
}

uint8_t App_GetServoFeedbackRaw(uint8_t servoIndex, uint16_t *adcRaw)
{
    (void)servoIndex;
    (void)adcRaw;
    return 0U;
}

uint8_t App_GetServoCommandAngle(uint8_t servoIndex, uint8_t *angle)
{
    (void)servoIndex;
    (void)angle;
    return 0U;
}

uint8_t App_GetServoFeedbackSnapshot(uint8_t servoIndex, App_ServoFeedbackSnapshotTypeDef *snapshot)
{
    (void)servoIndex;
    (void)snapshot;
    return 0U;
}

uint16_t App_ConvertServoFeedbackToMv(uint16_t adcRaw)
{
    (void)adcRaw;
    return 0U;
}

uint8_t App_GetServoFeedbackAngle(uint8_t servoIndex, uint8_t *angle)
{
    (void)servoIndex;
    (void)angle;
    return 0U;
}

uint8_t App_ReadBottomIrTest(App_BottomIrTestSnapshotTypeDef *snapshot)
{
    (void)snapshot;
    return 0U;
}

uint8_t App_ReleaseServo(uint8_t servoIndex)
{
    (void)servoIndex;
    return 0U;
}

uint8_t App_StartServoReciprocating(uint8_t servoIndex)
{
    (void)servoIndex;
    return 0U;
}

uint8_t App_GetServoState(uint8_t servoIndex, Servo_StateTypeDef *state)
{
    (void)servoIndex;
    (void)state;
    return 0U;
}

uint8_t App_SetWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_SetWs2812LedCount(uint16_t ledCount)
{
    (void)ledCount;
    return 0U;
}

uint16_t App_GetWs2812LedCount(void)
{
    return 0U;
}

uint16_t App_GetWs2812MaxLedCount(void)
{
    return 0U;
}

uint8_t App_StartWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_StartWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 0U;
}

uint8_t App_StartWs2812Rainbow(void)
{
    return 0U;
}

uint8_t App_StartWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    return 0U;
}

void App_StopWs2812Effect(void)
{
}

uint8_t App_SetBottomWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_SetBottomWs2812LedCount(uint16_t ledCount)
{
    (void)ledCount;
    return 0U;
}

uint16_t App_GetBottomWs2812LedCount(void)
{
    return 0U;
}

uint16_t App_GetBottomWs2812MaxLedCount(void)
{
    return 0U;
}

uint8_t App_StartBottomWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 0U;
}

uint8_t App_StartBottomWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 0U;
}

uint8_t App_StartBottomWs2812Rainbow(void)
{
    return 0U;
}

uint8_t App_StartBottomWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    return 0U;
}

void App_StopBottomWs2812Effect(void)
{
}

uint8_t App_RunBottomLightTest(void)
{
    return 0U;
}

uint8_t App_SetPower5V(uint8_t enabled, uint8_t sourceTag)
{
    (void)enabled;
    (void)sourceTag;
    return 0U;
}
#endif
