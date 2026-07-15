#include "voice_remote_control_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static voice_remote_snapshot_t snapshot(bool desired, bool permitted, uint32_t generation) {
    voice_remote_snapshot_t value = {
        .recording_desired = desired,
        .recording_permitted = permitted,
        .generation = generation,
    };
    return value;
}

static void test_open_waits_for_gate_then_starts_once(void) {
    voice_remote_control_state_t state;
    voice_remote_control_init(&state);

    assert(voice_remote_control_apply(&state, snapshot(true, false, 1U), false) == VOICE_REMOTE_ACTION_NONE);
    assert(state.recording_desired);
    assert(!state.recording_permitted);
    assert(voice_remote_control_apply(&state, snapshot(true, true, 2U), false) == VOICE_REMOTE_ACTION_START);
    assert(voice_remote_control_apply(&state, snapshot(true, true, 2U), false) == VOICE_REMOTE_ACTION_NONE);
}

static void test_close_before_gate_open_is_latest_wins(void) {
    voice_remote_control_state_t state;
    voice_remote_control_init(&state);

    assert(voice_remote_control_apply(&state, snapshot(true, false, 1U), false) == VOICE_REMOTE_ACTION_NONE);
    assert(voice_remote_control_apply(&state, snapshot(false, false, 2U), false) == VOICE_REMOTE_ACTION_NONE);
    assert(voice_remote_control_apply(&state, snapshot(false, true, 3U), false) == VOICE_REMOTE_ACTION_NONE);
    assert(!state.recording_desired);
    assert(state.recording_permitted);
}

static void test_coalesced_snapshot_never_replays_stale_open(void) {
    voice_remote_control_state_t state;
    voice_remote_control_init(&state);

    /* The task sees only the newest mailbox generation after open/close/gate updates race. */
    assert(voice_remote_control_apply(&state, snapshot(false, true, 3U), false) == VOICE_REMOTE_ACTION_NONE);
    assert(!state.recording_desired);
    assert(state.recording_permitted);
    assert(state.applied_generation == 3U);
}

static void test_close_stops_active_recording(void) {
    voice_remote_control_state_t state;
    voice_remote_control_init(&state);

    assert(voice_remote_control_apply(&state, snapshot(true, true, 1U), false) == VOICE_REMOTE_ACTION_START);
    assert(voice_remote_control_apply(&state, snapshot(false, true, 2U), true) == VOICE_REMOTE_ACTION_STOP);
}

static void test_gate_close_does_not_interrupt_active_recording(void) {
    voice_remote_control_state_t state;
    voice_remote_control_init(&state);

    assert(voice_remote_control_apply(&state, snapshot(true, true, 1U), false) == VOICE_REMOTE_ACTION_START);
    assert(voice_remote_control_apply(&state, snapshot(true, false, 2U), true) == VOICE_REMOTE_ACTION_NONE);
}

static void test_mailbox_rejects_request_outside_running_session(void) {
    voice_remote_mailbox_state_t mailbox;
    voice_remote_mailbox_init(&mailbox);

    assert(!voice_remote_mailbox_publish_desired(&mailbox, true));
    voice_remote_mailbox_prepare_session(&mailbox);
    assert(!voice_remote_mailbox_publish_desired(&mailbox, true));
    voice_remote_mailbox_open_session(&mailbox);
    assert(voice_remote_mailbox_publish_desired(&mailbox, true));
    voice_remote_mailbox_close_session(&mailbox);
    assert(!voice_remote_mailbox_publish_desired(&mailbox, true));
    assert(!mailbox.target.recording_desired);
}

static void test_mailbox_create_failure_cannot_leak_target_to_next_session(void) {
    voice_remote_mailbox_state_t mailbox;
    voice_remote_mailbox_init(&mailbox);

    voice_remote_mailbox_publish_permitted(&mailbox, false);
    voice_remote_mailbox_prepare_session(&mailbox);
    assert(!mailbox.target.recording_permitted);
    assert(!voice_remote_mailbox_publish_desired(&mailbox, true));
    voice_remote_mailbox_close_session(&mailbox);
    voice_remote_mailbox_prepare_session(&mailbox);
    assert(!mailbox.target.recording_desired);
}

static void test_mailbox_suspend_closes_target_and_requires_ack_only_when_open(void) {
    voice_remote_mailbox_state_t mailbox;
    voice_remote_suspend_request_t suspend_request;
    voice_remote_mailbox_init(&mailbox);

    suspend_request = voice_remote_mailbox_request_suspend(&mailbox);
    assert(!suspend_request.needs_ack);
    voice_remote_mailbox_prepare_session(&mailbox);
    voice_remote_mailbox_open_session(&mailbox);
    assert(voice_remote_mailbox_publish_desired(&mailbox, true));
    suspend_request = voice_remote_mailbox_request_suspend(&mailbox);
    assert(suspend_request.needs_ack);
    assert(suspend_request.generation == mailbox.suspend_requested_generation);
    assert(!mailbox.target.recording_desired);
}

int main(void) {
    test_open_waits_for_gate_then_starts_once();
    test_close_before_gate_open_is_latest_wins();
    test_coalesced_snapshot_never_replays_stale_open();
    test_close_stops_active_recording();
    test_gate_close_does_not_interrupt_active_recording();
    test_mailbox_rejects_request_outside_running_session();
    test_mailbox_create_failure_cannot_leak_target_to_next_session();
    test_mailbox_suspend_closes_target_and_requires_ack_only_when_open();
    puts("voice remote control core tests passed");
    return 0;
}
