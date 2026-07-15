#include "ws_tts_buffer_policy.h"

#include <assert.h>
#include <stdio.h>

#define FRAME_BYTES 4096U
#define BYTES_PER_SECOND 48000U
#define START_BYTES (16U * FRAME_BYTES)
#define REBUFFER_BYTES (12U * FRAME_BYTES)

static void test_fragmented_chunks_use_real_pending_bytes(void) {
    ws_tts_buffer_policy_t policy;

    ws_tts_buffer_policy_reset(&policy);
    for (int i = 0; i < 4; ++i) {
        ws_tts_buffer_policy_enqueue(&policy, 1024U);
    }

    assert(policy.pending_bytes == 4096U);
    assert(ws_tts_buffer_policy_pending_ms(&policy, BYTES_PER_SECOND) == 85U);
    assert(!ws_tts_buffer_policy_ready(&policy, false, false, START_BYTES, REBUFFER_BYTES));

    for (uint32_t i = 0; i < (START_BYTES - FRAME_BYTES) / 1024U; ++i) {
        ws_tts_buffer_policy_enqueue(&policy, 1024U);
    }
    assert(policy.pending_bytes == START_BYTES);
    assert(ws_tts_buffer_policy_ready(&policy, false, false, START_BYTES, REBUFFER_BYTES));
}

static void test_starvation_requires_rebuffer_before_resume(void) {
    ws_tts_buffer_policy_t policy;

    ws_tts_buffer_policy_reset(&policy);
    ws_tts_buffer_policy_mark_starved(&policy);
    ws_tts_buffer_policy_enqueue(&policy, REBUFFER_BYTES - 1U);

    assert(!ws_tts_buffer_policy_ready(&policy, true, false, START_BYTES, REBUFFER_BYTES));
    ws_tts_buffer_policy_enqueue(&policy, 1U);
    assert(ws_tts_buffer_policy_ready(&policy, true, false, START_BYTES, REBUFFER_BYTES));
    ws_tts_buffer_policy_mark_resumed(&policy);
    assert(!policy.rebuffering);
}

static void test_end_pending_drains_short_tail(void) {
    ws_tts_buffer_policy_t policy;

    ws_tts_buffer_policy_reset(&policy);
    ws_tts_buffer_policy_mark_starved(&policy);
    ws_tts_buffer_policy_enqueue(&policy, 1024U);

    assert(ws_tts_buffer_policy_ready(&policy, true, true, START_BYTES, REBUFFER_BYTES));
    ws_tts_buffer_policy_dequeue(&policy, 1024U);
    assert(policy.pending_bytes == 0U);
}

static void test_dequeue_saturates_instead_of_underflowing(void) {
    ws_tts_buffer_policy_t policy;

    ws_tts_buffer_policy_reset(&policy);
    ws_tts_buffer_policy_enqueue(&policy, 100U);
    ws_tts_buffer_policy_dequeue(&policy, 200U);

    assert(policy.pending_bytes == 0U);
}

static void test_finish_waits_for_network_eos_queue_and_inflight(void) {
    assert(!ws_tts_buffer_policy_should_finish(false, 0U, false));
    assert(!ws_tts_buffer_policy_should_finish(true, 1U, false));
    assert(!ws_tts_buffer_policy_should_finish(true, 0U, true));
    assert(ws_tts_buffer_policy_should_finish(true, 0U, false));
}

int main(void) {
    test_fragmented_chunks_use_real_pending_bytes();
    test_starvation_requires_rebuffer_before_resume();
    test_end_pending_drains_short_tail();
    test_dequeue_saturates_instead_of_underflowing();
    test_finish_waits_for_network_eos_queue_and_inflight();
    puts("ws tts buffer policy host tests passed");
    return 0;
}
