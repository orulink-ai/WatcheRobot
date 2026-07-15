#include "animation_service_sync_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    animation_service_sync_adapter_t *adapter;
    animation_event_t events[64];
    size_t event_count;
    animation_service_result_t reentrant_result;
    animation_ticket_t played_ticket;
    animation_request_t played_request;
    size_t play_count;
    size_t stop_count;
    animation_failure_t play_failure;
} test_context_t;

static animation_request_t make_request(emoji_anim_type_t type, uint32_t owner_epoch) {
    animation_request_t request = {
        .type = type,
        .priority = ANIM_PRIORITY_INTERACTION,
        .preempt_policy = ANIM_PREEMPTIBLE,
        .repeat_count = 0U,
        .source = ANIM_SOURCE_AGENT,
        .owner_epoch = owner_epoch,
        .correlation_id = owner_epoch + 100U,
    };
    return request;
}

static animation_failure_t fake_play(void *context, animation_ticket_t ticket, const animation_request_t *request) {
    test_context_t *test = context;
    test->played_ticket = ticket;
    test->played_request = *request;
    test->play_count++;
    return test->play_failure;
}

static void fake_stop(void *context) {
    test_context_t *test = context;
    test->stop_count++;
}

static void record_event(const animation_event_t *event, void *context) {
    test_context_t *test = context;
    assert(test->event_count < sizeof(test->events) / sizeof(test->events[0]));
    test->events[test->event_count++] = *event;
}

static void reentrant_sink(const animation_event_t *event, void *context) {
    test_context_t *test = context;
    animation_request_t request = make_request(EMOJI_ANIM_THINKING, 999U);
    animation_ticket_t ticket = ANIMATION_TICKET_INVALID;
    (void)event;
    test->reentrant_result = animation_service_sync_submit(test->adapter, &request, &ticket);
}

static void init_adapter(animation_service_sync_adapter_t *adapter, test_context_t *test) {
    animation_service_player_ops_t ops = {
        .context = test,
        .play = fake_play,
        .stop = fake_stop,
    };
    memset(test, 0, sizeof(*test));
    test->adapter = adapter;
    animation_service_sync_adapter_init(adapter, &ops);
}

static void test_lifecycle_forwarding(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 1U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_OK);
    assert(ticket != ANIMATION_TICKET_INVALID);
    assert(test.play_count == 1U && test.played_ticket == ticket);
    assert(test.events[0].type == ANIMATION_EVENT_ACCEPTED);
    assert(test.events[1].type == ANIMATION_EVENT_PREPARING);

    assert(animation_service_sync_player_event(&adapter, ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                               ANIMATION_FAILURE_NONE) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_player_event(&adapter, ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                               ANIMATION_FAILURE_NONE) == ANIMATION_SERVICE_OK);
    assert(test.events[2].type == ANIMATION_EVENT_COMMITTED);
    assert(test.events[3].type == ANIMATION_EVENT_COMPLETED);
}

static void test_no_surface_rejects_before_ticket(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 2U);
    animation_ticket_t ticket = 123U;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_NO_SURFACE);
    assert(ticket == ANIMATION_TICKET_INVALID);
    assert(test.play_count == 0U);
}

static void test_service_stop_fails_every_live_ticket_once(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t active = make_request(EMOJI_ANIM_STANDBY_END, 3U);
    animation_request_t queued = make_request(EMOJI_ANIM_LISTENING, 4U);
    animation_ticket_t active_ticket;
    animation_ticket_t queued_ticket;
    size_t failed_count = 0U;
    size_t index;

    active.priority = ANIM_PRIORITY_BEHAVIOR;
    active.preempt_policy = ANIM_PROTECTED_AFTER_COMMIT;
    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &active, &active_ticket) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_player_event(&adapter, active_ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                               ANIMATION_FAILURE_NONE) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &queued, &queued_ticket) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_stop(&adapter) == ANIMATION_SERVICE_OK);
    assert(test.stop_count == 1U);
    for (index = 0U; index < test.event_count; ++index) {
        if (test.events[index].type == ANIMATION_EVENT_FAILED &&
            test.events[index].failure == ANIMATION_FAILURE_SERVICE_STOPPED) {
            failed_count++;
        }
    }
    assert(failed_count == 2U);
    assert(animation_service_sync_cancel(&adapter, active_ticket) == ANIMATION_SERVICE_STOPPED);
}

static void test_fixed_controller_queue_full_is_forwarded(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t active = make_request(EMOJI_ANIM_STANDBY_END, 10U);
    animation_request_t queued = make_request(EMOJI_ANIM_LISTENING, 20U);
    animation_ticket_t ticket;
    size_t index;

    active.priority = ANIM_PRIORITY_BEHAVIOR;
    active.preempt_policy = ANIM_PROTECTED_AFTER_COMMIT;
    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &active, &ticket) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_player_event(&adapter, ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                               ANIMATION_FAILURE_NONE) == ANIMATION_SERVICE_OK);
    for (index = 0U; index < ANIMATION_CONTROLLER_QUEUE_CAPACITY; ++index) {
        queued.source = (animation_source_t)(ANIM_SOURCE_AGENT + (index % 4U));
        queued.owner_epoch = 20U + (uint32_t)index;
        assert(animation_service_sync_submit(&adapter, &queued, &ticket) == ANIMATION_SERVICE_OK);
    }
    queued.owner_epoch = 999U;
    assert(animation_service_sync_submit(&adapter, &queued, &ticket) == ANIMATION_SERVICE_QUEUE_FULL);
    assert(ticket == ANIMATION_TICKET_INVALID);
}

