#include "behavior_catalog_parser.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

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

static int parser_get_non_negative_int(cJSON *obj, const char *key, int default_value) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item != NULL && cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return item->valueint;
    }
    return default_value;
}

static const char *parser_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item != NULL && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

static uint8_t parser_get_motion_profile(cJSON *obj, uint8_t default_profile) {
    cJSON *item = cJSON_GetObjectItem(obj, "motion_profile");
    const char *profile_name = NULL;

    if (item == NULL) {
        return default_profile;
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT ? BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT
                                                                     : BEHAVIOR_MOTION_PROFILE_LINEAR;
    }
    if (!cJSON_IsString(item)) {
        return default_profile;
    }

    profile_name = item->valuestring;
    if (profile_name != NULL && strcmp(profile_name, "ease_in_out") == 0) {
        return BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT;
    }
    return BEHAVIOR_MOTION_PROFILE_LINEAR;
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

static esp_err_t parse_motion_events(cJSON *array, behavior_motion_event_t **out_events, int *out_count,
                                     uint32_t *out_timeline_end_ms) {
    int count;
    int index = 0;
    cJSON *item = NULL;
    behavior_motion_event_t *events = NULL;

    *out_events = NULL;
    *out_count = 0;
    if (array == NULL) {
        return ESP_OK;
    }
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }

    count = cJSON_GetArraySize(array);
    if (count <= 0) {
        return ESP_OK;
    }

    events = (behavior_motion_event_t *)calloc((size_t)count, sizeof(behavior_motion_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_ArrayForEach(item, array) {
        int x_deg;
        int y_deg;
        int duration_ms;
        uint32_t at_ms;

        if (!cJSON_IsObject(item)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        x_deg = parser_get_non_negative_int(item, "x_deg", -1);
        y_deg = parser_get_non_negative_int(item, "y_deg", -1);
        duration_ms = parser_get_non_negative_int(item, "duration_ms", 0);
        at_ms = (uint32_t)parser_get_non_negative_int(item, "at_ms", 0);
        if (x_deg < BEHAVIOR_SERVO_LOGICAL_MIN_DEG || x_deg > BEHAVIOR_SERVO_LOGICAL_MAX_DEG ||
            y_deg < BEHAVIOR_SERVO_LOGICAL_MIN_DEG || y_deg > BEHAVIOR_SERVO_LOGICAL_MAX_DEG) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        events[index].at_ms = at_ms;
        events[index].x_deg = x_deg;
        events[index].y_deg = clamp_servo_y_angle(y_deg);
        events[index].duration_ms = duration_ms;
        events[index].motion_profile = parser_get_motion_profile(item, BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT);
        if (at_ms + (uint32_t)duration_ms > *out_timeline_end_ms) {
            *out_timeline_end_ms = at_ms + (uint32_t)duration_ms;
        }
        index++;
    }

    *out_events = events;
    *out_count = count;
    return ESP_OK;
}

static esp_err_t parse_expression_events(cJSON *array, behavior_anim_validator_t anim_validator,
                                         void *anim_validator_ctx, behavior_expression_event_t **out_events,
                                         int *out_count, uint32_t *out_timeline_end_ms) {
    int count;
    int index = 0;
    cJSON *item = NULL;
    behavior_expression_event_t *events = NULL;

    *out_events = NULL;
    *out_count = 0;
    if (array == NULL) {
        return ESP_OK;
    }
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }

    count = cJSON_GetArraySize(array);
    if (count <= 0) {
        return ESP_OK;
    }

    events = (behavior_expression_event_t *)calloc((size_t)count, sizeof(behavior_expression_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_ArrayForEach(item, array) {
        const char *anim;
        const char *text;
        uint32_t at_ms;

        if (!cJSON_IsObject(item)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        anim = parser_get_string(item, "anim");
        text = parser_get_string(item, "text");
        at_ms = (uint32_t)parser_get_non_negative_int(item, "at_ms", 0);

        if ((anim == NULL || anim[0] == '\0') && (text == NULL || text[0] == '\0')) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        if (anim != NULL && anim[0] != '\0' && anim_validator != NULL &&
            !anim_validator(anim, anim_validator_ctx)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        events[index].at_ms = at_ms;
        events[index].font_size = parser_get_non_negative_int(item, "font_size", 0);
        parser_copy_string(events[index].anim, sizeof(events[index].anim), anim);
        parser_copy_string(events[index].text, sizeof(events[index].text), text);
        if (at_ms > *out_timeline_end_ms) {
            *out_timeline_end_ms = at_ms;
        }
        index++;
    }

    *out_events = events;
    *out_count = count;
    return ESP_OK;
}

static esp_err_t parse_sound_events(cJSON *array, behavior_sound_event_t **out_events, int *out_count,
                                    uint32_t *out_timeline_end_ms) {
    int count;
    int index = 0;
    cJSON *item = NULL;
    behavior_sound_event_t *events = NULL;

    *out_events = NULL;
    *out_count = 0;
    if (array == NULL) {
        return ESP_OK;
    }
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }

    count = cJSON_GetArraySize(array);
    if (count <= 0) {
        return ESP_OK;
    }

    events = (behavior_sound_event_t *)calloc((size_t)count, sizeof(behavior_sound_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_ArrayForEach(item, array) {
        const char *sound_id;
        uint32_t at_ms;

        if (!cJSON_IsObject(item)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        sound_id = parser_get_string(item, "sound_id");
        at_ms = (uint32_t)parser_get_non_negative_int(item, "at_ms", 0);
        if (sound_id == NULL || sound_id[0] == '\0') {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        events[index].at_ms = at_ms;
        parser_copy_string(events[index].sound_id, sizeof(events[index].sound_id), sound_id);
        if (at_ms > *out_timeline_end_ms) {
            *out_timeline_end_ms = at_ms;
        }
        index++;
    }

    *out_events = events;
    *out_count = count;
    return ESP_OK;
}

static esp_err_t parse_state_def(const char *state_id, cJSON *obj, behavior_anim_validator_t anim_validator,
                                 void *anim_validator_ctx, behavior_state_def_t *out_state) {
    cJSON *loop_item = NULL;
    uint32_t timeline_end_ms = 0;
    esp_err_t ret;

    if (state_id == NULL || state_id[0] == '\0' || obj == NULL || out_state == NULL || !cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_state, 0, sizeof(*out_state));
    parser_copy_string(out_state->id, sizeof(out_state->id), state_id);

    loop_item = cJSON_GetObjectItem(obj, "loop");
    out_state->loop = (loop_item != NULL && cJSON_IsBool(loop_item) && cJSON_IsTrue(loop_item));
    loop_item = cJSON_GetObjectItem(obj, "hold_until_replaced");
    out_state->hold_until_replaced = (loop_item != NULL && cJSON_IsBool(loop_item) && cJSON_IsTrue(loop_item));

    ret = parse_motion_events(cJSON_GetObjectItem(obj, "motion"), &out_state->motion, &out_state->motion_count,
                              &timeline_end_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = parse_expression_events(cJSON_GetObjectItem(obj, "expression"), anim_validator, anim_validator_ctx,
                                  &out_state->expression, &out_state->expression_count, &timeline_end_ms);
    if (ret != ESP_OK) {
        free(out_state->motion);
        memset(out_state, 0, sizeof(*out_state));
        return ret;
    }

    ret = parse_sound_events(cJSON_GetObjectItem(obj, "sound"), &out_state->sound, &out_state->sound_count,
                             &timeline_end_ms);
    if (ret != ESP_OK) {
        free(out_state->motion);
        free(out_state->expression);
        memset(out_state, 0, sizeof(*out_state));
        return ret;
    }

    out_state->timeline_end_ms = timeline_end_ms;
    return ESP_OK;
}

esp_err_t behavior_catalog_parse_json(const char *json, size_t json_len, behavior_anim_validator_t anim_validator,
                                      void *anim_validator_ctx, behavior_catalog_t *out_catalog) {
    cJSON *root = NULL;
    cJSON *states_obj = NULL;
    cJSON *state_item = NULL;
    behavior_catalog_t catalog = {0};
    int expected_count = 0;
    int state_index = 0;

    if (json == NULL || out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        return ESP_FAIL;
    }

    parser_copy_string(catalog.version, sizeof(catalog.version),
                       parser_get_string(root, "version") != NULL ? parser_get_string(root, "version") : "1.0");
    parser_copy_string(catalog.default_state, sizeof(catalog.default_state),
                       parser_get_string(root, "default_state") != NULL ? parser_get_string(root, "default_state")
                                                                         : "standby");

    states_obj = cJSON_GetObjectItem(root, "states");
    if (states_obj == NULL || !cJSON_IsObject(states_obj)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_ArrayForEach(state_item, states_obj) {
        if (state_item->string != NULL) {
            expected_count++;
        }
    }

    if (expected_count > 0) {
        catalog.states = (behavior_state_def_t *)calloc((size_t)expected_count, sizeof(behavior_state_def_t));
        if (catalog.states == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
    }

    cJSON_ArrayForEach(state_item, states_obj) {
        esp_err_t ret;

        if (state_item->string == NULL) {
            continue;
        }

        ret = parse_state_def(state_item->string, state_item, anim_validator, anim_validator_ctx,
                              &catalog.states[state_index]);
        if (ret != ESP_OK) {
            behavior_free_catalog(&catalog);
            cJSON_Delete(root);
            return ret;
        }
        state_index++;
    }

    catalog.state_count = state_index;
    cJSON_Delete(root);
    *out_catalog = catalog;
    return ESP_OK;
}
