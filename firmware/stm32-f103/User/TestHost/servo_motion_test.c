#include "servo.h"

#include <stdio.h>
#include <string.h>

static int s_testFailures;
static uint32_t s_nowMs;
static uint16_t s_writtenPulses[32];
static size_t s_writtenPulseCount;

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

static void reset_capture(void)
{
    s_nowMs = 0U;
    memset(s_writtenPulses, 0, sizeof(s_writtenPulses));
    s_writtenPulseCount = 0U;
}

static uint16_t last_pulse(void)
{
    return (s_writtenPulseCount == 0U) ? 0U : s_writtenPulses[s_writtenPulseCount - 1U];
}

static Servo_HandleTypeDef create_servo(void)
{
    static TIM_HandleTypeDef tim;
    Servo_ConfigTypeDef config;

    memset(&config, 0, sizeof(config));
    config.pTimHandle = &tim;
    config.TimChannel = TIM_CHANNEL_1;
    config.pulseMin = 500U;
    config.pulseMax = 2500U;
    config.pulseCenter = 1500U;
    config.commandPulseCount = 0U;
    config.stepSize = 10U;
    config.stepDelayMs = 20U;

    return Servo_Init(&config);
}

static void test_set_angle_is_immediate_position_mode(void)
{
    Servo_HandleTypeDef servo;
    uint16_t pulse = 0U;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);
    ASSERT_EQ_U32(last_pulse(), 1500U);

    ASSERT_EQ_U32(Servo_SetAngle(servo, 180U), SERVO_OK);
    ASSERT_EQ_U32(Servo_GetPulse(servo, &pulse), SERVO_OK);
    ASSERT_EQ_U32(pulse, 2500U);
    ASSERT_EQ_U32(last_pulse(), 2500U);
    ASSERT_EQ_U32(Servo_IsMotionActive(servo), 0U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_move_to_time_linear_reaches_target_on_deadline(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 180U, 100U, SERVO_MOTION_PROFILE_LINEAR), SERVO_OK);
    ASSERT_EQ_U32(Servo_IsMotionActive(servo), 1U);

    s_nowMs = 50U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2000U);

    s_nowMs = 100U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2500U);
    ASSERT_EQ_U32(Servo_IsMotionActive(servo), 0U);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.currentPulse, 2500U);
    ASSERT_EQ_U32(state.targetPulse, 2500U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_move_to_time_with_update_period_throttles_intermediate_writes(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTimeWithUpdatePeriod(servo,
                                                            180U,
                                                            100U,
                                                            SERVO_MOTION_PROFILE_LINEAR,
                                                            50U),
                  SERVO_OK);

    s_nowMs = 25U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1500U);

    s_nowMs = 50U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2000U);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.motionUpdatePeriodMs, 50U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_steps_to_update_period_respects_pwm_frame_floor(void)
{
    ASSERT_EQ_U32(Servo_MotionUpdatePeriodFromSteps(800U, 40U), 20U);
    ASSERT_EQ_U32(Servo_MotionUpdatePeriodFromSteps(800U, 200U), 20U);
    ASSERT_EQ_U32(Servo_MotionUpdatePeriodFromSteps(800U, 10U), 80U);
    ASSERT_EQ_U32(Servo_MotionUpdatePeriodFromSteps(0U, 40U), SERVO_MOTION_UPDATE_PERIOD_MS);
}

