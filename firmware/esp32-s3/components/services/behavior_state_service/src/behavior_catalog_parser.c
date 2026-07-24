#include "behavior_catalog_parser.h"
#include "animation_playback_policy.h"
#include "behavior_memory.h"

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

static cJSON *parser_get_item_alias(cJSON *obj, const char *primary, const char *legacy) {
    cJSON *item = cJSON_GetObjectItem(obj, primary);
    return item != NULL || legacy == NULL ? item : cJSON_GetObjectItem(obj, legacy);
}

static int parser_get_non_negative_int_alias(cJSON *obj, const char *primary, const char *legacy, int default_value) {
    cJSON *item = parser_get_item_alias(obj, primary, legacy);
    if (item != NULL && cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return item->valueint;
    }
    return default_value;
}

static bool parser_get_playback_mode(cJSON *obj, animation_playback_mode_t *out_mode) {
    cJSON *item = cJSON_GetObjectItem(obj, "playback_mode");
    const char *mode = NULL;

    if (out_mode == NULL) {
        return false;
    }
    if (item == NULL) {
        *out_mode = ANIM_PLAYBACK_RESOURCE_DEFAULT;
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    mode = item->valuestring;
    if (strcmp(mode, "resource_default") == 0) {
        *out_mode = ANIM_PLAYBACK_RESOURCE_DEFAULT;
        return true;
    }
    if (strcmp(mode, "once") == 0) {
        *out_mode = ANIM_PLAYBACK_ONCE;
        return true;
    }
    if (strcmp(mode, "repeat") == 0) {
        *out_mode = ANIM_PLAYBACK_REPEAT_COUNT;
        return true;
    }
    if (strcmp(mode, "loop_until_replaced") == 0) {
        *out_mode = ANIM_PLAYBACK_LOOP_UNTIL_REPLACED;
        return true;
    }
    return false;
}

static uint8_t parser_get_motion_profile(cJSON *obj, uint8_t default_profile) {
    cJSON *item = cJSON_GetObjectItem(obj, "motion_profile");
    const char *profile_name = NULL;

    if (item == NULL) {
        item = cJSON_GetObjectItem(obj, "profile");
    }
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

    events = (behavior_motion_event_t *)behavior_persistent_calloc((size_t)count, sizeof(behavior_motion_event_t));
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

        x_deg = parser_get_non_negative_int_alias(item, "pan_deg", "x_deg", -1);
        y_deg = parser_get_non_negative_int_alias(item, "tilt_deg", "y_deg", -1);
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
    bool primary_animation_seen = false;
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

    events =
        (behavior_expression_event_t *)behavior_persistent_calloc((size_t)count, sizeof(behavior_expression_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_ArrayForEach(item, array) {
        const char *anim;
        const char *text;
        cJSON *repeat_item;
        cJSON *fade_item;
        animation_playback_mode_t playback_mode;
        uint32_t at_ms;

        if (!cJSON_IsObject(item)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        anim = parser_get_string(item, "anim");
        text = parser_get_string(item, "text");
        at_ms = (uint32_t)parser_get_non_negative_int(item, "at_ms", 0);
        repeat_item = cJSON_GetObjectItem(item, "repeat_count");
        fade_item = cJSON_GetObjectItem(item, "fade_in_ms");

        if (!parser_get_playback_mode(item, &playback_mode)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        if (repeat_item != NULL &&
            (!cJSON_IsNumber(repeat_item) || repeat_item->valuedouble < 0 || repeat_item->valuedouble > UINT16_MAX)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        if (fade_item != NULL &&
            (!cJSON_IsNumber(fade_item) || fade_item->valuedouble < 0 || fade_item->valuedouble > UINT16_MAX)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        if (!animation_playback_request_is_valid(playback_mode,
                                                 repeat_item != NULL ? (uint16_t)repeat_item->valueint : 0U)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        if ((anim == NULL || anim[0] == '\0') && (text == NULL || text[0] == '\0')) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        if (anim != NULL && anim[0] != '\0' && anim_validator != NULL && !anim_validator(anim, anim_validator_ctx)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        if (anim != NULL && anim[0] != '\0') {
            if (at_ms != 0U || primary_animation_seen) {
                free(events);
                return ESP_ERR_INVALID_ARG;
            }
            primary_animation_seen = true;
        }

        events[index].at_ms = at_ms;
        events[index].playback_mode = playback_mode;
        events[index].repeat_count = repeat_item != NULL ? (uint16_t)repeat_item->valueint : 0U;
        events[index].fade_in_ms = fade_item != NULL ? (uint16_t)fade_item->valueint : 0U;
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

    events = (behavior_sound_event_t *)behavior_persistent_calloc((size_t)count, sizeof(behavior_sound_event_t));
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

static esp_err_t parse_light_events(cJSON *array, behavior_light_event_t **out_events, int *out_count,
                                    uint32_t *out_timeline_end_ms) {
    int count;
    int index = 0;
    cJSON *item = NULL;
    behavior_light_event_t *events = NULL;

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
    events = (behavior_light_event_t *)behavior_persistent_calloc((size_t)count, sizeof(behavior_light_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_ArrayForEach(item, array) {
        const char *effect;
        const char *zone;
        int red;
        int green;
        int blue;
        int brightness;
        int period_ms;
        int repeat_count;
        uint32_t at_ms;
        uint64_t event_end_ms;

        if (!cJSON_IsObject(item)) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }
        effect = parser_get_string(item, "effect");
        zone = parser_get_string(item, "zone");
        red = parser_get_non_negative_int(item, "red", 0);
        green = parser_get_non_negative_int(item, "green", 0);
        blue = parser_get_non_negative_int(item, "blue", 0);
        brightness = parser_get_non_negative_int(item, "brightness", 100);
        period_ms = parser_get_non_negative_int(item, "period_ms", 0);
        repeat_count = parser_get_non_negative_int_alias(item, "repeat", "repeat_count", 0);
        at_ms = (uint32_t)parser_get_non_negative_int(item, "at_ms", 0);
        event_end_ms = (uint64_t)at_ms + (uint64_t)period_ms * (uint64_t)repeat_count;
        if (red > UINT8_MAX || green > UINT8_MAX || blue > UINT8_MAX || brightness > 100 || period_ms > UINT16_MAX ||
            repeat_count > UINT16_MAX || event_end_ms > UINT32_MAX) {
            free(events);
            return ESP_ERR_INVALID_ARG;
        }

        events[index].at_ms = at_ms;
        events[index].red = (uint8_t)red;
        events[index].green = (uint8_t)green;
        events[index].blue = (uint8_t)blue;
        events[index].brightness = (uint8_t)brightness;
        events[index].period_ms = (uint16_t)period_ms;
        events[index].repeat_count = (uint16_t)repeat_count;
        parser_copy_string(events[index].effect, sizeof(events[index].effect), effect);
        parser_copy_string(events[index].zone, sizeof(events[index].zone), zone);
        if ((uint32_t)event_end_ms > *out_timeline_end_ms) {
            *out_timeline_end_ms = (uint32_t)event_end_ms;
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
    cJSON *completion_item = NULL;
    cJSON *failure_item = NULL;
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

    completion_item = cJSON_GetObjectItem(obj, "on_animation_complete");
    if (completion_item != NULL) {
        const char *anim;
        const char *target_state;

        if (!cJSON_IsObject(completion_item)) {
            return ESP_ERR_INVALID_ARG;
        }
        anim = parser_get_string(completion_item, "anim");
        target_state = parser_get_string(completion_item, "state");
        if (anim == NULL || anim[0] == '\0' || target_state == NULL || target_state[0] == '\0' ||
            (anim_validator != NULL && !anim_validator(anim, anim_validator_ctx))) {
            return ESP_ERR_INVALID_ARG;
        }
        parser_copy_string(out_state->animation_complete_anim, sizeof(out_state->animation_complete_anim), anim);
        parser_copy_string(out_state->animation_complete_state, sizeof(out_state->animation_complete_state),
                           target_state);
    }

    failure_item = cJSON_GetObjectItem(obj, "on_animation_failed");
    if (failure_item != NULL) {
        const char *target_state;

        if (!cJSON_IsObject(failure_item)) {
            return ESP_ERR_INVALID_ARG;
        }
        target_state = parser_get_string(failure_item, "state");
        if (target_state == NULL || target_state[0] == '\0') {
            return ESP_ERR_INVALID_ARG;
        }
        parser_copy_string(out_state->animation_failure_state, sizeof(out_state->animation_failure_state),
                           target_state);
    }

    ret = parse_motion_events(cJSON_GetObjectItem(obj, "motion"), &out_state->motion, &out_state->motion_count,
                              &timeline_end_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = parse_expression_events(parser_get_item_alias(obj, "animation", "expression"), anim_validator,
                                  anim_validator_ctx, &out_state->expression, &out_state->expression_count,
                                  &timeline_end_ms);
    if (ret != ESP_OK) {
        free(out_state->motion);
        memset(out_state, 0, sizeof(*out_state));
        return ret;
    }

    ret = parse_sound_events(parser_get_item_alias(obj, "audio", "sound"), &out_state->sound, &out_state->sound_count,
                             &timeline_end_ms);
    if (ret != ESP_OK) {
        free(out_state->motion);
        free(out_state->expression);
        memset(out_state, 0, sizeof(*out_state));
        return ret;
    }

    ret = parse_light_events(cJSON_GetObjectItem(obj, "light"), &out_state->light, &out_state->light_count,
                             &timeline_end_ms);
    if (ret != ESP_OK) {
        free(out_state->motion);
        free(out_state->expression);
        free(out_state->sound);
        memset(out_state, 0, sizeof(*out_state));
        return ret;
    }

    out_state->timeline_end_ms = timeline_end_ms;
    return ESP_OK;
}

static bool catalog_contains_state(const behavior_catalog_t *catalog, const char *state_id) {
    int index;

    if (catalog == NULL || state_id == NULL || state_id[0] == '\0') {
        return false;
    }
    for (index = 0; index < catalog->state_count; ++index) {
        if (strcmp(catalog->states[index].id, state_id) == 0) {
            return true;
        }
    }
    return false;
}

static bool state_contains_expression_animation(const behavior_state_def_t *state, const char *anim_id) {
    int index;

    if (state == NULL || anim_id == NULL || anim_id[0] == '\0') {
        return false;
    }
    for (index = 0; index < state->expression_count; ++index) {
        if (strcmp(state->expression[index].anim, anim_id) == 0) {
            return true;
        }
    }
    return false;
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
                       parser_get_string(root, "schema_version") != NULL
                           ? parser_get_string(root, "schema_version")
                           : (parser_get_string(root, "version") != NULL ? parser_get_string(root, "version") : "1.0"));
    parser_copy_string(catalog.default_state, sizeof(catalog.default_state),
                       parser_get_string(root, "default_behavior") != NULL
                           ? parser_get_string(root, "default_behavior")
                           : (parser_get_string(root, "default_state") != NULL
                                  ? parser_get_string(root, "default_state")
                                  : "standby"));

    states_obj = parser_get_item_alias(root, "behaviors", "states");
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
        catalog.states =
            (behavior_state_def_t *)behavior_persistent_calloc((size_t)expected_count, sizeof(behavior_state_def_t));
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
    for (state_index = 0; state_index < catalog.state_count; ++state_index) {
        const behavior_state_def_t *state = &catalog.states[state_index];

        if (state->animation_complete_state[0] != '\0' &&
            (!catalog_contains_state(&catalog, state->animation_complete_state) ||
             !state_contains_expression_animation(state, state->animation_complete_anim) ||
             (!state->loop && !state->hold_until_replaced))) {
            behavior_free_catalog(&catalog);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        if (state->animation_failure_state[0] != '\0' &&
            !catalog_contains_state(&catalog, state->animation_failure_state)) {
            behavior_free_catalog(&catalog);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
    }
    cJSON_Delete(root);
    *out_catalog = catalog;
    return ESP_OK;
}
