#include "behavior_motion_sequence.h"

#include "hal_servo.h"

#include <stdio.h>
#include <string.h>

static int s_failures;
static int s_play_count;
static size_t s_frame_count;
static hal_servo_motion_source_t s_source;
static esp_err_t s_play_result;
static hal_servo_trajectory_frame_t s_frames[HAL_SERVO_TRAJECTORY_MAX_FRAMES];

#define ASSERT_EQ(actual, expected)                                                                                    \
    do {                                                                                                               \
        long long actual_value = (long long)(actual);                                                                  \
        long long expected_value = (long long)(expected);                                                              \
        if (actual_value != expected_value) {                                                                          \
            fprintf(stderr, "ASSERT_EQ failed: got=%lld expected=%lld (%s:%d)\n", actual_value, expected_value,        \
                    __FILE__, __LINE__);                                                                               \
            s_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

esp_err_t hal_servo_play_trajectory(const hal_servo_trajectory_frame_t *frames, size_t frame_count,
                                    hal_servo_motion_source_t source) {
    s_play_count++;
    s_frame_count = frame_count;
    s_source = source;
    if (frame_count > HAL_SERVO_TRAJECTORY_MAX_FRAMES) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_frames, frames, frame_count * sizeof(*frames));
    return s_play_result;
}

static void reset_capture(void) {
    s_play_count = 0;
    s_frame_count = 0u;
    s_source = HAL_SERVO_MOTION_SOURCE_UNKNOWN;
    s_play_result = ESP_OK;
    memset(s_frames, 0, sizeof(s_frames));
}

static void test_sequence_preserves_timeline_holds_and_profiles(void) {
    const behavior_motion_event_t events[] = {
        {.at_ms = 0u, .x_deg = 90, .y_deg = 110, .duration_ms = 200, .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR},
        {.at_ms = 500u,
         .x_deg = 100,
         .y_deg = 120,
         .duration_ms = 100,
         .motion_profile = BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT},
    };

    reset_capture();
    ASSERT_EQ(behavior_motion_sequence_play(events, 2), ESP_OK);
    ASSERT_EQ(s_play_count, 1);
    ASSERT_EQ(s_frame_count, 3u);
    ASSERT_EQ(s_source, HAL_SERVO_MOTION_SOURCE_BEHAVIOR);

    ASSERT_EQ(s_frames[0].x_deg_x10, 900);
    ASSERT_EQ(s_frames[0].y_deg_x10, 1100);
    ASSERT_EQ(s_frames[0].duration_ms, 200u);
    ASSERT_EQ(s_frames[0].motion_profile, HAL_SERVO_MOTION_PROFILE_LINEAR);

    ASSERT_EQ(s_frames[1].x_deg_x10, 900);
    ASSERT_EQ(s_frames[1].y_deg_x10, 1100);
    ASSERT_EQ(s_frames[1].duration_ms, 300u);
    ASSERT_EQ(s_frames[1].motion_profile, HAL_SERVO_MOTION_PROFILE_LINEAR);

    ASSERT_EQ(s_frames[2].x_deg_x10, 1000);
    ASSERT_EQ(s_frames[2].y_deg_x10, 1200);
    ASSERT_EQ(s_frames[2].duration_ms, 100u);
    ASSERT_EQ(s_frames[2].motion_profile, HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT);
}

static void test_empty_sequence_is_a_noop(void) {
    reset_capture();
    ASSERT_EQ(behavior_motion_sequence_play(NULL, 0), ESP_OK);
    ASSERT_EQ(s_play_count, 0);
}

static void test_overlapping_event_truncates_previous_motion(void) {
    const behavior_motion_event_t events[] = {
        {.at_ms = 0u, .x_deg = 90, .y_deg = 110, .duration_ms = 1000, .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR},
        {.at_ms = 500u,
         .x_deg = 120,
         .y_deg = 130,
         .duration_ms = 100,
         .motion_profile = BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT},
    };

    reset_capture();
    ASSERT_EQ(behavior_motion_sequence_play(events, 2), ESP_OK);
    ASSERT_EQ(s_frame_count, 2u);
    ASSERT_EQ(s_frames[0].duration_ms, 500u);
    ASSERT_EQ(s_frames[1].x_deg_x10, 1200);
    ASSERT_EQ(s_frames[1].duration_ms, 100u);
}

static void test_non_monotonic_timestamps_are_rejected(void) {
    const behavior_motion_event_t events[] = {
        {.at_ms = 0u, .x_deg = 90, .y_deg = 110, .duration_ms = 100, .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR},
        {.at_ms = 200u,
         .x_deg = 100,
         .y_deg = 120,
         .duration_ms = 100,
         .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR},
        {.at_ms = 150u,
         .x_deg = 110,
         .y_deg = 130,
         .duration_ms = 100,
         .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR},
    };

    reset_capture();
    ASSERT_EQ(behavior_motion_sequence_play(events, 3), ESP_ERR_INVALID_ARG);
    ASSERT_EQ(s_play_count, 0);
}

static void test_servo_error_is_propagated(void) {
    const behavior_motion_event_t event = {
        .at_ms = 0u,
        .x_deg = 90,
        .y_deg = 110,
        .duration_ms = 100,
        .motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR,
    };

    reset_capture();
    s_play_result = ESP_FAIL;
    ASSERT_EQ(behavior_motion_sequence_play(&event, 1), ESP_FAIL);
}

int main(void) {
    test_sequence_preserves_timeline_holds_and_profiles();
    test_empty_sequence_is_a_noop();
    test_overlapping_event_truncates_previous_motion();
    test_non_monotonic_timestamps_are_rejected();
    test_servo_error_is_propagated();

    if (s_failures != 0) {
        fprintf(stderr, "behavior_motion_sequence_host_tests: %d failure(s)\n", s_failures);
        return 1;
    }
    puts("behavior_motion_sequence_host_tests: PASS");
    return 0;
}
