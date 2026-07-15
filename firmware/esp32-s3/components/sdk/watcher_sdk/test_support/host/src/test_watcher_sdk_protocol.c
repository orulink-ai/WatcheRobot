#include "watcher_sdk_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parse_authenticate(void) {
    static const char json[] =
        "{\"type\":\"sys.sdk.authenticate\",\"code\":0,\"data\":{"
        "\"command_id\":\"auth-1\",\"pairing_code\":\"123456\","
        "\"protocol_version\":\"1.0\",\"client_name\":\"pytest\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_OK);
    assert(command.type == WATCHER_SDK_PROTOCOL_AUTHENTICATE);
    assert(strcmp(command.command_id, "auth-1") == 0);
    assert(strcmp(command.data.authenticate.pairing_code, "123456") == 0);
    assert(strcmp(command.data.authenticate.protocol_version, "1.0") == 0);
}

static void test_parse_behavior_play(void) {
    static const char json[] =
        "{\"type\":\"ctrl.behavior.play\",\"code\":0,\"data\":{"
        "\"command_id\":\"cmd-1\",\"behavior_id\":\"greeting\",\"repeat\":1}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_OK);
    assert(command.type == WATCHER_SDK_PROTOCOL_BEHAVIOR_PLAY);
    assert(strcmp(command.data.behavior_play.behavior_id, "greeting") == 0);
    assert(command.data.behavior_play.repeat_count == 1U);
}

static void test_parse_motion_move_to(void) {
    static const char json[] =
        "{\"type\":\"ctrl.motion.move_to\",\"code\":0,\"data\":{"
        "\"command_id\":\"cmd-2\",\"pan_deg\":110,\"tilt_deg\":120,"
        "\"duration_ms\":500,\"profile\":\"ease_in_out\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_OK);
    assert(command.type == WATCHER_SDK_PROTOCOL_MOTION_MOVE_TO);
    assert(command.data.motion.pan_deg == 110);
    assert(command.data.motion.tilt_deg == 120);
    assert(command.data.motion.duration_ms == 500U);
    assert(command.data.motion.ease_in_out);
}

static void test_parse_light_color(void) {
    static const char json[] =
        "{\"type\":\"ctrl.light.set\",\"code\":0,\"data\":{"
        "\"command_id\":\"cmd-3\",\"color\":\"#4DA3FF\",\"brightness\":0.7,\"zone\":\"all\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_OK);
    assert(command.type == WATCHER_SDK_PROTOCOL_LIGHT_SET);
    assert(command.data.light.red == 0x4D);
    assert(command.data.light.green == 0xA3);
    assert(command.data.light.blue == 0xFF);
    assert(command.data.light.brightness_percent == 70U);
}

