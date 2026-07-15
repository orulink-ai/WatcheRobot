#include "behavior_action_parser.h"
#include "behavior_catalog_parser.h"
#include "behavior_catalog_loader.h"
#include "behavior_memory.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static bool anim_validator(const char *anim_id, void *ctx) {
    (void)ctx;
    return anim_id != NULL &&
           (strcmp(anim_id, "standby") == 0 || strcmp(anim_id, "happy") == 0 || strcmp(anim_id, "standby_start") == 0 ||
            strcmp(anim_id, "standby_loop") == 0 || strcmp(anim_id, "standby_end") == 0 ||
            strcmp(anim_id, "music") == 0 || strcmp(anim_id, "standby1") == 0 || strcmp(anim_id, "standby2") == 0 ||
            strcmp(anim_id, "standby3") == 0 || strcmp(anim_id, "standby4") == 0 || strcmp(anim_id, "listening") == 0 ||
            strcmp(anim_id, "fondle_love") == 0);
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
    static const char json[] = "{"
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

    behavior_memory_test_reset();
    assert(behavior_action_parse_json("wave", json, sizeof(json) - 1U, &action) == ESP_OK);
    assert(behavior_memory_test_persistent_allocations() == 1U);
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
    static const char json[] = "{"
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
    static const char json[] = "{"
                               "\"version\":\"1.0\","
                               "\"default_state\":\"standby\","
                               "\"states\":{"
                               "\"standby\":{"
                               "\"loop\":true,"
                               "\"motion\":[{\"at_ms\":10,\"x_deg\":90,\"y_deg\":120,\"duration_ms\":300}],"
                               "\"expression\":[{\"at_ms\":0,\"anim\":\"standby\",\"text\":\"\",\"font_size\":24}],"
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

