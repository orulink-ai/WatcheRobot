#include "anim_player_event_queue.h"
#include "anim_player_lifecycle.h"

#include <stdio.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int test_single_frame_non_loop_completes_after_commit(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    events = anim_player_lifecycle_begin_prepare(&lifecycle, 7U, 107U, 31);
    failures += expect_true(events.count == 1U, "prepare emits one event");
    failures += expect_true(events.items[0].type == ANIM_PLAYER_EVENT_PREPARING, "prepare event type");

    events = anim_player_lifecycle_commit(&lifecycle, 7U, 107U, 31, 1U, false);
    failures += expect_true(events.count == 2U, "single-frame commit emits commit and completion");
    failures += expect_true(events.items[0].type == ANIM_PLAYER_EVENT_COMMITTED, "commit event is first");
    failures += expect_true(events.items[1].type == ANIM_PLAYER_EVENT_COMPLETED, "completion event is second");
    failures += expect_true(events.items[0].ticket == 107U && events.items[1].ticket == 107U,
                            "commit lifecycle preserves ticket");

    anim_player_lifecycle_snapshot_t snapshot = anim_player_lifecycle_snapshot(&lifecycle);
    failures += expect_true(snapshot.phase == ANIM_PLAYER_PHASE_COMPLETED, "single frame is completed");
    failures += expect_true(snapshot.visible_generation == 7U, "committed generation becomes visible");
    return failures;
}

static int test_multi_frame_non_loop_completes_on_final_frame(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 9U, 109U, 4);
    events = anim_player_lifecycle_commit(&lifecycle, 9U, 109U, 4, 3U, false);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_COMMITTED,
                            "multi-frame commit does not complete early");
    events = anim_player_lifecycle_note_frame(&lifecycle, 9U, 109U, 1U, 3U, false);
    failures += expect_true(events.count == 0U, "intermediate frame emits no lifecycle event");
    events = anim_player_lifecycle_note_frame(&lifecycle, 9U, 109U, 2U, 3U, false);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_COMPLETED,
                            "final frame completes non-loop animation");
    events = anim_player_lifecycle_note_frame(&lifecycle, 9U, 109U, 2U, 3U, false);
    failures += expect_true(events.count == 0U, "completion is emitted exactly once");
    return failures;
}

static int test_loop_emits_cycle_without_completing(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 11U, 111U, 7);
    (void)anim_player_lifecycle_commit(&lifecycle, 11U, 111U, 7, 2U, true);
    events = anim_player_lifecycle_note_frame(&lifecycle, 11U, 111U, 1U, 2U, true);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_CYCLE_COMPLETED,
                            "last drawn loop frame completes cycle");
    events = anim_player_lifecycle_note_frame(&lifecycle, 11U, 111U, 0U, 2U, true);
    failures += expect_true(events.count == 0U, "wrap to first frame does not complete the prior cycle late");
    failures += expect_true(anim_player_lifecycle_snapshot(&lifecycle).phase == ANIM_PLAYER_PHASE_PLAYING,
                            "loop remains playing");
    return failures;
}

static int test_non_loop_repeat_two_reports_each_drawn_final_frame(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 12U, 112U, 7);
    (void)anim_player_lifecycle_commit(&lifecycle, 12U, 112U, 7, 3U, true);
    events = anim_player_lifecycle_note_frame(&lifecycle, 12U, 112U, 1U, 3U, true);
    failures += expect_true(events.count == 0U, "repeat first cycle intermediate frame is silent");
    events = anim_player_lifecycle_note_frame(&lifecycle, 12U, 112U, 2U, 3U, true);
    failures += expect_true(events.count == 1U && events.items[0].completed_cycles == 1U,
                            "repeat first final draw reports cycle one");
    (void)anim_player_lifecycle_note_frame(&lifecycle, 12U, 112U, 0U, 3U, true);
    (void)anim_player_lifecycle_note_frame(&lifecycle, 12U, 112U, 1U, 3U, true);
    events = anim_player_lifecycle_note_frame(&lifecycle, 12U, 112U, 2U, 3U, true);
    failures += expect_true(events.count == 1U && events.items[0].completed_cycles == 2U,
                            "repeat second final draw reports cycle two");
    return failures;
}

