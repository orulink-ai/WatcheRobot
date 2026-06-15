#include "cli.h"

#include "app.h"
#include "bmi160.h"
#include "ip5306_irq.h"
#include "ip5306_key.h"
#include "platform_uart.h"
#include "qmc6309.h"
#include "sensors.h"
#include "touch_sensor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int s_testFailures;
static char s_uartOutput[8192];
static size_t s_uartOutputLength;
static uint8_t s_sideRgbCount;
static uint8_t s_bottomRgbCount;
static uint8_t s_sideRainbowCount;
static uint8_t s_bottomRainbowCount;
static uint8_t s_bottomCountSetCount;
static uint16_t s_lastBottomLedCount;
static uint8_t s_bottomLightTestCount;
static uint8_t s_bottomIrReadCount;
static uint8_t s_servoMoveCount;
static uint8_t s_servoStopCount;
static uint8_t s_servoLimitSetCount;
static uint8_t s_lastServoIndex;
static uint8_t s_lastServoAngle;
static uint16_t s_lastServoDurationMs;
static uint16_t s_lastServoRequestedSteps;
static uint16_t s_lastServoUpdatePeriodMs;
static uint8_t s_lastServoProfile;
static uint8_t s_lastServoEaseStrengthPercent;
static uint16_t s_lastServoMotionStartPulse;
static uint16_t s_lastServoMotionTargetPulse;
static uint8_t s_servoMinLimit[3] = {0U, 0U, 0U};
static uint8_t s_servoMaxLimit[3] = {0U, 180U, 180U};

#define ASSERT_TRUE(expr)                                                                                               \
    do {                                                                                                                \
        if (!(expr)) {                                                                                                  \
            fprintf(stderr, "ASSERT_TRUE failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                            \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

#define ASSERT_EQ_U32(actual, expected)                                                                                 \
    do {                                                                                                                \
        if ((uint32_t)(actual) != (uint32_t)(expected)) {                                                               \
            fprintf(stderr, "ASSERT_EQ_U32 failed: got=%lu expected=%lu (%s:%d)\n",                                    \
                    (unsigned long)(actual), (unsigned long)(expected), __FILE__, __LINE__);                           \
            s_testFailures++;                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

static void reset_capture(void)
{
    s_uartOutput[0] = '\0';
    s_uartOutputLength = 0U;
    s_sideRgbCount = 0U;
    s_bottomRgbCount = 0U;
    s_sideRainbowCount = 0U;
    s_bottomRainbowCount = 0U;
    s_bottomCountSetCount = 0U;
    s_lastBottomLedCount = 0U;
    s_bottomLightTestCount = 0U;
    s_bottomIrReadCount = 0U;
    s_servoMoveCount = 0U;
    s_servoStopCount = 0U;
    s_servoLimitSetCount = 0U;
    s_lastServoIndex = 0U;
    s_lastServoAngle = 0U;
    s_lastServoDurationMs = 0U;
    s_lastServoRequestedSteps = 0U;
    s_lastServoUpdatePeriodMs = 20U;
    s_lastServoProfile = SERVO_MOTION_PROFILE_LINEAR;
    s_lastServoEaseStrengthPercent = SERVO_MOTION_EASE_STRENGTH_DEFAULT_PERCENT;
    s_lastServoMotionStartPulse = 1500U;
    s_lastServoMotionTargetPulse = 1500U;
    s_servoMinLimit[1] = 0U;
    s_servoMaxLimit[1] = 180U;
    s_servoMinLimit[2] = 0U;
    s_servoMaxLimit[2] = 180U;
}

static void append_output(const char *text)
{
    size_t length;
    size_t available;

    if (text == NULL) {
        return;
    }

    length = strlen(text);
    available = sizeof(s_uartOutput) - s_uartOutputLength - 1U;
    if (length > available) {
        length = available;
    }

    memcpy(&s_uartOutput[s_uartOutputLength], text, length);
    s_uartOutputLength += length;
    s_uartOutput[s_uartOutputLength] = '\0';
}

static void run_test(void (*test_fn)(void), const char *name)
{
    int failuresBefore = s_testFailures;

    test_fn();
    if (s_testFailures == failuresBefore) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
    }
}

static void test_help_lists_bottom_light_test_commands(void)
{
    reset_capture();

    Cli_ExecuteCommand("help");

    ASSERT_TRUE(strstr(s_uartOutput, "ws red|green|blue|white|off") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "ws rainbow|cyber") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "ws_bottom red|green|blue|white|off") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "ws_bottom rainbow|cyber") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "ws_bottom test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "bottom_light_test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "bottom_ir_test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "bottom_ir") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "servo_move_time <id> <angle> <ms>") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "servo_time <id> <angle> <ms>") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "servo_stop [id]") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "servo_limit <id> <min> <max>") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "servo_status [id]") != NULL);
}

static void test_servo_time_command_starts_timed_motion(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_time 2 120 800 linear");

    ASSERT_EQ_U32(s_servoMoveCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 2U);
    ASSERT_EQ_U32(s_lastServoAngle, 120U);
    ASSERT_EQ_U32(s_lastServoDurationMs, 800U);
    ASSERT_EQ_U32(s_lastServoProfile, SERVO_MOTION_PROFILE_LINEAR);
    ASSERT_TRUE(strstr(s_uartOutput, "Servo Motion") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status  : MOVING") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Start pulse") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Source  : command pulse, us interpolation (no feedback)") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Use: servo <angle>") == NULL);
}

