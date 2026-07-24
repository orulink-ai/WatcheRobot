#include "ws_audio_codec_negotiation.h"

#include <string.h>

ws_audio_codec_negotiation_result_t ws_audio_codec_negotiate(const char *codec, int sample_rate, int channels,
                                                             int frame_duration_ms, const char *packetization,
                                                             int version, bool opus_available) {
    ws_audio_codec_negotiation_result_t result = {
        .codec = WS_AUDIO_CODEC_SELECTION_PCM_S16LE,
        .explicit_selection = false,
    };
    bool common_shape = sample_rate == 16000 && channels == 1 && frame_duration_ms == 60 && version == 1;

    if (codec == NULL || packetization == NULL || !common_shape) {
        return result;
    }
    if (strcmp(codec, "opus") == 0 && strcmp(packetization, "one_opus_packet_per_wspk") == 0) {
        if (opus_available) {
            result.codec = WS_AUDIO_CODEC_SELECTION_OPUS;
            result.explicit_selection = true;
        }
        return result;
    }
    if (strcmp(codec, "pcm_s16le") == 0 && strcmp(packetization, "pcm_s16le_stream") == 0) {
        result.explicit_selection = true;
    }
    return result;
}
