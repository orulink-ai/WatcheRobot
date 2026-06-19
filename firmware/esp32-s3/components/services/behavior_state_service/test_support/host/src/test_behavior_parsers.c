#include "behavior_action_parser.h"
#include "behavior_catalog_parser.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static bool anim_validator(const char *anim_id, void *ctx) {
    (void)ctx;
    return anim_id != NULL && (strcmp(anim_id, "standby") == 0 || strcmp(anim_id, "happy") == 0 ||
                               strcmp(anim_id, "standby1") == 0 || strcmp(anim_id, "standby2") == 0 ||
                               strcmp(anim_id, "standby3") == 0 || strcmp(anim_id, "standby4") == 0 ||
                               strcmp(anim_id, "listening") == 0 || strcmp(anim_id, "fondle_love") == 0);
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *handle = NULL;
    long file_size;
    char *buffer = NULL;

    if (path == NULL || out_len == NULL) {
        return NULL;
    }

    handle = fopen(path, "rb");
    if (handle == NULL) {
        return NULL;
    }
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return NULL;
    }
    file_size = ftell(handle);
    if (file_size < 0) {
        fclose(handle);
        return NULL;
    }
    rewind(handle);

    buffer = (char *)malloc((size_t)file_size + 1U);
    if (buffer == NULL) {
        fclose(handle);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)file_size, handle) != (size_t)file_size) {
        free(buffer);
        fclose(handle);
        return NULL;
    }
    fclose(handle);
    buffer[file_size] = '\0';
    *out_len = (size_t)file_size;
    return buffer;
}

static const behavior_state_def_t *find_state(const behavior_catalog_t *catalog, const char *state_id) {
    int index;

    if (catalog == NULL || state_id == NULL) {
        return NULL;
    }
    for (index = 0; index < catalog->state_count; ++index) {
        if (strcmp(catalog->states[index].id, state_id) == 0) {
            return &catalog->states[index];
        }
    }
    return NULL;
}

static void test_action_parser_converts_keyframes_to_motion(void) {
    static const char json[] =
        "{"
        "\"fps\":10,"
        "\"frame_start\":0,"
        "\"frame_end\":3,"
        "\"animated_objects\":["
        "{\"keyframe_data\":["
        "{\"active_axis\":\"z\",\"frame_number\":0,\"rotation_angle\":90},"
        "{\"active_axis\":\"z\",\"frame_number\":2,\"rotation_angle\":120},"
        "{\"active_axis\":\"x\",\"frame_number\":1,\"rotation_angle\":110}"
        "]}"
        "]"
        "}";
    behavior_action_def_t action = {0};

    assert(behavior_action_parse_json("wave", json, sizeof(json) - 1U, &action) == ESP_OK);
    assert(strcmp(action.id, "wave") == 0);
    assert(action.motion_count == 2);
    assert(action.motion[0].at_ms == 0);
    assert(action.motion[0].x_deg == 90);
    assert(action.motion[0].y_deg == 110);
    assert(action.motion[0].duration_ms == 100);
    assert(action.motion[0].motion_profile == BEHAVIOR_MOTION_PROFILE_LINEAR);
    assert(action.motion[1].at_ms == 100);
    assert(action.motion[1].x_deg == 120);
    assert(action.motion[1].y_deg == 110);
    assert(action.motion[1].duration_ms == 100);
    assert(action.motion[1].motion_profile == BEHAVIOR_MOTION_PROFILE_LINEAR);
    assert(action.total_duration_ms == 400);
    free(action.motion);
}

static void test_action_parser_single_keyframe_uses_fixed_duration(void) {
    static const char json[] =
        "{"
        "\"fps\":24,"
        "\"frame_start\":0,"
        "\"frame_end\":0,"
        "\"animated_objects\":["
        "{\"keyframe_data\":[{\"active_axis\":\"z\",\"frame_number\":5,\"rotation_angle\":135}]}"
        "]"
        "}";
    behavior_action_def_t action = {0};

    assert(behavior_action_parse_json("single", json, sizeof(json) - 1U, &action) == ESP_OK);
    assert(action.motion_count == 1);
    assert(action.motion[0].at_ms == 0);
    assert(action.motion[0].x_deg == 135);
    assert(action.motion[0].y_deg == BEHAVIOR_ACTION_DEFAULT_Y_DEG);
    assert(action.motion[0].duration_ms == BEHAVIOR_ACTION_SINGLE_KEYFRAME_DURATION_MS);
    assert(action.motion[0].motion_profile == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT);
    free(action.motion);
}

static void test_action_parser_rejects_missing_timing(void) {
    behavior_action_def_t action = {0};
    static const char json[] = "{\"animated_objects\":[]}";

    assert(behavior_action_parse_json("bad", json, sizeof(json) - 1U, &action) == ESP_ERR_INVALID_ARG);
    assert(action.motion == NULL);
}

