#include "ws_audio_codec_negotiation.h"

#include <assert.h>
#include <stdio.h>

static void test_exact_opus_selection(void) {
    ws_audio_codec_negotiation_result_t result =
        ws_audio_codec_negotiate("opus", 16000, 1, 60, "one_opus_packet_per_wspk", 1, true);
    assert(result.explicit_selection);
    assert(result.codec == WS_AUDIO_CODEC_SELECTION_OPUS);
}

static void test_opus_without_encoder_falls_back_to_pcm(void) {
    ws_audio_codec_negotiation_result_t result =
        ws_audio_codec_negotiate("opus", 16000, 1, 60, "one_opus_packet_per_wspk", 1, false);
    assert(!result.explicit_selection);
    assert(result.codec == WS_AUDIO_CODEC_SELECTION_PCM_S16LE);
}

static void test_legacy_or_malformed_ack_never_enables_opus(void) {
    ws_audio_codec_negotiation_result_t missing = ws_audio_codec_negotiate(NULL, 0, 0, 0, NULL, 0, true);
    ws_audio_codec_negotiation_result_t concatenated =
        ws_audio_codec_negotiate("opus", 16000, 1, 60, "concatenated", 1, true);
    assert(!missing.explicit_selection);
    assert(missing.codec == WS_AUDIO_CODEC_SELECTION_PCM_S16LE);
    assert(!concatenated.explicit_selection);
    assert(concatenated.codec == WS_AUDIO_CODEC_SELECTION_PCM_S16LE);
}

static void test_exact_pcm_selection(void) {
    ws_audio_codec_negotiation_result_t result =
        ws_audio_codec_negotiate("pcm_s16le", 16000, 1, 60, "pcm_s16le_stream", 1, true);
    assert(result.explicit_selection);
    assert(result.codec == WS_AUDIO_CODEC_SELECTION_PCM_S16LE);
}

int main(void) {
    test_exact_opus_selection();
    test_opus_without_encoder_falls_back_to_pcm();
    test_legacy_or_malformed_ack_never_enables_opus();
    test_exact_pcm_selection();
    puts("ws_audio_codec_negotiation host tests passed");
    return 0;
}
