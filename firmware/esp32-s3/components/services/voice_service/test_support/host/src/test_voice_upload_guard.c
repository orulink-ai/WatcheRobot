#include "voice_upload_guard.h"

#include <assert.h>
#include <stdio.h>

static void test_cloud_loss_aborts_immediately(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 0U);
    assert(voice_upload_guard_observe(&guard, false, true, 0U) == VOICE_UPLOAD_ABORT_CLOUD_LOST);
}

static void test_transient_enqueue_failure_recovers(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 0U);
    assert(voice_upload_guard_observe(&guard, true, false, 0U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, true, 0U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, false, 0U) == VOICE_UPLOAD_CONTINUE);
}

static void test_consecutive_enqueue_failures_abort(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 0U);
    assert(voice_upload_guard_observe(&guard, true, false, 0U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, false, 0U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, false, 0U) == VOICE_UPLOAD_ABORT_SEND_FAILURES);
}

static void test_sustained_queue_drops_abort(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 10U);
    for (uint32_t i = 0U; i < VOICE_UPLOAD_DROP_GRACE_OBSERVATIONS; ++i) {
        assert(voice_upload_guard_observe(&guard, true, true, 10U) == VOICE_UPLOAD_CONTINUE);
    }
    assert(voice_upload_guard_observe(&guard, true, true, 14U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, true, 17U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, true, 18U) == VOICE_UPLOAD_ABORT_QUEUE_OVERRUN);
}

static void test_drop_grace_is_shorter_than_available_audio_queue_headroom(void) {
    /* Eight queued 60 ms frames provide only about 480 ms of headroom.  A
     * multi-second grace would hide the exact overflow this guard protects. */
    assert(VOICE_UPLOAD_DROP_GRACE_OBSERVATIONS <= 10U);
}

static void test_startup_queue_drops_do_not_abort_recording(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 100U);
    for (uint32_t i = 0U; i < VOICE_UPLOAD_DROP_GRACE_OBSERVATIONS; ++i) {
        assert(voice_upload_guard_observe(&guard, true, true, 101U + i) == VOICE_UPLOAD_CONTINUE);
    }
    assert(guard.dropped_frames_since_start == 0U);
}

static void test_drop_counter_reset_does_not_underflow(void) {
    voice_upload_guard_t guard;

    voice_upload_guard_reset(&guard, 20U);
    assert(voice_upload_guard_observe(&guard, true, true, 2U) == VOICE_UPLOAD_CONTINUE);
    assert(voice_upload_guard_observe(&guard, true, true, 9U) == VOICE_UPLOAD_CONTINUE);
}

int main(void) {
    test_cloud_loss_aborts_immediately();
    test_transient_enqueue_failure_recovers();
    test_consecutive_enqueue_failures_abort();
    test_sustained_queue_drops_abort();
    test_drop_grace_is_shorter_than_available_audio_queue_headroom();
    test_startup_queue_drops_do_not_abort_recording();
    test_drop_counter_reset_does_not_underflow();
    puts("voice upload guard host tests passed");
    return 0;
}
