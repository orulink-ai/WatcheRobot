#include "realtime_frame_assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_assembles_fragmented_json(void) {
    char buffer[64];
    realtime_frame_assembler_t assembler;
    const char *complete = NULL;
    const char *first = "{\"type\":\"";
    const char *second = "session.created\"}";
    size_t payload_len = strlen(first) + strlen(second);

    realtime_frame_assembler_init(&assembler, buffer, sizeof(buffer));
    expect_true(realtime_frame_assembler_append(&assembler, first, strlen(first), payload_len, 0, true, true, false,
                                                &complete) == ESP_OK,
                "first fragment accepted");
    expect_true(complete == NULL, "first fragment incomplete");
    expect_true(realtime_frame_assembler_append(&assembler, second, strlen(second), payload_len, strlen(first) + 1, true,
                                                false, false, &complete) != ESP_OK,
                "gap is rejected");
    realtime_frame_assembler_reset(&assembler);
    expect_true(realtime_frame_assembler_append(&assembler, first, strlen(first), payload_len, 0, true, true, false,
                                                &complete) == ESP_OK,
                "restart first accepted");
    expect_true(realtime_frame_assembler_append(&assembler, second, strlen(second), payload_len, strlen(first), true,
                                                false, false, &complete) == ESP_OK,
                "second fragment accepted");
    expect_true(complete != NULL, "complete returned");
    expect_true(strcmp(complete, "{\"type\":\"session.created\"}") == 0, "json reconstructed");
}

static void test_assembles_buffer_split_single_frame(void) {
    char buffer[64];
    realtime_frame_assembler_t assembler;
    const char *complete = NULL;
    const char *first = "{\"type\":\"";
    const char *second = "session.created\"}";
    size_t payload_len = strlen(first) + strlen(second);

    realtime_frame_assembler_init(&assembler, buffer, sizeof(buffer));
    expect_true(realtime_frame_assembler_append(&assembler, first, strlen(first), payload_len, 0, true, true, false,
                                                &complete) == ESP_OK,
                "single-frame first chunk accepted");
    expect_true(complete == NULL, "single-frame first chunk incomplete");
    expect_true(realtime_frame_assembler_append(&assembler, second, strlen(second), payload_len, strlen(first), true,
                                                false, false, &complete) == ESP_OK,
                "single-frame second chunk accepted");
    expect_true(complete != NULL, "single-frame complete returned");
    expect_true(strcmp(complete, "{\"type\":\"session.created\"}") == 0, "single-frame json reconstructed");
}

static void test_assembles_websocket_continuation_frames(void) {
    char buffer[128];
    realtime_frame_assembler_t assembler;
    const char *complete = NULL;
    const char *first_frame = "{\"type\":\"response.output_audio.";
    const char *cont_a = "delta\",\"delta\":\"";
    const char *cont_b = "AAAA\"}";
    size_t cont_payload_len = strlen(cont_a) + strlen(cont_b);

    realtime_frame_assembler_init(&assembler, buffer, sizeof(buffer));
    expect_true(realtime_frame_assembler_append(&assembler, first_frame, strlen(first_frame), strlen(first_frame), 0,
                                                false, true, false, &complete) == ESP_OK,
                "text start frame accepted");
    expect_true(complete == NULL, "text start frame incomplete");
    expect_true(realtime_frame_assembler_append(&assembler, cont_a, strlen(cont_a), cont_payload_len, 0, true, false,
                                                true, &complete) == ESP_OK,
                "continuation first chunk accepted");
    expect_true(complete == NULL, "continuation first chunk incomplete");
    expect_true(realtime_frame_assembler_append(&assembler, cont_b, strlen(cont_b), cont_payload_len, strlen(cont_a),
                                                true, false, false, &complete) == ESP_OK,
                "continuation second chunk accepted");
    expect_true(complete != NULL, "continuation complete returned");
    expect_true(strcmp(complete, "{\"type\":\"response.output_audio.delta\",\"delta\":\"AAAA\"}") == 0,
                "continuation json reconstructed");
}

static void test_rejects_oversize_payload(void) {
    char buffer[8];
    realtime_frame_assembler_t assembler;
    const char *complete = NULL;

    realtime_frame_assembler_init(&assembler, buffer, sizeof(buffer));
    expect_true(realtime_frame_assembler_append(&assembler, "{\"", 2, 2, 0, false, true, false, &complete) == ESP_OK,
                "oversize starts below capacity");
    expect_true(realtime_frame_assembler_append(&assembler, "01234567", 8, 8, 0, true, false, true, &complete) ==
                    ESP_ERR_NO_MEM,
                "oversize payload rejected");
    expect_true(complete == NULL, "oversize has no complete frame");
}

int main(void) {
    test_assembles_fragmented_json();
    test_assembles_buffer_split_single_frame();
    test_assembles_websocket_continuation_frames();
    test_rejects_oversize_payload();
    printf("realtime frame assembler tests passed\n");
    return 0;
}