static int test_single_frame_repeat_reports_each_successful_draw(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 15U, 115U, 7);
    events = anim_player_lifecycle_commit(&lifecycle, 15U, 115U, 7, 1U, true);
    failures += expect_true(events.count == 2U && events.items[1].type == ANIM_PLAYER_EVENT_CYCLE_COMPLETED,
                            "single-frame first committed draw reports cycle one");
    failures += expect_true(events.items[1].completed_cycles == 1U, "single-frame first cycle count");
    events = anim_player_lifecycle_note_frame(&lifecycle, 15U, 115U, 0U, 1U, true);
    failures += expect_true(events.count == 1U && events.items[0].completed_cycles == 2U,
                            "single-frame next successful draw reports cycle two");
    return failures;
}

static int test_prepare_failures_are_bounded_and_observable(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 13U, 113U, 8);
    for (unsigned attempt = 1U; attempt < 3U; ++attempt) {
        events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 13U, 113U, ANIM_PLAYER_FAILURE_STREAM_OPEN, 3U);
        failures += expect_true(events.count == 0U, "failure below limit remains retryable");
        failures += expect_true(anim_player_lifecycle_should_retry_prepare(&lifecycle, 13U, 113U),
                                "prepare may retry below failure limit");
    }
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 13U, 113U, ANIM_PLAYER_FAILURE_STREAM_OPEN, 3U);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_FAILED,
                            "failure limit emits FAILED");
    failures += expect_true(events.items[0].failure == ANIM_PLAYER_FAILURE_STREAM_OPEN, "FAILED preserves open reason");
    failures += expect_true(!anim_player_lifecycle_should_retry_prepare(&lifecycle, 13U, 113U),
                            "failed prepare cannot retry forever");
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 13U, 113U, ANIM_PLAYER_FAILURE_STREAM_OPEN, 3U);
    failures += expect_true(events.count == 0U, "terminal failure is emitted exactly once");
    return failures;
}

static int test_prepare_progress_resets_consecutive_failures(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 14U, 114U, 8);
    (void)anim_player_lifecycle_note_prepare_failure(&lifecycle, 14U, 114U, ANIM_PLAYER_FAILURE_FRAME_READ, 2U);
    anim_player_lifecycle_note_prepare_progress(&lifecycle, 14U, 114U);
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 14U, 114U, ANIM_PLAYER_FAILURE_FRAME_READ, 2U);
    failures += expect_true(events.count == 0U, "successful frame resets consecutive failure budget");
    failures += expect_true(anim_player_lifecycle_snapshot(&lifecycle).prepare_failure_count == 1U,
                            "failure count restarts after progress");
    return failures;
}

static int test_stale_generation_cannot_commit_or_fail_new_prepare(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 21U, 121U, 5);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 22U, 122U, 6);
    events = anim_player_lifecycle_commit(&lifecycle, 21U, 121U, 5, 1U, false);
    failures += expect_true(events.count == 0U, "stale generation cannot commit");
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 21U, 121U, ANIM_PLAYER_FAILURE_FRAME_READ, 1U);
    failures += expect_true(events.count == 0U, "stale failure cannot fail current prepare");

    anim_player_lifecycle_snapshot_t snapshot = anim_player_lifecycle_snapshot(&lifecycle);
    failures += expect_true(snapshot.phase == ANIM_PLAYER_PHASE_PREPARING, "new prepare remains active");
    failures += expect_true(snapshot.preparing_generation == 22U, "new generation remains current");
    events = anim_player_lifecycle_commit(&lifecycle, 22U, 999U, 6, 1U, false);
    failures += expect_true(events.count == 0U, "matching generation with stale ticket cannot commit");
    failures += expect_true(anim_player_lifecycle_snapshot(&lifecycle).preparing_ticket == 122U,
                            "stale ticket does not replace current ticket");
    return failures;
}

static int test_worker_exit_requires_explicit_acknowledgement(void) {
    anim_player_lifecycle_t lifecycle;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    anim_player_lifecycle_worker_started(&lifecycle);
    failures += expect_true(!anim_player_lifecycle_can_destroy_sync(&lifecycle), "running worker blocks sync teardown");
    anim_player_lifecycle_request_worker_stop(&lifecycle);
    anim_player_lifecycle_snapshot_t snapshot = anim_player_lifecycle_snapshot(&lifecycle);
    failures += expect_true(snapshot.worker_stop_requested, "worker stop request is observable");
    failures += expect_true(!anim_player_lifecycle_can_destroy_sync(&lifecycle), "request alone is not exit handshake");
    anim_player_lifecycle_worker_exited(&lifecycle);
    failures +=
        expect_true(anim_player_lifecycle_can_destroy_sync(&lifecycle), "exit acknowledgement permits teardown");
    return failures;
}