static void test_action_parser_uses_json_length(void) {
    behavior_action_def_t action = {0};
    static const char json_with_tail[] =
        "{\"fps\":10,\"frame_start\":0,\"frame_end\":0,\"animated_objects\":[{\"keyframe_data\":["
        "{\"active_axis\":\"z\",\"frame_number\":0,\"rotation_angle\":100}]}]}TRAILING";
    const size_t json_len = sizeof(json_with_tail) - 1U - strlen("TRAILING");

    assert(behavior_action_parse_json("bounded", json_with_tail, json_len, &action) == ESP_OK);
    assert(action.motion_count == 1);
    assert(action.motion[0].x_deg == 100);
    free(action.motion);
}

static void test_action_parser_rejects_invalid_angle(void) {
    behavior_action_def_t action = {0};
    static const char json[] =
        "{\"fps\":10,\"frame_start\":0,\"frame_end\":0,\"animated_objects\":[{\"keyframe_data\":["
        "{\"active_axis\":\"z\",\"frame_number\":0,\"rotation_angle\":181}]}]}";

    assert(behavior_action_parse_json("bad_angle", json, sizeof(json) - 1U, &action) == ESP_ERR_INVALID_ARG);
    assert(action.motion == NULL);
}

static void test_action_parser_clamps_y_axis_angle(void) {
    behavior_action_def_t action = {0};
    static const char below_json[] =
        "{"
        "\"fps\":10,"
        "\"frame_start\":0,"
        "\"frame_end\":0,"
        "\"animated_objects\":["
        "{\"keyframe_data\":[{\"active_axis\":\"x\",\"frame_number\":0,\"rotation_angle\":90}]}"
        "]"
        "}";
    static const char above_json[] =
        "{"
        "\"fps\":10,"
        "\"frame_start\":0,"
        "\"frame_end\":0,"
        "\"animated_objects\":["
        "{\"keyframe_data\":[{\"active_axis\":\"x\",\"frame_number\":0,\"rotation_angle\":160}]}"
        "]"
        "}";

    assert(behavior_action_parse_json("clamp_y_below", below_json, sizeof(below_json) - 1U, &action) == ESP_OK);
    assert(action.motion_count == 1);
    assert(action.motion[0].y_deg == BEHAVIOR_SERVO_Y_MIN_DEG);
    free(action.motion);

    memset(&action, 0, sizeof(action));
    assert(behavior_action_parse_json("clamp_y_above", above_json, sizeof(above_json) - 1U, &action) == ESP_OK);
    assert(action.motion_count == 1);
    assert(action.motion[0].y_deg == BEHAVIOR_SERVO_Y_MAX_DEG);
    free(action.motion);
}

