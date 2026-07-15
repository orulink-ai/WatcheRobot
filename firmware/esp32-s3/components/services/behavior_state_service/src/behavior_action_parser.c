#include "behavior_action_parser.h"
#include "behavior_memory.h"

#include "cJSON.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int frame_number;
    int angle_deg;
} behavior_action_keyframe_t;

static void parser_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int parser_strcasecmp(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return left == right ? 0 : 1;
    }

    while (*left != '\0' && *right != '\0') {
        int l = tolower((unsigned char)*left);
        int r = tolower((unsigned char)*right);
        if (l != r) {
            return l - r;
        }
        left++;
        right++;
    }

    return tolower((unsigned char)*left) - tolower((unsigned char)*right);
}

static const char *parser_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item != NULL && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

static int parser_get_int(cJSON *obj, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item != NULL && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_value;
}

static int compare_keyframes(const void *lhs, const void *rhs) {
    const behavior_action_keyframe_t *left = (const behavior_action_keyframe_t *)lhs;
    const behavior_action_keyframe_t *right = (const behavior_action_keyframe_t *)rhs;

    if (left->frame_number < right->frame_number) {
        return -1;
    }
    if (left->frame_number > right->frame_number) {
        return 1;
    }
    return 0;
}

static int compare_ints(const void *lhs, const void *rhs) {
    const int left = *(const int *)lhs;
    const int right = *(const int *)rhs;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static int dedup_keyframes(behavior_action_keyframe_t *frames, int count) {
    int read_index;
    int write_index = 0;

    if (frames == NULL || count <= 0) {
        return 0;
    }

    qsort(frames, (size_t)count, sizeof(behavior_action_keyframe_t), compare_keyframes);
    for (read_index = 0; read_index < count; ++read_index) {
        if (write_index > 0 && frames[write_index - 1].frame_number == frames[read_index].frame_number) {
            frames[write_index - 1] = frames[read_index];
        } else {
            frames[write_index++] = frames[read_index];
        }
    }

    return write_index;
}

static bool append_keyframe(behavior_action_keyframe_t **frames, int *count, int *capacity, int frame_number,
                            int angle_deg) {
    behavior_action_keyframe_t *new_frames = NULL;

    if (frames == NULL || count == NULL || capacity == NULL) {
        return false;
    }

    if (*count >= *capacity) {
        int new_capacity = (*capacity == 0) ? 8 : (*capacity * 2);
        new_frames =
            (behavior_action_keyframe_t *)realloc(*frames, (size_t)new_capacity * sizeof(behavior_action_keyframe_t));
        if (new_frames == NULL) {
            return false;
        }
        *frames = new_frames;
        *capacity = new_capacity;
    }

    (*frames)[*count].frame_number = frame_number;
    (*frames)[*count].angle_deg = angle_deg;
    (*count)++;
    return true;
}

static int find_angle_for_frame(const behavior_action_keyframe_t *frames, int count, int frame_number, int fallback) {
    int index;
    int angle = fallback;

    if (frames == NULL || count <= 0) {
        return fallback;
    }

    angle = frames[0].angle_deg;
    for (index = 0; index < count; ++index) {
        if (frames[index].frame_number > frame_number) {
            break;
        }
        angle = frames[index].angle_deg;
    }

    return angle;
}

static uint32_t frame_to_ms(int frame_number, int start_frame, int fps) {
    int relative_frame = frame_number - start_frame;

    if (fps <= 0 || relative_frame <= 0) {
        return 0;
    }

    return (uint32_t)(((int64_t)relative_frame * 1000 + (fps / 2)) / fps);
}

static bool append_frame_number(int **frames, int *count, int *capacity, int frame_number) {
    int *new_frames = NULL;

    if (frames == NULL || count == NULL || capacity == NULL) {
        return false;
    }

    if (*count >= *capacity) {
        int new_capacity = (*capacity == 0) ? 8 : (*capacity * 2);
        new_frames = (int *)realloc(*frames, (size_t)new_capacity * sizeof(int));
        if (new_frames == NULL) {
            return false;
        }
        *frames = new_frames;
        *capacity = new_capacity;
    }

    (*frames)[*count] = frame_number;
    (*count)++;
    return true;
}

static int dedup_frame_numbers(int *frames, int count) {
    int read_index;
    int write_index = 0;

    if (frames == NULL || count <= 0) {
        return 0;
    }

    qsort(frames, (size_t)count, sizeof(int), compare_ints);
    for (read_index = 0; read_index < count; ++read_index) {
        if (write_index == 0 || frames[write_index - 1] != frames[read_index]) {
            frames[write_index++] = frames[read_index];
        }
    }

    return write_index;
}

static bool is_valid_servo_angle(int angle_deg) {
    return angle_deg >= BEHAVIOR_SERVO_LOGICAL_MIN_DEG && angle_deg <= BEHAVIOR_SERVO_LOGICAL_MAX_DEG;
}

static int clamp_servo_y_angle(int angle_deg) {
    if (angle_deg < BEHAVIOR_SERVO_Y_MIN_DEG) {
        return BEHAVIOR_SERVO_Y_MIN_DEG;
    }
    if (angle_deg > BEHAVIOR_SERVO_Y_MAX_DEG) {
        return BEHAVIOR_SERVO_Y_MAX_DEG;
    }
    return angle_deg;
}

esp_err_t behavior_action_parse_json(const char *action_id, const char *json, size_t json_len,
                                     behavior_action_def_t *out_action) {
    cJSON *root = NULL;
    cJSON *animated_objects = NULL;
    cJSON *object = NULL;
    behavior_action_keyframe_t *x_frames = NULL;
    behavior_action_keyframe_t *y_frames = NULL;
    behavior_motion_event_t *events = NULL;
    int *frame_numbers = NULL;
    int x_count = 0;
    int x_capacity = 0;
    int y_count = 0;
    int y_capacity = 0;
    int frame_count = 0;
    int frame_capacity = 0;
    int fps;
    int frame_start;
    int frame_end;
    int effective_start;
    int effective_end;
    int effective_end_boundary;
    int max_keyframe = 0;
    int event_count = 0;
    int index;
    int last_x = BEHAVIOR_ACTION_DEFAULT_X_DEG;
    int last_y = BEHAVIOR_ACTION_DEFAULT_Y_DEG;
    esp_err_t ret = ESP_OK;

    if (action_id == NULL || action_id[0] == '\0' || json == NULL || out_action == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_action, 0, sizeof(*out_action));
    parser_copy_string(out_action->id, sizeof(out_action->id), action_id);

    root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        return ESP_FAIL;
    }

    fps = parser_get_int(root, "fps", 0);
    frame_start = parser_get_int(root, "frame_start", 0);
    frame_end = parser_get_int(root, "frame_end", frame_start);
    animated_objects = cJSON_GetObjectItem(root, "animated_objects");
    if (fps <= 0 || animated_objects == NULL || !cJSON_IsArray(animated_objects)) {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    cJSON_ArrayForEach(object, animated_objects) {
        cJSON *keyframe_data = NULL;
        cJSON *keyframe = NULL;

        if (!cJSON_IsObject(object)) {
            continue;
        }

        keyframe_data = cJSON_GetObjectItem(object, "keyframe_data");
        if (keyframe_data == NULL || !cJSON_IsArray(keyframe_data)) {
            continue;
        }

        cJSON_ArrayForEach(keyframe, keyframe_data) {
            const char *axis_name;
            cJSON *rotation_item = NULL;
            int frame_number;
            int angle_deg;

            if (!cJSON_IsObject(keyframe)) {
                continue;
            }

            axis_name = parser_get_string(keyframe, "active_axis");
            rotation_item = cJSON_GetObjectItem(keyframe, "rotation_angle");
            frame_number = parser_get_int(keyframe, "frame_number", frame_start);
            if (axis_name == NULL || rotation_item == NULL || !cJSON_IsNumber(rotation_item)) {
                continue;
            }

            angle_deg = (int)(rotation_item->valuedouble >= 0.0 ? (rotation_item->valuedouble + 0.5)
                                                                : (rotation_item->valuedouble - 0.5));
            if (!is_valid_servo_angle(angle_deg)) {
                ret = ESP_ERR_INVALID_ARG;
                goto cleanup;
            }
            if (frame_number > max_keyframe) {
                max_keyframe = frame_number;
            }

            if (parser_strcasecmp(axis_name, "z") == 0) {
                if (!append_keyframe(&x_frames, &x_count, &x_capacity, frame_number, angle_deg)) {
                    ret = ESP_ERR_NO_MEM;
                    goto cleanup;
                }
            } else if (parser_strcasecmp(axis_name, "x") == 0) {
                if (!append_keyframe(&y_frames, &y_count, &y_capacity, frame_number, clamp_servo_y_angle(angle_deg))) {
                    ret = ESP_ERR_NO_MEM;
                    goto cleanup;
                }
            }
        }
    }

    x_count = dedup_keyframes(x_frames, x_count);
    y_count = dedup_keyframes(y_frames, y_count);

    if (x_count > 0) {
        last_x = x_frames[0].angle_deg;
        effective_start = x_frames[0].frame_number;
    } else if (y_count > 0) {
        effective_start = y_frames[0].frame_number;
    } else {
        effective_start = frame_start;
    }

    if (y_count > 0) {
        last_y = y_frames[0].angle_deg;
        if (y_frames[0].frame_number < effective_start) {
            effective_start = y_frames[0].frame_number;
        }
    }
    if (frame_start < effective_start) {
        effective_start = frame_start;
    }

    effective_end = frame_end;
    if (max_keyframe > effective_end) {
        effective_end = max_keyframe;
    }
    if (effective_end < effective_start) {
        effective_end = effective_start;
    }
    effective_end_boundary = effective_end < INT_MAX ? effective_end + 1 : effective_end;

    for (index = 0; index < x_count; ++index) {
        if (!append_frame_number(&frame_numbers, &frame_count, &frame_capacity, x_frames[index].frame_number)) {
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
    }
    for (index = 0; index < y_count; ++index) {
        if (!append_frame_number(&frame_numbers, &frame_count, &frame_capacity, y_frames[index].frame_number)) {
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
    }

    frame_count = dedup_frame_numbers(frame_numbers, frame_count);
    if (frame_count > 0) {
        events =
            (behavior_motion_event_t *)behavior_persistent_calloc((size_t)frame_count, sizeof(behavior_motion_event_t));
        if (events == NULL) {
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
    }

    for (index = 0; index < frame_count; ++index) {
        int current_frame = frame_numbers[index];
        int x_deg = find_angle_for_frame(x_frames, x_count, current_frame, last_x);
        int y_deg = find_angle_for_frame(y_frames, y_count, current_frame, last_y);

        last_x = x_deg;
        last_y = y_deg;

        if (frame_count == 1) {
            events[event_count].at_ms = 0;
            events[event_count].x_deg = x_deg;
            events[event_count].y_deg = y_deg;
            events[event_count].duration_ms = BEHAVIOR_ACTION_SINGLE_KEYFRAME_DURATION_MS;
            events[event_count].motion_profile = BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT;
            event_count++;
            break;
        }

        if (index + 1 < frame_count) {
            int next_frame = frame_numbers[index + 1];
            int next_x_deg = find_angle_for_frame(x_frames, x_count, next_frame, last_x);
            int next_y_deg = find_angle_for_frame(y_frames, y_count, next_frame, last_y);
            uint32_t at_ms = frame_to_ms(current_frame, effective_start, fps);
            uint32_t next_ms = frame_to_ms(next_frame, effective_start, fps);
            int duration_ms = (next_ms > at_ms) ? (int)(next_ms - at_ms) : 0;

            if (duration_ms <= 0) {
                continue;
            }
            if (event_count > 0 && events[event_count - 1].x_deg == next_x_deg &&
                events[event_count - 1].y_deg == next_y_deg) {
                continue;
            }

            events[event_count].at_ms = at_ms;
            events[event_count].x_deg = next_x_deg;
            events[event_count].y_deg = next_y_deg;
            events[event_count].duration_ms = duration_ms;
            events[event_count].motion_profile = BEHAVIOR_MOTION_PROFILE_LINEAR;
            event_count++;
        }
    }

    out_action->motion = events;
    out_action->motion_count = event_count;
    out_action->total_duration_ms = frame_to_ms(effective_end_boundary, effective_start, fps);
    if (out_action->total_duration_ms == 0 && event_count > 0) {
        out_action->total_duration_ms = events[event_count - 1].at_ms + (uint32_t)events[event_count - 1].duration_ms;
    }
    events = NULL;

cleanup:
    cJSON_Delete(root);
    free(x_frames);
    free(y_frames);
    free(frame_numbers);
    free(events);
    if (ret != ESP_OK) {
        free(out_action->motion);
        memset(out_action, 0, sizeof(*out_action));
    }
    return ret;
}