static anim_player_ticket_event_t make_queue_event(animation_ticket_t ticket, animation_event_type_t type) {
    anim_player_ticket_event_t event = {
        .ticket = ticket,
        .type = type,
        .failure = ANIMATION_FAILURE_NONE,
        .animation_type = EMOJI_ANIM_LISTENING,
        .generation = ticket,
        .completed_cycles = 0U,
    };
    return event;
}

static int test_event_queue_preserves_fifo_order_and_wraps(void) {
    anim_player_event_queue_t queue;
    int failures = 0;

    anim_player_event_queue_init(&queue);
    for (animation_ticket_t ticket = 1U; ticket <= EMOJI_ANIM_PLAYER_EVENT_QUEUE_CAPACITY; ++ticket) {
        anim_player_ticket_event_t event = make_queue_event(ticket, ANIMATION_EVENT_COMMITTED);
        failures += expect_true(anim_player_event_queue_push_batch(&queue, &event, 1U), "queue accepts capacity");
    }
    for (animation_ticket_t ticket = 1U; ticket <= 8U; ++ticket) {
        anim_player_ticket_event_t event;
        failures += expect_true(anim_player_event_queue_pop(&queue, &event), "queue pops first half");
        failures += expect_true(event.ticket == ticket, "queue preserves first-half FIFO order");
    }
    for (animation_ticket_t ticket = 17U; ticket <= 24U; ++ticket) {
        anim_player_ticket_event_t event = make_queue_event(ticket, ANIMATION_EVENT_COMPLETED);
        failures += expect_true(anim_player_event_queue_push_batch(&queue, &event, 1U), "queue wraps tail");
    }
    for (animation_ticket_t ticket = 9U; ticket <= 24U; ++ticket) {
        anim_player_ticket_event_t event;
        failures += expect_true(anim_player_event_queue_pop(&queue, &event), "queue drains wrapped data");
        failures += expect_true(event.ticket == ticket, "queue preserves wrapped FIFO order");
    }
    failures += expect_true(anim_player_event_queue_count(&queue) == 0U, "queue drains to empty");
    return failures;
}

static int test_event_queue_overflow_rejects_batch_atomically(void) {
    anim_player_event_queue_t queue;
    anim_player_ticket_event_t initial[15];
    anim_player_ticket_event_t overflow[2];
    int failures = 0;

    anim_player_event_queue_init(&queue);
    for (size_t index = 0U; index < 15U; ++index) {
        initial[index] = make_queue_event((animation_ticket_t)(index + 1U), ANIMATION_EVENT_PREPARING);
    }
    overflow[0] = make_queue_event(100U, ANIMATION_EVENT_COMMITTED);
    overflow[1] = make_queue_event(100U, ANIMATION_EVENT_COMPLETED);

    failures += expect_true(anim_player_event_queue_push_batch(&queue, initial, 15U), "initial batch fits");
    failures += expect_true(!anim_player_event_queue_push_batch(&queue, overflow, 2U), "overflowing batch is rejected");
    failures += expect_true(anim_player_event_queue_count(&queue) == 15U, "overflow does not partially enqueue");
    failures += expect_true(queue.overflow_count == 1U, "overflow count is observable");
    failures += expect_true(queue.last_overflow_ticket == 100U, "overflow identifies rejected ticket");
    failures +=
        expect_true(anim_player_event_queue_has_overflow_fault(&queue), "overflow reserves an explicit terminal fault");
    failures += expect_true(!anim_player_event_queue_push_batch(&queue, &overflow[1], 1U),
                            "faulted ticket cannot enqueue events after its terminal fault");
    failures +=
        expect_true(anim_player_event_queue_count(&queue) == 15U, "post-fault events do not alter the normal queue");

    for (animation_ticket_t ticket = 1U; ticket <= 15U; ++ticket) {
        anim_player_ticket_event_t event;
        failures += expect_true(anim_player_event_queue_pop(&queue, &event), "original batch remains readable");
        failures += expect_true(event.ticket == ticket, "overflow preserves original event order");
    }
    anim_player_ticket_event_t fault;
    failures += expect_true(anim_player_event_queue_pop(&queue, &fault), "overflow fault remains drainable");
    failures += expect_true(fault.ticket == 100U && fault.type == ANIMATION_EVENT_FAILED,
                            "overflow fault terminates rejected ticket");
    failures +=
        expect_true(fault.failure == ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL, "overflow reports its precise reason");
    failures += expect_true(!anim_player_event_queue_pop(&queue, &fault), "queue is empty after overflow fault");
    return failures;
}

