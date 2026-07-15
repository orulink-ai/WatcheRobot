#include "agent_audio_processing.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int16_t read_le_i16(const uint8_t *pcm) {
    uint16_t raw = (uint16_t)pcm[0] | ((uint16_t)pcm[1] << 8);
    return (int16_t)raw;
}

static void write_le_i16(uint8_t *pcm, int16_t sample) {
    pcm[0] = (uint8_t)((uint16_t)sample & 0xffU);
    pcm[1] = (uint8_t)(((uint16_t)sample >> 8) & 0xffU);
}

static void test_gain_amplifies_signed_pcm(void) {
    uint8_t pcm[4];

    write_le_i16(&pcm[0], 1000);
    write_le_i16(&pcm[2], -1000);

    agent_audio_apply_gain_q8(pcm, sizeof(pcm), 362);

    expect_true(read_le_i16(&pcm[0]) == 1414, "positive PCM sample is amplified by about +3 dB");
    expect_true(read_le_i16(&pcm[2]) == -1414, "negative PCM sample is amplified by about +3 dB");
}

static void test_gain_clips_to_int16_range(void) {
    uint8_t pcm[4];

    write_le_i16(&pcm[0], 30000);
    write_le_i16(&pcm[2], -30000);

    agent_audio_apply_gain_q8(pcm, sizeof(pcm), 362);

    expect_true(read_le_i16(&pcm[0]) == INT16_MAX, "positive overflow clips to INT16_MAX");
    expect_true(read_le_i16(&pcm[2]) == INT16_MIN, "negative overflow clips to INT16_MIN");
}

static void test_gain_leaves_odd_tail_byte_unchanged(void) {
    uint8_t pcm[3];

    write_le_i16(&pcm[0], 1000);
    pcm[2] = 0x7a;

    agent_audio_apply_gain_q8(pcm, sizeof(pcm), 362);

    expect_true(read_le_i16(&pcm[0]) == 1414, "complete sample is processed");
    expect_true(pcm[2] == 0x7a, "odd trailing byte is unchanged");
}

static void test_unity_gain_is_noop(void) {
    uint8_t pcm[2];

    write_le_i16(&pcm[0], 1234);
    agent_audio_apply_gain_q8(pcm, sizeof(pcm), 256);

    expect_true(read_le_i16(&pcm[0]) == 1234, "unity gain leaves sample unchanged");
}

int main(void) {
    test_gain_amplifies_signed_pcm();
    test_gain_clips_to_int16_range();
    test_gain_leaves_odd_tail_byte_unchanged();
    test_unity_gain_is_noop();

    if (failures != 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    return 0;
}