static void test_move_to_time_ease_profile_smooths_start_and_stop(void)
{
    Servo_HandleTypeDef servo;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 180U, 100U, SERVO_MOTION_PROFILE_EASE_IN_OUT), SERVO_OK);

    s_nowMs = 25U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1656U);

    s_nowMs = 75U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2344U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_move_to_time_ease_strength_blends_linear_and_smoothstep(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(servo,
                                                                           180U,
                                                                           100U,
                                                                           SERVO_MOTION_PROFILE_EASE_IN_OUT,
                                                                           20U,
                                                                           50U),
                  SERVO_OK);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.motionEaseStrengthPercent, 50U);

    s_nowMs = 25U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1703U);

    s_nowMs = 75U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2297U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);
    ASSERT_EQ_U32(Servo_MoveToAngleOverTimeWithUpdatePeriodAndEaseStrength(servo,
                                                                           180U,
                                                                           100U,
                                                                           SERVO_MOTION_PROFILE_EASE_IN_OUT,
                                                                           20U,
                                                                           0U),
                  SERVO_OK);

    s_nowMs = 25U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1750U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_move_to_time_anti_drop_preloads_before_descent(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 45U, 1000U, SERVO_MOTION_PROFILE_ANTI_DROP), SERVO_OK);

    s_nowMs = 80U;
    Servo_Update();
    ASSERT_TRUE(last_pulse() > 1500U);

    s_nowMs = 140U;
    Servo_Update();
    ASSERT_TRUE(last_pulse() > 1500U);

    s_nowMs = 600U;
    Servo_Update();
    ASSERT_TRUE(last_pulse() < 1500U);

    s_nowMs = 1000U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1000U);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.motionProfile, SERVO_MOTION_PROFILE_ANTI_DROP);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_timed_motion_uses_last_commanded_position_without_feedback(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_SetAngle(servo, 45U), SERVO_OK);
    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 135U, 100U, SERVO_MOTION_PROFILE_LINEAR), SERVO_OK);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.motionStartPulse, 1000U);
    ASSERT_EQ_U32(state.motionTargetPulse, 2000U);

    s_nowMs = 50U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 1500U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_stop_motion_holds_current_estimated_position(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 180U, 100U, SERVO_MOTION_PROFILE_LINEAR), SERVO_OK);
    s_nowMs = 50U;
    ASSERT_EQ_U32(Servo_StopMotion(servo), SERVO_OK);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.currentPulse, 2000U);
    ASSERT_EQ_U32(state.targetPulse, 2000U);
    ASSERT_EQ_U32(state.isMotionActive, 0U);

    s_nowMs = 100U;
    Servo_Update();
    ASSERT_EQ_U32(last_pulse(), 2000U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

static void test_immediate_set_cancels_active_motion(void)
{
    Servo_HandleTypeDef servo;
    Servo_StateTypeDef state;

    reset_capture();
    servo = create_servo();
    ASSERT_TRUE(servo != NULL);

    ASSERT_EQ_U32(Servo_MoveToAngleOverTime(servo, 180U, 100U, SERVO_MOTION_PROFILE_LINEAR), SERVO_OK);
    ASSERT_EQ_U32(Servo_SetAngle(servo, 45U), SERVO_OK);
    ASSERT_EQ_U32(Servo_GetState(servo, &state), SERVO_OK);
    ASSERT_EQ_U32(state.currentPulse, 1000U);
    ASSERT_EQ_U32(state.targetPulse, 1000U);
    ASSERT_EQ_U32(state.isMotionActive, 0U);
    ASSERT_EQ_U32(Servo_DeInit(servo), SERVO_OK);
}

int main(void)
{
    run_test(test_set_angle_is_immediate_position_mode, "set_angle_is_immediate_position_mode");
    run_test(test_move_to_time_linear_reaches_target_on_deadline, "move_to_time_linear_reaches_target_on_deadline");
    run_test(test_move_to_time_with_update_period_throttles_intermediate_writes,
             "move_to_time_with_update_period_throttles_intermediate_writes");
    run_test(test_steps_to_update_period_respects_pwm_frame_floor, "steps_to_update_period_respects_pwm_frame_floor");
    run_test(test_move_to_time_ease_profile_smooths_start_and_stop, "move_to_time_ease_profile_smooths_start_and_stop");
    run_test(test_move_to_time_ease_strength_blends_linear_and_smoothstep,
             "move_to_time_ease_strength_blends_linear_and_smoothstep");
    run_test(test_move_to_time_anti_drop_preloads_before_descent,
             "move_to_time_anti_drop_preloads_before_descent");
    run_test(test_timed_motion_uses_last_commanded_position_without_feedback,
             "timed_motion_uses_last_commanded_position_without_feedback");
    run_test(test_stop_motion_holds_current_estimated_position, "stop_motion_holds_current_estimated_position");
    run_test(test_immediate_set_cancels_active_motion, "immediate_set_cancels_active_motion");

    if (s_testFailures != 0) {
        fprintf(stderr, "servo_motion_tests: %d failure(s)\n", s_testFailures);
        return 1;
    }

    printf("servo_motion_tests: all checks passed\n");
    return 0;
}

HAL_StatusTypeDef Platform_Pwm_Start(TIM_HandleTypeDef *timHandle, uint32_t channel)
{
    (void)timHandle;
    (void)channel;
    return HAL_OK;
}

HAL_StatusTypeDef Platform_Pwm_WritePulse(TIM_HandleTypeDef *timHandle, uint32_t channel, uint16_t pulse)
{
    (void)timHandle;
    (void)channel;
    if (s_writtenPulseCount < (sizeof(s_writtenPulses) / sizeof(s_writtenPulses[0]))) {
        s_writtenPulses[s_writtenPulseCount++] = pulse;
    }
    return HAL_OK;
}

void Platform_Time_DelayMs(uint32_t delayMs)
{
    s_nowMs += delayMs;
}

uint32_t Platform_Time_GetTickMs(void)
{
    return s_nowMs;
}

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout)
{
    (void)hadc;
    (void)timeout;
    return HAL_OK;
}

uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return 0U;
}

HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    return HAL_OK;
}