static void test_servo_move_time_command_accepts_steps_and_profile(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_move_time 1 120 800 40 linear");

    ASSERT_EQ_U32(s_servoMoveCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 1U);
    ASSERT_EQ_U32(s_lastServoAngle, 120U);
    ASSERT_EQ_U32(s_lastServoDurationMs, 800U);
    ASSERT_EQ_U32(s_lastServoRequestedSteps, 40U);
    ASSERT_EQ_U32(s_lastServoUpdatePeriodMs, 20U);
    ASSERT_EQ_U32(s_lastServoProfile, SERVO_MOTION_PROFILE_LINEAR);
    ASSERT_TRUE(strstr(s_uartOutput, "Command : servo_move_time") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Steps req: 40") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Update  : 20 ms") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Profile : linear") != NULL);
}

static void test_servo_move_time_command_accepts_ease_strength(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_move_time 1 120 800 40 ease 65");

    ASSERT_EQ_U32(s_servoMoveCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 1U);
    ASSERT_EQ_U32(s_lastServoAngle, 120U);
    ASSERT_EQ_U32(s_lastServoDurationMs, 800U);
    ASSERT_EQ_U32(s_lastServoRequestedSteps, 40U);
    ASSERT_EQ_U32(s_lastServoProfile, SERVO_MOTION_PROFILE_EASE_IN_OUT);
    ASSERT_EQ_U32(s_lastServoEaseStrengthPercent, 65U);
    ASSERT_TRUE(strstr(s_uartOutput, "Profile : ease") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Ease    : 65%") != NULL);
}

static void test_servo_time_command_accepts_profile_strength_without_steps(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_time 2 120 800 ease 40");

    ASSERT_EQ_U32(s_servoMoveCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 2U);
    ASSERT_EQ_U32(s_lastServoRequestedSteps, 0U);
    ASSERT_EQ_U32(s_lastServoProfile, SERVO_MOTION_PROFILE_EASE_IN_OUT);
    ASSERT_EQ_U32(s_lastServoEaseStrengthPercent, 40U);
}

static void test_servo_time_command_rejects_invalid_ease_strength(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_time 1 120 800 40 ease 101");

    ASSERT_EQ_U32(s_servoMoveCount, 0U);
    ASSERT_TRUE(strstr(s_uartOutput, "ease_strength 0~100") != NULL);
}

static void test_servo_time_command_accepts_anti_drop_profile(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_time 1 90 1600 anti_drop");

    ASSERT_EQ_U32(s_servoMoveCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 1U);
    ASSERT_EQ_U32(s_lastServoAngle, 90U);
    ASSERT_EQ_U32(s_lastServoDurationMs, 1600U);
    ASSERT_EQ_U32(s_lastServoProfile, SERVO_MOTION_PROFILE_ANTI_DROP);
    ASSERT_TRUE(strstr(s_uartOutput, "Profile : anti_drop") != NULL);
}

static void test_servo_stop_command_holds_current_pwm(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_stop 2");

    ASSERT_EQ_U32(s_servoStopCount, 1U);
    ASSERT_EQ_U32(s_lastServoIndex, 2U);
    ASSERT_TRUE(strstr(s_uartOutput, "Action: Stop and hold") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Signal: ACTIVE") != NULL);
}

static void test_servo_limit_command_updates_runtime_limit(void)
{
    reset_capture();

    Cli_ExecuteCommand("servo_limit 1 20 160");

    ASSERT_EQ_U32(s_servoLimitSetCount, 1U);
    ASSERT_EQ_U32(s_servoMinLimit[1], 20U);
    ASSERT_EQ_U32(s_servoMaxLimit[1], 160U);
    ASSERT_TRUE(strstr(s_uartOutput, "Limit : 20~160 deg") != NULL);

    Cli_ExecuteCommand("servo_limits");
    ASSERT_TRUE(strstr(s_uartOutput, "Servo1: 20~160 deg") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Servo2: 0~180 deg") != NULL);
}

static void test_ws_red_controls_side_light(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws red");

    ASSERT_EQ_U32(s_sideRgbCount, 1U);
    ASSERT_EQ_U32(s_bottomRgbCount, 0U);
    ASSERT_TRUE(strstr(s_uartOutput, "Side WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws red") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_bottom_red_controls_bottom_light(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws_bottom red");

    ASSERT_EQ_U32(s_sideRgbCount, 0U);
    ASSERT_EQ_U32(s_bottomRgbCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Bottom WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws_bottom red") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_bottom_test_runs_bottom_light_test(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws_bottom test");

    ASSERT_EQ_U32(s_bottomLightTestCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws_bottom test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "PB10 / TIM2_CH3") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_bottom_count_accepts_40_leds(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws_bottom count 40");

    ASSERT_EQ_U32(s_bottomCountSetCount, 1U);
    ASSERT_EQ_U32(s_lastBottomLedCount, 40U);
    ASSERT_TRUE(strstr(s_uartOutput, "Bottom WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws_bottom count 40") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_rainbow_controls_side_light(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws rainbow");

    ASSERT_EQ_U32(s_sideRainbowCount, 1U);
    ASSERT_EQ_U32(s_bottomRainbowCount, 0U);
    ASSERT_TRUE(strstr(s_uartOutput, "Side WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws rainbow") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_bottom_rainbow_controls_bottom_light(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws_bottom rainbow");

    ASSERT_EQ_U32(s_sideRainbowCount, 0U);
    ASSERT_EQ_U32(s_bottomRainbowCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Bottom WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws_bottom rainbow") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_ws_bottom_cyber_alias_controls_bottom_light(void)
{
    reset_capture();

    Cli_ExecuteCommand("ws_bottom cyber");

    ASSERT_EQ_U32(s_sideRainbowCount, 0U);
    ASSERT_EQ_U32(s_bottomRainbowCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Bottom WS2812") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: ws_bottom cyber") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_bottom_light_test_alias_runs_bottom_light_test(void)
{
    reset_capture();

    Cli_ExecuteCommand("bottom_light_test");

    ASSERT_EQ_U32(s_bottomLightTestCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Command: bottom_light_test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "PB10 / TIM2_CH3") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Status : OK") != NULL);
}

static void test_bottom_ir_test_reads_adc_threshold(void)
{
    reset_capture();

    Cli_ExecuteCommand("bottom_ir_test");

    ASSERT_EQ_U32(s_bottomIrReadCount, 1U);
    ASSERT_TRUE(strstr(s_uartOutput, "Bottom IR") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Command   : bottom_ir_test") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "PB0 / ADC1_IN8") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Raw       : 2300") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Threshold : 2048") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Compare   : ABOVE") != NULL);
    ASSERT_TRUE(strstr(s_uartOutput, "Active    : YES") != NULL);
}

int main(void)
{
    run_test(test_help_lists_bottom_light_test_commands, "help_lists_bottom_light_test_commands");
    run_test(test_servo_time_command_starts_timed_motion, "servo_time_command_starts_timed_motion");
    run_test(test_servo_move_time_command_accepts_steps_and_profile,
             "servo_move_time_command_accepts_steps_and_profile");
    run_test(test_servo_move_time_command_accepts_ease_strength,
             "servo_move_time_command_accepts_ease_strength");
    run_test(test_servo_time_command_accepts_profile_strength_without_steps,
             "servo_time_command_accepts_profile_strength_without_steps");
    run_test(test_servo_time_command_rejects_invalid_ease_strength,
             "servo_time_command_rejects_invalid_ease_strength");
    run_test(test_servo_time_command_accepts_anti_drop_profile, "servo_time_command_accepts_anti_drop_profile");
    run_test(test_servo_stop_command_holds_current_pwm, "servo_stop_command_holds_current_pwm");
    run_test(test_servo_limit_command_updates_runtime_limit, "servo_limit_command_updates_runtime_limit");
    run_test(test_ws_red_controls_side_light, "ws_red_controls_side_light");
    run_test(test_ws_bottom_red_controls_bottom_light, "ws_bottom_red_controls_bottom_light");
    run_test(test_ws_bottom_test_runs_bottom_light_test, "ws_bottom_test_runs_bottom_light_test");
    run_test(test_ws_bottom_count_accepts_40_leds, "ws_bottom_count_accepts_40_leds");
    run_test(test_ws_rainbow_controls_side_light, "ws_rainbow_controls_side_light");
    run_test(test_ws_bottom_rainbow_controls_bottom_light, "ws_bottom_rainbow_controls_bottom_light");
    run_test(test_ws_bottom_cyber_alias_controls_bottom_light, "ws_bottom_cyber_alias_controls_bottom_light");
    run_test(test_bottom_light_test_alias_runs_bottom_light_test, "bottom_light_test_alias_runs_bottom_light_test");
    run_test(test_bottom_ir_test_reads_adc_threshold, "bottom_ir_test_reads_adc_threshold");

    if (s_testFailures != 0) {
        fprintf(stderr, "%d test(s) failed\n", s_testFailures);
        return 1;
    }

    return 0;
}

void Platform_Uart_Print(const char *text)
{
    append_output(text);
}

void Platform_Uart_Printf(const char *format, ...)
{
    char message[512];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    append_output(message);
}

void Platform_Uart_Init(void)
{
}

uint8_t Platform_Uart_TryReadByte(uint8_t *byte)
{
    (void)byte;
    return 0U;
}

uint8_t App_SetServoAngle(uint8_t servoIndex, uint8_t angle, uint16_t *pulseUs)
{
    s_lastServoIndex = servoIndex;
    s_lastServoAngle = angle;
    if (pulseUs != NULL) {
        *pulseUs = 1500U;
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
    s_servoMoveCount++;
    s_lastServoIndex = servoIndex;
    s_lastServoAngle = angle;
    s_lastServoDurationMs = durationMs;
    s_lastServoRequestedSteps = requestedSteps;
    s_lastServoUpdatePeriodMs = 20U;
    if (requestedSteps != 0U) {
        uint16_t roundedPeriodMs = (uint16_t)(((uint32_t)durationMs + ((uint32_t)requestedSteps / 2U)) /
                                              (uint32_t)requestedSteps);
        s_lastServoUpdatePeriodMs = (roundedPeriodMs < 20U) ? 20U : roundedPeriodMs;
    }
    s_lastServoProfile = (uint8_t)profile;
    s_lastServoEaseStrengthPercent = easeStrengthPercent;
    s_lastServoMotionStartPulse = 1500U;
    s_lastServoMotionTargetPulse = (uint16_t)(500U + (((uint16_t)angle * 2000U) / 180U));
    if (targetPulseUs != NULL) {
        *targetPulseUs = s_lastServoMotionTargetPulse;
    }
    if (effectiveUpdatePeriodMs != NULL) {
        *effectiveUpdatePeriodMs = s_lastServoUpdatePeriodMs;
    }
    return 1U;
}

uint8_t App_StopServoMotion(uint8_t servoIndex)
{
    s_servoStopCount++;
    s_lastServoIndex = servoIndex;
    return 1U;
}

uint8_t App_SetServoLimit(uint8_t servoIndex, uint8_t minAngle, uint8_t maxAngle)
{
    if (servoIndex > 2U) {
        return 0U;
    }
    s_servoLimitSetCount++;
    s_servoMinLimit[servoIndex] = minAngle;
    s_servoMaxLimit[servoIndex] = maxAngle;
    return 1U;
}

uint8_t App_GetServoLimit(uint8_t servoIndex, uint8_t *minAngle, uint8_t *maxAngle)
{
    if ((servoIndex > 2U) || (minAngle == NULL) || (maxAngle == NULL)) {
        return 0U;
    }
    *minAngle = s_servoMinLimit[servoIndex];
    *maxAngle = s_servoMaxLimit[servoIndex];
    return 1U;
}

uint8_t App_GetServoCommandAngle(uint8_t servoIndex, uint8_t *angle)
{
    (void)servoIndex;
    if (angle != NULL) {
        *angle = 90U;
    }
    return 1U;
}

uint8_t App_GetServoFeedbackSnapshot(uint8_t servoIndex, App_ServoFeedbackSnapshotTypeDef *snapshot)
{
    (void)servoIndex;
    (void)snapshot;
    return 0U;
}

uint8_t App_GetServoFeedbackRaw(uint8_t servoIndex, uint16_t *adcRaw)
{
    (void)servoIndex;
    if (adcRaw != NULL) {
        *adcRaw = 0U;
    }
    return 1U;
}

uint16_t App_ConvertServoFeedbackToMv(uint16_t adcRaw)
{
    return adcRaw;
}

uint8_t App_GetServoFeedbackAngle(uint8_t servoIndex, uint8_t *angle)
{
    (void)servoIndex;
    if (angle != NULL) {
        *angle = 90U;
    }
    return 1U;
}

uint8_t App_ReadBottomIrTest(App_BottomIrTestSnapshotTypeDef *snapshot)
{
    if (snapshot == NULL) {
        return 0U;
    }

    snapshot->raw = 2300U;
    snapshot->millivolts = 1853U;
    snapshot->thresholdRaw = 2048U;
    snapshot->aboveThreshold = 1U;
    snapshot->active = 1U;
    s_bottomIrReadCount++;
    return 1U;
}

uint8_t App_ReleaseServo(uint8_t servoIndex)
{
    (void)servoIndex;
    return 1U;
}

uint8_t App_StartServoReciprocating(uint8_t servoIndex)
{
    (void)servoIndex;
    return 1U;
}

uint8_t App_GetServoState(uint8_t servoIndex, Servo_StateTypeDef *state)
{
    (void)servoIndex;
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->currentPulse = 1500U;
        state->targetPulse = s_lastServoMotionTargetPulse;
        state->motionStartPulse = s_lastServoMotionStartPulse;
        state->motionTargetPulse = s_lastServoMotionTargetPulse;
        state->motionDurationMs = s_lastServoDurationMs;
        state->motionUpdatePeriodMs = s_lastServoUpdatePeriodMs;
        state->motionProfile = s_lastServoProfile;
        state->motionEaseStrengthPercent = s_lastServoEaseStrengthPercent;
        state->isMotionActive = (s_servoMoveCount != 0U) ? 1U : 0U;
        state->isSignalActive = 1U;
    }
    return 1U;
}

uint8_t App_SetWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    s_sideRgbCount++;
    return 1U;
}

uint8_t App_SetWs2812LedCount(uint16_t ledCount)
{
    (void)ledCount;
    return 1U;
}

uint16_t App_GetWs2812LedCount(void)
{
    return 1U;
}

uint16_t App_GetWs2812MaxLedCount(void)
{
    return 16U;
}

uint8_t App_StartWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 1U;
}

uint8_t App_StartWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 1U;
}

uint8_t App_StartWs2812Rainbow(void)
{
    s_sideRainbowCount++;
    return 1U;
}

uint8_t App_StartWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    s_sideRainbowCount++;
    return 1U;
}

uint8_t App_RunBottomLightTest(void)
{
    s_bottomLightTestCount++;
    return 1U;
}

void App_StopWs2812Effect(void)
{
}

uint8_t App_SetBottomWs2812Rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    s_bottomRgbCount++;
    return 1U;
}

uint8_t App_SetBottomWs2812LedCount(uint16_t ledCount)
{
    s_bottomCountSetCount++;
    s_lastBottomLedCount = ledCount;
    return 1U;
}

uint16_t App_GetBottomWs2812LedCount(void)
{
    return 1U;
}

uint16_t App_GetBottomWs2812MaxLedCount(void)
{
    return 40U;
}

uint8_t App_StartBottomWs2812Breathe(uint8_t red, uint8_t green, uint8_t blue)
{
    (void)red;
    (void)green;
    (void)blue;
    return 1U;
}

uint8_t App_StartBottomWs2812BreatheTimed(uint8_t red, uint8_t green, uint8_t blue, uint8_t step, uint8_t intervalMs)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)step;
    (void)intervalMs;
    return 1U;
}

uint8_t App_StartBottomWs2812Rainbow(void)
{
    s_bottomRainbowCount++;
    return 1U;
}

uint8_t App_StartBottomWs2812RainbowTimed(uint8_t step, uint8_t intervalMs)
{
    (void)step;
    (void)intervalMs;
    s_bottomRainbowCount++;
    return 1U;
}

void App_StopBottomWs2812Effect(void)
{
}

uint8_t App_SetPower5V(uint8_t enabled, uint8_t sourceTag)
{
    (void)enabled;
    (void)sourceTag;
    return 1U;
}

void BMI160_Init(void)
{
}

void BMI160_ReadData(BMI160_DataTypeDef *data)
{
    if (data != NULL) {
        memset(data, 0, sizeof(*data));
    }
}

uint8_t QMC6309_Init(void)
{
    return 1U;
}

uint8_t QMC6309_ReadData(QMC6309_DataTypeDef *data)
{
    if (data != NULL) {
        memset(data, 0, sizeof(*data));
    }
    return 1U;
}

uint8_t QMC6309_ReadStatus(uint8_t *status)
{
    if (status != NULL) {
        *status = 0U;
    }
    return 1U;
}

uint8_t QMC6309_ReadDiagnostics(QMC6309_DiagnosticsTypeDef *diagnostics)
{
    if (diagnostics != NULL) {
        memset(diagnostics, 0, sizeof(*diagnostics));
    }
    return 1U;
}

void Sensors_Scan(void)
{
}

void Sensors_ScanAllI2cAddresses(void)
{
}

void Sensors_PrintI2cDiagnostics(void)
{
}

void Sensors_RecoverI2cBus(void)
{
}

void Sensors_TestI2cPins(void)
{
}

uint8_t TouchSensor_Read(void)
{
    return 1U;
}

void IP5306_Irq_Init(void)
{
}

uint8_t IP5306_Irq_Read(void)
{
    return 0U;
}

void IP5306_Irq_OnRisingEdge(void)
{
}

uint8_t IP5306_Irq_HasPendingEvent(void)
{
    return 0U;
}

uint32_t IP5306_Irq_GetEventCount(void)
{
    return 0U;
}

uint32_t IP5306_Irq_GetLastEventTickMs(void)
{
    return 0U;
}

void IP5306_Irq_ClearPendingEvent(void)
{
}

void IP5306_Key_Init(void)
{
}

void IP5306_Key_PressMs(uint32_t durationMs)
{
    (void)durationMs;
}

void IP5306_Key_ShortPress(void)
{
}

void IP5306_Key_LongPress(void)
{
}

void IP5306_Key_DoubleShortPress(void)
{
}

void IP5306_Key_TestHold(void)
{
}
