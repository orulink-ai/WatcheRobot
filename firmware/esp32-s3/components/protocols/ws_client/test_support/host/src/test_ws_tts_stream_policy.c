#include "ws_tts_stream_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_newer_stream_after_eos_continues_active_playout(void) {
    assert(ws_tts_stream_should_continue(19U, 20U, true, true, 2U, false));
    assert(ws_tts_stream_should_continue(20U, 21U, true, false, 0U, true));
}

static void test_non_eos_or_inactive_stream_replaces(void) {
    assert(!ws_tts_stream_should_continue(19U, 20U, false, true, 2U, false));
    assert(!ws_tts_stream_should_continue(19U, 20U, true, false, 0U, false));
    assert(!ws_tts_stream_should_continue(19U, 19U, true, true, 2U, false));
    assert(!ws_tts_stream_should_continue(20U, 19U, true, true, 2U, false));
}

int main(void) {
    test_newer_stream_after_eos_continues_active_playout();
    test_non_eos_or_inactive_stream_replaces();
    puts("ws TTS stream policy host tests passed");
    return 0;
}
