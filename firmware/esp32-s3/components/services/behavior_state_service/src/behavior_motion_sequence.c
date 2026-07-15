#include "behavior_motion_sequence.h"

#include "hal_servo.h"

#include <limits.h>
#include <stddef.h>

static esp_err_t append_frame(hal_servo_trajectory_frame_t *frames, size_t *frame_count, int x_deg, int y_deg,
                              uint32_t duration_ms, hal_servo_motion_profile_t profile) {
    while (duration_ms > 0u) {
        uint16_t chunk_ms = duration_ms > UINT16_MAX ? UINT16_MAX : (uint16_t)duration_ms;
        hal_servo_trajectory_frame_t *frame;

        if (*frame_count >= HAL_SERVO_TRAJECTORY_MAX_FRAMES) {
            return ESP_ERR_INVALID_SIZE;
        }

        frame = &frames[*frame_count];
        frame->axis_mask = HAL_SERVO_AXIS_MASK_X | HAL_SERVO_AXIS_MASK_Y;
        frame->x_deg_x10 = (int16_t)(x_deg * 10);
        frame->y_deg_x10 = (int16_t)(y_deg * 10);
        frame->duration_ms = chunk_ms;
        frame->motion_profile = profile;
        (*frame_count)++;
        duration_ms -= chunk_ms;
    }

    return ESP_OK;
}

esp_err_t behavior_motion_sequence_play(const behavior_motion_event_t *events, int event_count) {
    hal_servo_trajectory_frame_t frames[HAL_SERVO_TRAJECTORY_MAX_FRAMES];
    size_t frame_count = 0u;
    uint32_t timeline_ms = 0u;
    int last_x_deg = 0;
    int last_y_deg = 0;

    if (events == NULL || event_count <= 0) {
        return event_count == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (events[0].at_ms != 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int index = 0; index < event_count; index++) {
        const behavior_motion_event_t *event = &events[index];

        if (event->x_deg < 0 || event->x_deg > 180 || event->y_deg < 0 || event->y_deg > 180 ||
            event->duration_ms <= 0 || event->duration_ms > UINT16_MAX ||
            event->at_ms > UINT32_MAX - (uint32_t)event->duration_ms ||
            (index > 0 && event->at_ms < events[index - 1].at_ms)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (int index = 0; index < event_count; index++) {
        const behavior_motion_event_t *event = &events[index];
        hal_servo_motion_profile_t profile;
        uint32_t effective_duration_ms = (uint32_t)event->duration_ms;
        esp_err_t ret;

        if (index + 1 < event_count && events[index + 1].at_ms < event->at_ms + effective_duration_ms) {
            effective_duration_ms = events[index + 1].at_ms - event->at_ms;
        }

        if (index > 0 && event->at_ms > timeline_ms) {
            ret = append_frame(frames, &frame_count, last_x_deg, last_y_deg, event->at_ms - timeline_ms,
                               HAL_SERVO_MOTION_PROFILE_LINEAR);
            if (ret != ESP_OK) {
                return ret;
            }
        }

        profile = event->motion_profile == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT ? HAL_SERVO_MOTION_PROFILE_EASE_IN_OUT
                                                                               : HAL_SERVO_MOTION_PROFILE_LINEAR;
        ret = append_frame(frames, &frame_count, event->x_deg, event->y_deg, effective_duration_ms, profile);
        if (ret != ESP_OK) {
            return ret;
        }

        last_x_deg = event->x_deg;
        last_y_deg = event->y_deg;
        timeline_ms = event->at_ms + effective_duration_ms;
    }

    return hal_servo_play_trajectory(frames, frame_count, HAL_SERVO_MOTION_SOURCE_BEHAVIOR);
}