static void test_catalog_parser_loads_animation_repeat_and_failure_transition(void) {
    static const char json[] = "{"
                               "\"states\":{"
                               "\"standby_entry\":{"
                               "\"hold_until_replaced\":true,"
                               "\"on_animation_complete\":{\"anim\":\"standby\",\"state\":\"standby_loop\"},"
                               "\"on_animation_failed\":{\"state\":\"standby_loop\"},"
                                   "\"expression\":[{\"anim\":\"standby\",\"playback_mode\":\"repeat\","
                                   "\"repeat_count\":2,\"fade_in_ms\":3000}],"
                               "\"motion\":[],\"sound\":[]"
                               "},"
                               "\"standby_loop\":{\"loop\":true,\"expression\":[{\"anim\":\"standby_loop\"}]}"
                               "}"
                               "}";
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *entry = NULL;

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    entry = find_state(&catalog, "standby_entry");
    assert(entry != NULL);
    assert(entry->expression_count == 1);
    assert(entry->expression[0].repeat_count == 2U);
    assert(entry->expression[0].playback_mode == ANIM_PLAYBACK_REPEAT_COUNT);
    assert(entry->expression[0].fade_in_ms == 3000U);
    assert(strcmp(entry->animation_failure_state, "standby_loop") == 0);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_loads_explicit_animation_playback_modes(void) {
    static const char json[] = "{\"states\":{"
                               "\"once\":{\"expression\":[{\"anim\":\"happy\",\"playback_mode\":\"once\"}]},"
                               "\"repeat\":{\"expression\":[{\"anim\":\"standby\",\"playback_mode\":\"repeat\","
                               "\"repeat_count\":2}]},"
                               "\"loop\":{\"expression\":[{\"anim\":\"music\","
                               "\"playback_mode\":\"loop_until_replaced\"}]}"
                               "}}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    assert(find_state(&catalog, "once")->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);
    assert(find_state(&catalog, "repeat")->expression[0].playback_mode == ANIM_PLAYBACK_REPEAT_COUNT);
    assert(find_state(&catalog, "loop")->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_accepts_public_behavior_schema(void) {
    static const char json[] =
        "{"
        "\"schema_version\":\"1.0\","
        "\"default_behavior\":\"greeting\","
        "\"behaviors\":{"
        "\"greeting\":{"
        "\"loop\":false,"
        "\"motion\":[{\"at_ms\":0,\"pan_deg\":110,\"tilt_deg\":120,\"duration_ms\":300,"
        "\"profile\":\"linear\"}],"
        "\"animation\":[{\"at_ms\":0,\"anim\":\"happy\",\"playback_mode\":\"once\"}],"
        "\"audio\":[{\"at_ms\":20,\"sound_id\":\"hello\"}],"
        "\"light\":[{\"at_ms\":0,\"effect\":\"breathing\",\"red\":77,\"green\":163,"
        "\"blue\":255,\"brightness\":70,\"period_ms\":800,\"repeat\":1}]"
        "}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *greeting;

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_OK);
    assert(strcmp(catalog.version, "1.0") == 0);
    assert(strcmp(catalog.default_state, "greeting") == 0);
    assert(catalog.state_count == 1);
    greeting = find_state(&catalog, "greeting");
    assert(greeting != NULL);
    assert(greeting->motion_count == 1);
    assert(greeting->motion[0].x_deg == 110);
    assert(greeting->motion[0].y_deg == 120);
    assert(greeting->motion[0].motion_profile == BEHAVIOR_MOTION_PROFILE_LINEAR);
    assert(greeting->expression_count == 1);
    assert(strcmp(greeting->expression[0].anim, "happy") == 0);
    assert(greeting->sound_count == 1);
    assert(strcmp(greeting->sound[0].sound_id, "hello") == 0);
    assert(greeting->light_count == 1);
    assert(strcmp(greeting->light[0].effect, "breathing") == 0);
    assert(greeting->light[0].red == 77U);
    assert(greeting->light[0].green == 163U);
    assert(greeting->light[0].blue == 255U);
    assert(greeting->light[0].brightness == 70U);
    assert(greeting->light[0].period_ms == 800U);
    assert(greeting->light[0].repeat_count == 1U);
    assert(greeting->timeline_end_ms == 800U);
    behavior_free_catalog(&catalog);
}

static void test_catalog_parser_rejects_invalid_animation_playback_combinations(void) {
    static const char missing_repeat[] =
        "{\"states\":{\"bad\":{\"expression\":[{\"anim\":\"standby\",\"playback_mode\":\"repeat\"}]}}}";
    static const char once_with_repeat[] =
        "{\"states\":{\"bad\":{\"expression\":[{\"anim\":\"happy\",\"playback_mode\":\"once\","
        "\"repeat_count\":2}]}}}";
    static const char unknown_mode[] =
        "{\"states\":{\"bad\":{\"expression\":[{\"anim\":\"happy\",\"playback_mode\":\"forever\"}]}}}";
    static const char non_string_mode[] =
        "{\"states\":{\"bad\":{\"expression\":[{\"anim\":\"happy\",\"playback_mode\":1}]}}}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(missing_repeat, sizeof(missing_repeat) - 1U, anim_validator, NULL, &catalog) ==
           ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
    assert(behavior_catalog_parse_json(once_with_repeat, sizeof(once_with_repeat) - 1U, anim_validator, NULL,
                                       &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
    assert(behavior_catalog_parse_json(unknown_mode, sizeof(unknown_mode) - 1U, anim_validator, NULL, &catalog) ==
           ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
    assert(behavior_catalog_parse_json(non_string_mode, sizeof(non_string_mode) - 1U, anim_validator, NULL, &catalog) ==
           ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_missing_animation_failure_target(void) {
    static const char json[] = "{"
                               "\"states\":{"
                               "\"standby\":{"
                               "\"loop\":true,"
                               "\"on_animation_failed\":{\"state\":\"missing\"},"
                               "\"expression\":[{\"anim\":\"standby\"}]"
                               "}"
                               "}"
                               "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_cleans_partial_psram_allocation_on_oom(void) {
    static const char json[] = "{\"states\":{\"standby\":{"
                               "\"motion\":[{\"x_deg\":90,\"y_deg\":120,\"duration_ms\":300}],"
                               "\"expression\":[{\"text\":\"ready\"}],"
                               "\"sound\":[{\"sound_id\":\"boot\"}]}}}";
    behavior_catalog_t catalog = {0};

    behavior_memory_test_reset();
    behavior_memory_test_fail_on_attempt(2U);
    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_NO_MEM);
    assert(behavior_memory_test_allocation_attempts() == 2U);
    assert(catalog.states == NULL);
    assert(catalog.state_count == 0);
    behavior_free_catalog(&catalog);
    behavior_memory_test_reset();
}

static void test_persistent_allocator_rejects_size_overflow(void) {
    behavior_memory_test_reset();
    assert(behavior_persistent_calloc(SIZE_MAX, 2U) == NULL);
    assert(behavior_memory_test_allocation_attempts() == 0U);
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

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_delayed_primary_animation(void) {
    static const char json[] =
        "{"
        "\"states\":{"
        "\"bad\":{\"expression\":[{\"at_ms\":1,\"anim\":\"standby\"}],\"motion\":[],\"sound\":[]}"
        "}"
        "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_multiple_primary_animations(void) {
    static const char json[] = "{"
                               "\"states\":{"
                               "\"bad\":{\"expression\":["
                               "{\"at_ms\":0,\"anim\":\"standby\"},"
                               "{\"at_ms\":0,\"anim\":\"happy\"}],\"motion\":[],\"sound\":[]}"
                               "}"
                               "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_missing_animation_transition_target(void) {
    static const char json[] = "{"
                               "\"states\":{"
                               "\"wake\":{"
                               "\"hold_until_replaced\":true,"
                               "\"on_animation_complete\":{\"anim\":\"standby_end\",\"state\":\"missing\"},"
                               "\"expression\":[{\"anim\":\"standby_end\"}],\"motion\":[],\"sound\":[]"
                               "}"
                               "}"
                               "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_rejects_unowned_animation_transition(void) {
    static const char json[] = "{"
                               "\"states\":{"
                               "\"wake\":{"
                               "\"hold_until_replaced\":true,"
                               "\"on_animation_complete\":{\"anim\":\"standby_end\",\"state\":\"listening\"},"
                               "\"expression\":[{\"anim\":\"standby\"}],\"motion\":[],\"sound\":[]"
                               "},"
                               "\"listening\":{\"loop\":true,\"expression\":[{\"anim\":\"listening\"}]}"
                               "}"
                               "}";
    behavior_catalog_t catalog = {0};

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
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

    assert(behavior_catalog_parse_json(json, sizeof(json) - 1U, anim_validator, NULL, &catalog) == ESP_ERR_INVALID_ARG);
    assert(catalog.states == NULL);
}

static void test_catalog_parser_clamps_y_axis_angle(void) {
    static const char json[] = "{"
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
    assert(fondle_love->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);

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
    assert(recharge->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);
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
        assert(state->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);
    }

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_voice_sleep_transitions_are_states(void) {
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *standby = NULL;
    const behavior_state_def_t *standby_entry = NULL;
    const behavior_state_def_t *standby_start = NULL;
    const behavior_state_def_t *standby_loop = NULL;
    const behavior_state_def_t *standby_end = NULL;
    const behavior_state_def_t *listening_wake = NULL;
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);

    standby = find_state(&catalog, "standby");
    standby_entry = find_state(&catalog, "standby_entry");
    standby_loop = find_state(&catalog, "standby_loop");
    standby_start = find_state(&catalog, "standby_start");
    standby_end = find_state(&catalog, "standby_end");
    listening_wake = find_state(&catalog, "listening_wake");

    assert(standby != NULL);
    assert(standby->loop);
    assert(standby->expression_count == 1);
    assert(strcmp(standby->expression[0].anim, "standby_loop") == 0);
    assert(standby->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);

    assert(standby_entry != NULL);
    assert(!standby_entry->loop);
    assert(standby_entry->hold_until_replaced);
    assert(standby_entry->expression_count == 1);
    assert(strcmp(standby_entry->expression[0].anim, "standby") == 0);
    assert(standby_entry->expression[0].repeat_count == 2U);
    assert(standby_entry->expression[0].playback_mode == ANIM_PLAYBACK_REPEAT_COUNT);
    assert(standby_entry->expression[0].fade_in_ms == 3000U);
    assert(strcmp(standby_entry->animation_complete_anim, "standby") == 0);
    assert(strcmp(standby_entry->animation_complete_state, "standby_loop") == 0);
    assert(strcmp(standby_entry->animation_failure_state, "error") == 0);

    assert(standby_loop != NULL);
    assert(standby_loop->loop);
    assert(standby_loop->expression_count == 1);
    assert(strcmp(standby_loop->expression[0].anim, "standby_loop") == 0);

    assert(standby_start != NULL);
    assert(!standby_start->loop);
    assert(standby_start->expression_count == 1);
    assert(strcmp(standby_start->expression[0].anim, "standby_start") == 0);
    assert(standby_start->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);
    assert(strcmp(standby_start->animation_complete_anim, "standby_start") == 0);
    assert(strcmp(standby_start->animation_complete_state, "standby_loop") == 0);
    assert(strcmp(standby_start->animation_failure_state, "standby_loop") == 0);

    assert(standby_end != NULL);
    assert(!standby_end->loop);
    assert(standby_end->hold_until_replaced);
    assert(standby_end->expression_count == 1);
    assert(strcmp(standby_end->expression[0].anim, "standby_end") == 0);
    assert(standby_end->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);

    assert(listening_wake != NULL);
    assert(!listening_wake->loop);
    assert(listening_wake->hold_until_replaced);
    assert(listening_wake->expression_count == 1);
    assert(strcmp(listening_wake->expression[0].anim, "standby_end") == 0);
    assert(listening_wake->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);
    assert(strcmp(listening_wake->animation_complete_anim, "standby_end") == 0);
    assert(strcmp(listening_wake->animation_complete_state, "listening") == 0);
    /* A broken sleep-out transition is a presentation-integrity failure.  It
     * must never be hidden by jumping directly to listening. */
    assert(strcmp(listening_wake->animation_failure_state, "error") == 0);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_music_expression_is_state(void) {
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *music = NULL;
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);
    music = find_state(&catalog, "music");

    assert(music != NULL);
    assert(music->loop);
    assert(music->expression_count == 1);
    assert(strcmp(music->expression[0].anim, "music") == 0);
    assert(music->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_happy_handoff_is_animation_terminal_driven(void) {
    behavior_catalog_t catalog = {0};
    const behavior_state_def_t *happy = NULL;
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);
    happy = find_state(&catalog, "happy");

    assert(happy != NULL);
    assert(!happy->loop);
    assert(happy->hold_until_replaced);
    assert(happy->expression_count == 1);
    assert(happy->expression[0].at_ms == 0U);
    assert(happy->expression[0].repeat_count == 0U);
    assert(happy->expression[0].playback_mode == ANIM_PLAYBACK_ONCE);
    assert(strcmp(happy->expression[0].anim, "happy") == 0);
    assert(strcmp(happy->animation_complete_anim, "happy") == 0);
    assert(strcmp(happy->animation_complete_state, "standby_loop") == 0);
    assert(strcmp(happy->animation_failure_state, "standby_loop") == 0);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_voice_flow_states_clear_text_and_keep_anim(void) {
    behavior_catalog_t catalog = {0};
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);
    static const char *voice_states[] = {"thinking", "processing", "speaking"};
    size_t index;

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);

    for (index = 0; index < ARRAY_SIZE(voice_states); ++index) {
        const behavior_state_def_t *state = find_state(&catalog, voice_states[index]);

        assert(state != NULL);
        assert(state->expression_count == 1);
        assert(strcmp(state->expression[0].anim, voice_states[index]) == 0);
        assert(state->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);
        assert(strcmp(state->expression[0].text, "") == 0);
    }

    const behavior_state_def_t *listening = find_state(&catalog, "listening");
    assert(listening != NULL);
    assert(listening->loop);
    assert(listening->expression_count == 1);
    assert(strcmp(listening->expression[0].anim, "listening") == 0);
    assert(listening->expression[0].playback_mode == ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);
    assert(strcmp(listening->expression[0].text, "") == 0);

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_production_animations_never_depend_on_resource_loop_fallback(void) {
    behavior_catalog_t catalog = {0};
    size_t json_len = 0;
    char *json = read_file(BEHAVIOR_PRODUCTION_STATES_JSON, &json_len);

    assert(json != NULL);
    assert(behavior_catalog_parse_json(json, json_len, NULL, NULL, &catalog) == ESP_OK);
    for (int state_index = 0; state_index < catalog.state_count; ++state_index) {
        const behavior_state_def_t *state = &catalog.states[state_index];
        for (int event_index = 0; event_index < state->expression_count; ++event_index) {
            const behavior_expression_event_t *event = &state->expression[event_index];
            if (event->anim[0] != '\0') {
                assert(event->playback_mode != ANIM_PLAYBACK_RESOURCE_DEFAULT);
            }
        }
    }

    behavior_free_catalog(&catalog);
    free(json);
}

static void test_catalog_loader_prefers_valid_sd_candidate(void) {
    const behavior_catalog_candidate_t candidates[] = {
        {BEHAVIOR_PRODUCTION_STATES_JSON, "sd"},
        {"missing-internal-states.json", "internal"},
    };
    behavior_catalog_t catalog = {0};
    size_t selected = SIZE_MAX;

    assert(behavior_catalog_load_first_valid(candidates, ARRAY_SIZE(candidates), 128U * 1024U, NULL, NULL,
                                             &catalog, &selected) == ESP_OK);
    assert(selected == 0U);
    assert(catalog.state_count > 0);
    behavior_free_catalog(&catalog);
}

static void test_catalog_loader_falls_back_when_sd_candidate_is_invalid(void) {
    static const char invalid_path[] = "behavior-invalid-sd-states.json";
    FILE *file = fopen(invalid_path, "wb");
    const behavior_catalog_candidate_t candidates[] = {
        {invalid_path, "sd"},
        {BEHAVIOR_PRODUCTION_STATES_JSON, "internal"},
    };
    behavior_catalog_t catalog = {0};
    size_t selected = SIZE_MAX;

    assert(file != NULL);
    assert(fputs("{invalid-json", file) >= 0);
    fclose(file);

    assert(behavior_catalog_load_first_valid(candidates, ARRAY_SIZE(candidates), 128U * 1024U, NULL, NULL,
                                             &catalog, &selected) == ESP_OK);
    assert(selected == 1U);
    assert(catalog.state_count > 0);
    behavior_free_catalog(&catalog);
    remove(invalid_path);
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
        {"catalog_parser_accepts_public_behavior_schema", test_catalog_parser_accepts_public_behavior_schema},
        {"catalog_parser_accepts_linear_motion_profile_override",
         test_catalog_parser_accepts_linear_motion_profile_override},
        {"catalog_parser_loads_animation_repeat_and_failure_transition",
         test_catalog_parser_loads_animation_repeat_and_failure_transition},
        {"catalog_parser_loads_explicit_animation_playback_modes",
         test_catalog_parser_loads_explicit_animation_playback_modes},
        {"catalog_parser_rejects_invalid_animation_playback_combinations",
         test_catalog_parser_rejects_invalid_animation_playback_combinations},
        {"catalog_parser_rejects_missing_animation_failure_target",
         test_catalog_parser_rejects_missing_animation_failure_target},
        {"catalog_parser_cleans_partial_psram_allocation_on_oom",
         test_catalog_parser_cleans_partial_psram_allocation_on_oom},
        {"persistent_allocator_rejects_size_overflow", test_persistent_allocator_rejects_size_overflow},
        {"catalog_parser_uses_json_length", test_catalog_parser_uses_json_length},
        {"catalog_parser_rejects_unknown_animation", test_catalog_parser_rejects_unknown_animation},
        {"catalog_parser_rejects_delayed_primary_animation", test_catalog_parser_rejects_delayed_primary_animation},
        {"catalog_parser_rejects_multiple_primary_animations", test_catalog_parser_rejects_multiple_primary_animations},
        {"catalog_parser_rejects_missing_animation_transition_target",
         test_catalog_parser_rejects_missing_animation_transition_target},
        {"catalog_parser_rejects_unowned_animation_transition",
         test_catalog_parser_rejects_unowned_animation_transition},
        {"catalog_parser_rejects_invalid_motion_angle", test_catalog_parser_rejects_invalid_motion_angle},
        {"catalog_parser_clamps_y_axis_angle", test_catalog_parser_clamps_y_axis_angle},
        {"production_fondle_love_is_one_shot", test_production_fondle_love_is_one_shot},
        {"production_recharge_is_one_shot_hold", test_production_recharge_is_one_shot_hold},
        {"production_standby_variants_are_states", test_production_standby_variants_are_states},
        {"production_voice_sleep_transitions_are_states", test_production_voice_sleep_transitions_are_states},
        {"production_music_expression_is_state", test_production_music_expression_is_state},
        {"production_happy_handoff_is_animation_terminal_driven",
         test_production_happy_handoff_is_animation_terminal_driven},
        {"production_voice_flow_states_clear_text_and_keep_anim",
         test_production_voice_flow_states_clear_text_and_keep_anim},
        {"production_animations_never_depend_on_resource_loop_fallback",
         test_production_animations_never_depend_on_resource_loop_fallback},
        {"catalog_loader_prefers_valid_sd_candidate", test_catalog_loader_prefers_valid_sd_candidate},
        {"catalog_loader_falls_back_when_sd_candidate_is_invalid",
         test_catalog_loader_falls_back_when_sd_candidate_is_invalid},
    };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
