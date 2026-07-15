#include "agent_audio_processing.h"

#include <limits.h>

static int16_t agent_audio_clip_i16(int32_t value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

void agent_audio_apply_gain_q8(uint8_t *pcm, size_t len, int gain_q8) {
    if (pcm == NULL || len < 2 || gain_q8 <= 0 || gain_q8 == 256) {
        return;
    }

    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t raw = (uint16_t)pcm[i] | ((uint16_t)pcm[i + 1] << 8);
        int16_t sample = (int16_t)raw;
        int32_t scaled = (int32_t)sample * gain_q8;

        scaled += scaled >= 0 ? 128 : -128;
        scaled /= 256;

        sample = agent_audio_clip_i16(scaled);
        pcm[i] = (uint8_t)((uint16_t)sample & 0xffU);
        pcm[i + 1] = (uint8_t)(((uint16_t)sample >> 8) & 0xffU);
    }
}