static void test_parse_audio_stream_begin(void) {
    static const char json[] =
        "{\"type\":\"ctrl.audio.stream.begin\",\"code\":0,\"data\":{"
        "\"command_id\":\"audio-1\",\"stream_id\":7,\"total_bytes\":48000,"
        "\"sample_rate_hz\":24000,\"channels\":1,\"sample_width_bytes\":2,"
        "\"audio_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_OK);
    assert(command.type == WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN);
    assert(command.data.audio_stream.stream_id == 7U);
    assert(command.data.audio_stream.total_bytes == 48000U);
    assert(strcmp(command.data.audio_stream.audio_sha256,
                  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") == 0);
}

static void test_rejects_missing_command_id(void) {
    static const char json[] =
        "{\"type\":\"ctrl.audio.play\",\"code\":0,\"data\":{\"sound_id\":\"confirm\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
}

static void test_rejects_truncated_external_identifiers(void) {
    static const char long_command_id_json[] =
        "{\"type\":\"ctrl.audio.play\",\"code\":0,\"data\":{"
        "\"command_id\":\"123456789012345678901234567890123456789012345678\","
        "\"sound_id\":\"confirm\"}}";
    static const char long_resource_id_json[] =
        "{\"type\":\"ctrl.audio.play\",\"code\":0,\"data\":{"
        "\"command_id\":\"cmd-long-resource\","
        "\"sound_id\":\"1234567890123456789012345678901234567890123456789012345678901234\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(long_command_id_json, strlen(long_command_id_json), &command) ==
           WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
    assert(watcher_sdk_protocol_parse(long_resource_id_json, strlen(long_resource_id_json), &command) ==
           WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
}

static void test_rejects_pairing_code_prefix_attack(void) {
    static const char json[] =
        "{\"type\":\"sys.sdk.authenticate\",\"code\":0,\"data\":{"
        "\"command_id\":\"auth-prefix\",\"pairing_code\":\"123456-extra\","
        "\"protocol_version\":\"1.0\",\"client_name\":\"pytest\"}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(json, strlen(json), &command) == WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
}

static void test_rejects_invalid_optional_values_instead_of_defaulting(void) {
    static const char invalid_repeat[] =
        "{\"type\":\"ctrl.behavior.play\",\"code\":0,\"data\":{"
        "\"command_id\":\"bad-repeat\",\"behavior_id\":\"greeting\",\"repeat\":0}}";
    static const char invalid_brightness[] =
        "{\"type\":\"ctrl.light.set\",\"code\":0,\"data\":{"
        "\"command_id\":\"bad-brightness\",\"color\":\"#4DA3FF\",\"brightness\":1.1}}";
    static const char invalid_sample_rate[] =
        "{\"type\":\"ctrl.microphone.open\",\"code\":0,\"data\":{"
        "\"command_id\":\"bad-sample-rate\",\"sample_rate_hz\":8000}}";
    watcher_sdk_protocol_command_t command = {0};

    assert(watcher_sdk_protocol_parse(invalid_repeat, strlen(invalid_repeat), &command) ==
           WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
    assert(watcher_sdk_protocol_parse(invalid_brightness, strlen(invalid_brightness), &command) ==
           WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
    assert(watcher_sdk_protocol_parse(invalid_sample_rate, strlen(invalid_sample_rate), &command) ==
           WATCHER_SDK_PROTOCOL_INVALID_ARGUMENT);
}

static void test_type_filter_does_not_capture_legacy_control_routes(void) {
    assert(watcher_sdk_protocol_supports_type("sys.sdk.authenticate"));
    assert(watcher_sdk_protocol_supports_type("ctrl.camera.capture"));
    assert(watcher_sdk_protocol_supports_type("ctrl.motion.move_to"));
    assert(!watcher_sdk_protocol_supports_type("ctrl.camera.capture_image"));
    assert(!watcher_sdk_protocol_supports_type("ctrl.servo.angle"));
    assert(!watcher_sdk_protocol_supports_type("ctrl.robot.state.set"));
}

static void test_builds_operation_event(void) {
    char json[256];
    watcher_sdk_event_t event = {
        .job_id = 42U,
        .domain = WATCHER_SDK_DOMAIN_MOTION,
        .state = WATCHER_SDK_JOB_COMPLETED,
    };

    assert(watcher_sdk_protocol_build_operation_event(&event, json, sizeof(json)) == WATCHER_SDK_PROTOCOL_OK);
    assert(strstr(json, "\"type\":\"evt.sdk.operation\"") != NULL);
    assert(strstr(json, "\"operation_id\":42") != NULL);
    assert(strstr(json, "\"state\":\"completed\"") != NULL);
}

static void test_ready_advertises_host_audio_streaming(void) {
    char json[768];

    assert(watcher_sdk_protocol_build_ready("watcher-test", "test", json, sizeof(json)) ==
           WATCHER_SDK_PROTOCOL_OK);
    assert(strstr(json, "\"audio.stream\"") != NULL);
}

int main(void) {
    test_parse_authenticate();
    test_parse_behavior_play();
    test_parse_motion_move_to();
    test_parse_light_color();
    test_parse_audio_stream_begin();
    test_rejects_missing_command_id();
    test_rejects_truncated_external_identifiers();
    test_rejects_pairing_code_prefix_attack();
    test_rejects_invalid_optional_values_instead_of_defaulting();
    test_type_filter_does_not_capture_legacy_control_routes();
    test_builds_operation_event();
    test_ready_advertises_host_audio_streaming();
    puts("watcher_sdk_protocol_host_tests: PASS");
    return 0;
}