static int test_visible_render_failure_is_terminal_once(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 31U, 131U, 5);
    (void)anim_player_lifecycle_commit(&lifecycle, 31U, 131U, 5, 4U, true);
    events = anim_player_lifecycle_fail_visible(&lifecycle, 31U, 131U, ANIM_PLAYER_FAILURE_RENDER);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_FAILED,
                            "render failure terminates committed ticket");
    failures +=
        expect_true(events.items[0].failure == ANIM_PLAYER_FAILURE_RENDER, "render failure keeps precise reason");
    events = anim_player_lifecycle_fail_visible(&lifecycle, 31U, 131U, ANIM_PLAYER_FAILURE_RENDER);
    failures += expect_true(events.count == 0U, "repeated render failure emits no second terminal");
    events = anim_player_lifecycle_note_frame(&lifecycle, 31U, 131U, 3U, 4U, true);
    failures += expect_true(events.count == 0U, "frames after render failure cannot emit lifecycle events");
    return failures;
}

static int test_prepare_stalled_fails_once_and_preserves_visible_generation(void) {
    anim_player_lifecycle_t lifecycle;
    anim_player_lifecycle_events_t events;
    int failures = 0;

    anim_player_lifecycle_init(&lifecycle);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 40U, 140U, 2);
    (void)anim_player_lifecycle_commit(&lifecycle, 40U, 140U, 2, 3U, true);
    (void)anim_player_lifecycle_begin_prepare(&lifecycle, 41U, 141U, 3);
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 41U, 141U, ANIM_PLAYER_FAILURE_PREPARE_STALLED, 1U);
    failures += expect_true(events.count == 1U && events.items[0].type == ANIM_PLAYER_EVENT_FAILED,
                            "prepare stall emits failed terminal");
    failures += expect_true(events.items[0].failure == ANIM_PLAYER_FAILURE_PREPARE_STALLED,
                            "prepare stall keeps precise reason");
    anim_player_lifecycle_snapshot_t snapshot = anim_player_lifecycle_snapshot(&lifecycle);
    failures += expect_true(snapshot.visible_generation == 40U && snapshot.visible_ticket == 140U,
                            "failed staging preserves prior visible generation");
    events = anim_player_lifecycle_note_prepare_failure(&lifecycle, 41U, 141U, ANIM_PLAYER_FAILURE_PREPARE_STALLED, 1U);
    failures += expect_true(events.count == 0U, "prepare stall emits exactly one terminal");
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_single_frame_non_loop_completes_after_commit();
    failures += test_multi_frame_non_loop_completes_on_final_frame();
    failures += test_loop_emits_cycle_without_completing();
    failures += test_non_loop_repeat_two_reports_each_drawn_final_frame();
    failures += test_single_frame_repeat_reports_each_successful_draw();
    failures += test_prepare_failures_are_bounded_and_observable();
    failures += test_prepare_progress_resets_consecutive_failures();
    failures += test_stale_generation_cannot_commit_or_fail_new_prepare();
    failures += test_worker_exit_requires_explicit_acknowledgement();
    failures += test_event_queue_preserves_fifo_order_and_wraps();
    failures += test_event_queue_overflow_rejects_batch_atomically();
    failures += test_visible_render_failure_is_terminal_once();
    failures += test_prepare_stalled_fails_once_and_preserves_visible_generation();

    if (failures != 0) {
        fprintf(stderr, "anim player lifecycle host tests failed: %d\n", failures);
        return 1;
    }
    printf("anim player lifecycle host tests passed\n");
    return 0;
}
