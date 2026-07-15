#include "realtime_client.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static void test_session_update_contains_required_realtime_fields(void) {
    char json[1024];
    realtime_client_config_t config = {
        .voice = "Aiden",
        .instructions = "Answer briefly.",
    };

    expect_true(realtime_client_build_session_update_for_test(&config, json, sizeof(json)) == ESP_OK,
                "session.update builds");
    expect_true(strstr(json, "\"type\":\"session.update\"") != NULL, "session.update type present");
    expect_true(strstr(json, "\"voice\":\"Aiden\"") != NULL, "voice present");
    expect_true(strstr(json, "\"input\":{\"format\"") == NULL, "input audio format stays server default");
    expect_true(strstr(json, "\"output\":{\"voice\":\"Aiden\",\"format\":{\"type\":\"audio/pcm\",\"rate\":24000}}") !=
                    NULL,
                "output audio format declares 24k PCM");
    expect_true(strstr(json, "\"turn_detection\":{\"type\":\"server_vad\"}") != NULL, "server_vad present");
    expect_true(strstr(json, "\"input_audio_transcription\":{\"model\":\"local\"}") != NULL,
                "local transcription present");
}

static void test_session_update_escapes_strings(void) {
    char json[1024];
    realtime_client_config_t config = {
        .voice = "Ai\"den",
        .instructions = "Line one\nLine \"two\".",
    };

    expect_true(realtime_client_build_session_update_for_test(&config, json, sizeof(json)) == ESP_OK,
                "escaped session.update builds");
    expect_true(strstr(json, "Line one\\nLine \\\"two\\\".") != NULL, "instructions escaped");
    expect_true(strstr(json, "\"voice\":\"Ai\\\"den\"") != NULL, "voice escaped");
}

static void test_audio_append_base64_encodes_pcm(void) {
    const unsigned char pcm[] = {0x01, 0x02, 0x03, 0x04};
    char json[256];

    expect_true(realtime_client_build_audio_append_for_test(pcm, sizeof(pcm), json, sizeof(json)) == ESP_OK,
                "audio append builds");
    expect_true(strstr(json, "\"type\":\"input_audio_buffer.append\"") != NULL, "audio append type present");
    expect_true(strstr(json, "\"audio\":\"AQIDBA==\"") != NULL, "PCM base64 present");
}

static void test_audio_append_rejects_oversized_frame(void) {
    unsigned char pcm[1921] = {0};
    char json[2800];

    expect_true(realtime_client_build_audio_append_for_test(pcm, sizeof(pcm), json, sizeof(json)) ==
                    ESP_ERR_INVALID_SIZE,
                "oversized PCM frame rejected");
}

static void test_event_type_extraction(void) {
    char type[96];

    expect_true(realtime_client_parse_event_type_for_test("{\"type\":\"response.done\"}", type, sizeof(type)) == ESP_OK,
                "event type parses");
    expect_true(strcmp(type, "response.done") == 0, "event type value");
}

static void test_audio_delta_decodes(void) {
    unsigned char decoded[8] = {0};
    size_t decoded_len = 0;

    expect_true(realtime_client_decode_audio_delta_for_test("{\"type\":\"response.output_audio.delta\","
                                                            "\"delta\":\"AQIDBA==\"}",
                                                            decoded, sizeof(decoded), &decoded_len) == ESP_OK,
                "audio delta decodes");
    expect_true(decoded_len == 4, "decoded length");
    expect_true(decoded[0] == 1 && decoded[1] == 2 && decoded[2] == 3 && decoded[3] == 4, "decoded bytes");
}

int main(void) {
    test_session_update_contains_required_realtime_fields();
    test_session_update_escapes_strings();
    test_audio_append_base64_encodes_pcm();
    test_audio_append_rejects_oversized_frame();
    test_event_type_extraction();
    test_audio_delta_decodes();

    if (failures != 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    return 0;
}
