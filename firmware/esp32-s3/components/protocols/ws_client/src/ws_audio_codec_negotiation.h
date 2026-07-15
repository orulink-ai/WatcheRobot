#ifndef WS_AUDIO_CODEC_NEGOTIATION_H
#define WS_AUDIO_CODEC_NEGOTIATION_H

#include <stdbool.h>

typedef enum {
    WS_AUDIO_CODEC_SELECTION_PCM_S16LE = 0,
    WS_AUDIO_CODEC_SELECTION_OPUS = 1,
} ws_audio_codec_selection_t;

typedef struct {
    ws_audio_codec_selection_t codec;
    bool explicit_selection;
} ws_audio_codec_negotiation_result_t;

ws_audio_codec_negotiation_result_t ws_audio_codec_negotiate(
    const char *codec, int sample_rate, int channels, int frame_duration_ms,
    const char *packetization, int version, bool opus_available);

#endif