static void test_sink_reentry_is_rejected(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 30U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, reentrant_sink, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_OK);
    assert(test.reentrant_result == ANIMATION_SERVICE_REENTRANT);
}

static void test_player_start_failure_becomes_terminal(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 40U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    test.play_failure = ANIMATION_FAILURE_SD_OPEN_FAILED;
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_OK);
    assert(test.event_count == 3U);
    assert(test.events[2].type == ANIMATION_EVENT_FAILED);
    assert(test.events[2].failure == ANIMATION_FAILURE_SD_OPEN_FAILED);
}

static void test_suspend_cancels_live_and_resume_requires_new_ticket(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 50U);
    animation_ticket_t old_ticket;
    animation_ticket_t new_ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &old_ticket) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_suspended(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(test.events[test.event_count - 1U].ticket == old_ticket);
    assert(test.events[test.event_count - 1U].type == ANIMATION_EVENT_CANCELLED);
    assert(animation_service_sync_submit(&adapter, &request, &new_ticket) == ANIMATION_SERVICE_STOPPED);
    assert(animation_service_sync_set_suspended(&adapter, false) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &new_ticket) == ANIMATION_SERVICE_OK);
    assert(new_ticket != old_ticket);
}

static void test_unbind_fails_live_ticket_and_rejects_new_submit(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 60U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_surface(&adapter, false) == ANIMATION_SERVICE_OK);
    assert(test.events[test.event_count - 1U].ticket == ticket);
    assert(test.events[test.event_count - 1U].type == ANIMATION_EVENT_FAILED);
    assert(test.events[test.event_count - 1U].failure == ANIMATION_FAILURE_SERVICE_STOPPED);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_NO_SURFACE);

    animation_snapshot_t snapshot;
    assert(animation_service_sync_get_snapshot(&adapter, &snapshot) == ANIMATION_SERVICE_OK);
    assert(snapshot.accepted_count == 1U);
    assert(snapshot.terminal_count == 1U);
    assert(snapshot.failed_count == 1U);
    assert(snapshot.terminal_count == snapshot.accepted_count);
}

static void test_submit_can_ack_before_player_side_effects(void) {
    animation_service_sync_adapter_t adapter;
    animation_controller_output_t output;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 70U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_set_sink(&adapter, record_event, &test) == ANIMATION_SERVICE_OK);
    assert(animation_service_sync_prepare_submit(&adapter, &request, &ticket, &output) == ANIMATION_SERVICE_OK);
    assert(ticket != ANIMATION_TICKET_INVALID);
    assert(test.play_count == 0U);
    assert(test.event_count == 0U);
    animation_service_sync_dispatch_output(&adapter, &output);
    assert(test.play_count == 1U);
    assert(test.events[0].type == ANIMATION_EVENT_ACCEPTED);
    assert(test.events[1].type == ANIMATION_EVENT_PREPARING);
}

static void test_internal_observer_receives_events_without_public_sink(void) {
    animation_service_sync_adapter_t adapter;
    test_context_t test;
    animation_request_t request = make_request(EMOJI_ANIM_LISTENING, 71U);
    animation_ticket_t ticket;

    init_adapter(&adapter, &test);
    assert(animation_service_sync_set_surface(&adapter, true) == ANIMATION_SERVICE_OK);
    animation_service_sync_set_event_observer(&adapter, record_event, &test);
    assert(animation_service_sync_submit(&adapter, &request, &ticket) == ANIMATION_SERVICE_OK);
    assert(test.event_count == 2U);
    assert(test.events[0].type == ANIMATION_EVENT_ACCEPTED);
    assert(test.events[1].type == ANIMATION_EVENT_PREPARING);
}

static void test_overflow_buffer_preserves_one_failure_per_ticket(void) {
    animation_service_overflow_buffer_t buffer;
    bool seen[ANIMATION_CONTROLLER_TICKET_CAPACITY + 1U] = {false};
    animation_ticket_t ticket;
    size_t index;

    animation_service_overflow_buffer_init(&buffer);
    for (index = 1U; index <= ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        assert(animation_service_overflow_buffer_record(&buffer, (animation_ticket_t)index));
        assert(animation_service_overflow_buffer_record(&buffer, (animation_ticket_t)index));
    }
    assert(!animation_service_overflow_buffer_record(&buffer,
                                                     (animation_ticket_t)(ANIMATION_CONTROLLER_TICKET_CAPACITY + 1U)));
    assert(buffer.recorded_count == (ANIMATION_CONTROLLER_TICKET_CAPACITY * 2U) + 1U);
    assert(animation_service_overflow_buffer_has_pending(&buffer));
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        assert(animation_service_overflow_buffer_take(&buffer, &ticket));
        assert(ticket > 0U && ticket <= ANIMATION_CONTROLLER_TICKET_CAPACITY);
        assert(!seen[ticket]);
        seen[ticket] = true;
    }
    assert(!animation_service_overflow_buffer_take(&buffer, &ticket));
    assert(!animation_service_overflow_buffer_has_pending(&buffer));
}

int main(void) {
    test_lifecycle_forwarding();
    test_no_surface_rejects_before_ticket();
    test_service_stop_fails_every_live_ticket_once();
    test_fixed_controller_queue_full_is_forwarded();
    test_sink_reentry_is_rejected();
    test_player_start_failure_becomes_terminal();
    test_suspend_cancels_live_and_resume_requires_new_ticket();
    test_unbind_fails_live_ticket_and_rejects_new_submit();
    test_submit_can_ack_before_player_side_effects();
    test_internal_observer_receives_events_without_public_sink();
    test_overflow_buffer_preserves_one_failure_per_ticket();
    puts("animation service sync adapter host tests passed");
    return 0;
}
