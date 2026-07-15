#include "ws_audio_uplink_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_waits_for_second_frame_within_latency_budget(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(!ws_audio_uplink_should_flush(&policy, 1U, false, WS_AUDIO_UPLINK_MAX_WAIT_US - 1U));
    assert(ws_audio_uplink_should_flush(&policy, 2U, false, 0U));
}

static void test_flushes_single_tail_frame_on_end(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_should_flush(&policy, 1U, true, 0U));
}

static void test_flushes_stalled_single_frame_after_deadline(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_should_flush(&policy, 1U, false, WS_AUDIO_UPLINK_MAX_WAIT_US));
}

static void test_never_flushes_an_empty_queue(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(!ws_audio_uplink_should_flush(&policy, 0U, true, WS_AUDIO_UPLINK_MAX_WAIT_US));
}

static void test_batch_size_is_bounded(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_batch_frames(&policy, 0U, false, 0U) == 0U);
    assert(ws_audio_uplink_batch_frames(&policy, 1U, false, 0U) == 1U);
    assert(ws_audio_uplink_batch_frames(&policy, 2U, false, 0U) == WS_AUDIO_UPLINK_BASE_BATCH_FRAMES);
    assert(ws_audio_uplink_batch_frames(&policy, 3U, false, 0U) == WS_AUDIO_UPLINK_BASE_BATCH_FRAMES);
    assert(ws_audio_uplink_batch_frames(&policy, 32U, false, WS_AUDIO_UPLINK_PRESSURE_AGE_US) ==
           WS_AUDIO_UPLINK_MAX_BATCH_FRAMES);
}

static void test_high_watermark_increases_batch_size(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_batch_frames(&policy, WS_AUDIO_UPLINK_HIGH_WATER_FRAMES, false, 0U) ==
           WS_AUDIO_UPLINK_MAX_BATCH_FRAMES);
}

static void test_old_audio_increases_batch_size_before_queue_is_full(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_batch_frames(&policy, WS_AUDIO_UPLINK_MAX_BATCH_FRAMES, false,
                                        WS_AUDIO_UPLINK_PRESSURE_AGE_US) ==
           WS_AUDIO_UPLINK_MAX_BATCH_FRAMES);
}

static void test_slow_send_enters_pressure_and_flushes_three_frames(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    ws_audio_uplink_observe_send(&policy, 2U, 3U, 60000U, 200000U);
    assert(policy.pressure);
    assert(ws_audio_uplink_should_flush(&policy, 1U, false, 0U));
    assert(ws_audio_uplink_batch_frames(&policy, 3U, false, 0U) == 3U);
}

static void test_policy_returns_after_two_realtime_samples(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    ws_audio_uplink_observe_send(&policy, 2U, 4U, 60000U, 200000U);
    assert(policy.pressure);
    ws_audio_uplink_observe_send(&policy, 4U, 2U, 60000U, 100000U);
    assert(policy.pressure);
    ws_audio_uplink_observe_send(&policy, 2U, 0U, 60000U, 50000U);
    assert(!policy.pressure);
    assert(ws_audio_uplink_batch_frames(&policy, WS_AUDIO_UPLINK_MAX_BATCH_FRAMES - 1U, false, 0U) ==
           WS_AUDIO_UPLINK_BASE_BATCH_FRAMES);
}

static void test_end_drains_up_to_transport_maximum(void) {
    ws_audio_uplink_policy_t policy;
    ws_audio_uplink_policy_reset(&policy);
    assert(ws_audio_uplink_batch_frames(&policy, 3U, true, 0U) == 3U);
    assert(ws_audio_uplink_batch_frames(&policy, 8U, true, 0U) == WS_AUDIO_UPLINK_MAX_BATCH_FRAMES);
}

static void test_batch_two_requires_send_to_finish_inside_realtime_budget(void) {
    assert(ws_audio_uplink_can_keep_up(WS_AUDIO_UPLINK_BASE_BATCH_FRAMES, 60000U, 90000U));
    assert(!ws_audio_uplink_can_keep_up(WS_AUDIO_UPLINK_BASE_BATCH_FRAMES, 60000U, 200000U));
}

int main(void) {
    test_waits_for_second_frame_within_latency_budget();
    test_flushes_single_tail_frame_on_end();
    test_flushes_stalled_single_frame_after_deadline();
    test_never_flushes_an_empty_queue();
    test_batch_size_is_bounded();
    test_high_watermark_increases_batch_size();
    test_old_audio_increases_batch_size_before_queue_is_full();
    test_slow_send_enters_pressure_and_flushes_three_frames();
    test_policy_returns_after_two_realtime_samples();
    test_end_drains_up_to_transport_maximum();
    test_batch_two_requires_send_to_finish_inside_realtime_budget();
    puts("ws audio uplink policy host tests passed");
    return 0;
}
