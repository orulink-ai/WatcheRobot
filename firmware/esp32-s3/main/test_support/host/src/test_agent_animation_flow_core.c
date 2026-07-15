#include "agent_animation_flow_core.h"

#include <assert.h>
#include <stdio.h>

static void test_ready_then_early_wake_preserves_complete_transition_order(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_wake_pending(&flow));

    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_COMPLETE_WAKE);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_LISTENING);
}

static void test_wake_after_sleep_commit_still_requires_sleep_out(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_SLEEPING);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_ASLEEP);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_STANDBY_END);
}

static void test_sleep_out_failure_is_fatal_and_never_listening(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_FAILED) ==
           AGENT_ANIMATION_ACTION_ERROR);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_ERROR);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
}

static void test_close_resets_pending_wake_for_reentry(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    agent_animation_flow_core_close(&flow);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_IDLE);
    assert(!agent_animation_flow_wake_pending(&flow));
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
}

static void test_awake_listening_phase_does_not_require_sleep_out(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_SLEEPING);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_COMPLETE_WAKE);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_COMPLETE_WAKE);
}

static void test_listening_must_commit_before_recording_is_released(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT);

    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_COMPLETE_WAKE);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_LISTENING);
}

static void test_listening_failure_is_fatal_before_recording_starts(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_FAILED) ==
           AGENT_ANIMATION_ACTION_ERROR);
    assert(agent_animation_flow_phase(&flow) == AGENT_ANIMATION_PHASE_ERROR);
}

static void test_second_sleep_cycle_requires_a_new_sleep_out(void) {
    agent_animation_flow_core_t flow;

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_SLEEPING);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_STANDBY_END);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_END, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_NONE);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_COMPLETE_WAKE);

    agent_animation_flow_core_init(&flow);
    assert(agent_animation_flow_on_ready(&flow) == AGENT_ANIMATION_ACTION_STANDBY_ENTRY);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_ENTRY, AGENT_ANIMATION_EVENT_COMPLETED) ==
           AGENT_ANIMATION_ACTION_STANDBY_LOOP);
    assert(agent_animation_flow_on_event(&flow, AGENT_ANIMATION_CLIP_STANDBY_LOOP, AGENT_ANIMATION_EVENT_COMMITTED) ==
           AGENT_ANIMATION_ACTION_SLEEPING);
    assert(agent_animation_flow_on_wake(&flow) == AGENT_ANIMATION_ACTION_STANDBY_END);
}

int main(void) {
    test_ready_then_early_wake_preserves_complete_transition_order();
    test_wake_after_sleep_commit_still_requires_sleep_out();
    test_sleep_out_failure_is_fatal_and_never_listening();
    test_close_resets_pending_wake_for_reentry();
    test_awake_listening_phase_does_not_require_sleep_out();
    test_listening_must_commit_before_recording_is_released();
    test_listening_failure_is_fatal_before_recording_starts();
    test_second_sleep_cycle_requires_a_new_sleep_out();
    puts("agent animation flow core host tests passed");
    return 0;
}