static void test_catalog_parser_loads_state_timeline(void) {
    static const char json[] =
        "{"
        "\"version\":\"1.0\","
        "\"default_state\":\"standby\","
        "\"states\":{"
        "\"standby\":{"
        "\"loop\":true,"
        "\"motion\":[{\"at_ms\":10,\"x_deg\":90,\"y_deg\":120,\"duration_ms\":300}],"
        "\"expression\":[{\"at_ms\":20,\"anim\":\"standby\",\"text\":\"\",\"font_size\":24}],"
        "\"sound\":[{\"at_ms\":350,\"sound_id\":\"boot\"}]"
        "}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    assert(strcmp(catalog.version, "1.0") == 0);
    assert(strcmp(catalog.default_state, "standby") == 0);
    assert(catalog.state_count == 1);
    assert(catalog.states[0].loop);
    assert(catalog.states[0].motion_count == 1);
    assert(catalog.states[0].motion[0].motion_profile == BEHAVIOR_MOTION_PROFILE_EASE_IN_OUT);
    assert(catalog.states[0].expression_count == 1);
    assert(catalog.states[0].sound_count == 1);
    assert(catalog.states[0].timeline_end_ms == 350);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_accepts_linear_motion_profile_override(void) {
    static const char json[] =
        "{"
        "\"states\":{"
        "\"standby\":{"
        "\"motion\":[{\"x_deg\":90,\"y_deg\":120,\"duration_ms\":300,\"motion_profile\":\"linear\"}],"
        "\"expression\":[{\"text\":\"ready\"}],"
        "\"sound\":[]"
        "}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    assert(catalog.state_count == 1);
    assert(catalog.states[0].motion_count == 1);
    assert(catalog.states[0].motion[0].motion_profile == BEHAVIOR_MOTION_PROFILE_LINEAR);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_uses_json_length(void) {
    static const char json_with_tail[] =
        "{\"states\":{\"standby\":{\"expression\":[{\"text\":\"ready\"}],\"motion\":[],\"sound\":[]}}}TRAILING";
    const size_t json_len = sizeof(json_with_tail) - 1U - strlen("TRAILING");
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json_with_tail, json_len, anim_validator, NULL, &catalog) == ESP_OK);
    assert(catalog.state_count == 1);
    assert(strcmp(catalog.states[0].id, "standby") == 0);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_rejects_unknown_animation(void) {
    static const char json[] =
        "{"
        "\"states\":{"
        "\"bad\":{\"expression\":[{\"at_ms\":0,\"anim\":\"missing\"}],\"motion\":[],\"sound\":[]}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) ==
           ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_invalid_motion_angle(void) {
    static const char json[] =
        "{"
        "\"states\":{"
        "\"bad\":{\"motion\":[{\"x_deg\":181,\"y_deg\":90}],\"expression\":[{\"text\":\"bad\"}],\"sound\":[]}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) ==
           ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_clamps_y_axis_angle(void) {
    static const char json[] =
        "{"
        "\"states\":{"
        "\"standby\":{"
        "\"motion\":["
        "{\"x_deg\":90,\"y_deg\":90,\"duration_ms\":300},"
        "{\"x_deg\":90,\"y_deg\":160,\"duration_ms\":300}"
        "],"
        "\"expression\":[{\"text\":\"ready\"}],"
        "\"sound\":[]"
        "}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    assert(catalog.state_count == 1);
    assert(catalog.states[0].motion_count == 2);
    assert(catalog.states[0].motion[0].y_deg == BEHAVIOR_SERVO_Y_MIN_DEG);
    assert(catalog.states[0].motion[1].y_deg == BEHAVIOR_SERVO_Y_MAX_DEG);
    behavior_free_catalog(&catalog);
}

static void test_production_fondle_love_is_one_shot(void) {
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *fondle_love = NULL;
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);
    fondle_love = find_state(&catalog, "fondle_love");

    assert(fondle_love != NULL);
    assert(!fondle_love->loop);
    assert(!fondle_love->hold_until_replaced);
    assert(fondle_love->expression_count == 1);
    assert(strcmp(fondle_love->expression[0].anim, "fondle_love") == 0);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_recharge_is_one_shot_hold(void) {
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *recharge = NULL;
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);
    recharge = find_state(&catalog, "recharge");

    assert(recharge != NULL);
    assert(!recharge->loop);
    assert(recharge->hold_until_replaced);
    assert(recharge->expression_count == 1);
    assert(strcmp(recharge->expression[0].anim, "recharge") == 0);
    assert(recharge->sound_count == 1);
    assert(strcmp(recharge->sound[0].sound_id, "recharge") == 0);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_standby_variants_are_states(void) {
    behavior_catalog_t catalog = {0};
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);
    static const char *variants[] = {"standby1", "standby2", "standby3", "standby4"};
    size_t index;

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);

    for (index = 0; index < ARRAY_SIZE(variants); ++index) {
        const behavior_state_def_t *state = find_state(&catalog, variants[index]);

        assert(state != NULL);
        assert(state->loop);
        assert(state->expression_count == 1);
        assert(strcmp(state->expression[0].anim, variants[index]) == 0);
    }

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_voice_flow_states_clear_text_and_keep_anim(void) {
    behavior_catalog_t catalog = {0};
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);
    static const char *voice_states[] = {"listening", "thinking", "processing", "speaking"};
    size_t index;

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);

    for (index = 0; index < ARRAY_SIZE(voice_states); ++index) {
        const behavior_state_def_t *state = find_state(&catalog, voice_states[index]);

        assert(state != NULL);
        assert(state->expression_count == 1);
        assert(strcmp(state->expression[0].anim, voice_states[index]) == 0);
        assert(strcmp(state->expression[0].text, "") == 0);
    }

    behavior_free_catalog(&catalog);
    free(json);
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"action_parser_converts_keyframes_to_motion", test_action_parser_converts_keyframes_to_motion},
        {"action_parser_single_keyframe_uses_fixed_duration", test_action_parser_single_keyframe_uses_fixed_duration},
        {"action_parser_rejects_missing_timing", test_action_parser_rejects_missing_timing},
        {"action_parser_uses_json_length", test_action_parser_uses_json_length},
        {"action_parser_rejects_invalid_angle", test_action_parser_rejects_invalid_angle},
        {"action_parser_clamps_y_axis_angle", test_action_parser_clamps_y_axis_angle},
        {"catalog_parser_loads_state_timeline", test_catalog_parser_loads_state_timeline},
        {"catalog_parser_accepts_linear_motion_profile_override",
         test_catalog_parser_accepts_linear_motion_profile_override},
        {"catalog_parser_uses_json_length", test_catalog_parser_uses_json_length},
        {"catalog_parser_rejects_unknown_animation", test_catalog_parser_rejects_unknown_animation},
        {"catalog_parser_rejects_invalid_motion_angle", test_catalog_parser_rejects_invalid_motion_angle},
        {"catalog_parser_clamps_y_axis_angle", test_catalog_parser_clamps_y_axis_angle},
        {"production_fondle_love_is_one_shot", test_production_fondle_love_is_one_shot},
        {"production_recharge_is_one_shot_hold", test_production_recharge_is_one_shot_hold},
        {"production_standby_variants_are_states", test_production_standby_variants_are_states},
        {"production_voice_flow_states_clear_text_and_keep_anim",
         test_production_voice_flow_states_clear_text_and_keep_anim},
    };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
