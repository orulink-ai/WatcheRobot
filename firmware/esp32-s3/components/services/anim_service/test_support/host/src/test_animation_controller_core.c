#include "animation_controller_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

static animation_request_t make_request(emoji_anim_type_t type, animation_priority_t priority,
                                        animation_preempt_policy_t policy, animation_source_t source,
                                        uint32_t owner_epoch) {
    animation_request_t request = {
        .type = type,
        .priority = priority,
        .preempt_policy = policy,
        .repeat_count = 0,
        .source = source,
        .owner_epoch = owner_epoch,
        .correlation_id = owner_epoch + 100U,
    };
    return request;
}

static void assert_event(const animation_controller_output_t *output, size_t index, animation_ticket_t ticket,
                         animation_event_type_t type) {
    assert(index < output->event_count);
    assert(output->events[index].ticket == ticket);
    assert(output->events[index].type == type);
}

static animation_ticket_t submit_ok(animation_controller_t *controller, const animation_request_t *request,
                                    animation_controller_output_t *output) {
    animation_ticket_t ticket = ANIMATION_TICKET_INVALID;
    animation_service_result_t result = animation_controller_submit(controller, request, &ticket, output);

    assert(result == ANIMATION_SERVICE_OK);
    assert(ticket != ANIMATION_TICKET_INVALID);
    return ticket;
}

static void commit(animation_controller_t *controller, animation_ticket_t ticket,
                   animation_controller_output_t *output) {
    assert(animation_controller_handle_player_event(controller, ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                                    ANIMATION_FAILURE_NONE, output) == ANIMATION_SERVICE_OK);
}

static void test_lifecycle_repeat_count_and_terminal_exactly_once(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t request =
        make_request(EMOJI_ANIM_STANDBY, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 1U);
    animation_ticket_t ticket;
    animation_snapshot_t snapshot;

    request.repeat_count = 2U;
    animation_controller_init(&controller);

    ticket = submit_ok(&controller, &request, &output);
    assert(output.event_count == 2U);
    assert_event(&output, 0U, ticket, ANIMATION_EVENT_ACCEPTED);
    assert_event(&output, 1U, ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);
    assert(output.player_command.ticket == ticket);
    assert(output.player_command.request.repeat_count == 2U);

    commit(&controller, ticket, &output);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, ticket, ANIMATION_EVENT_COMMITTED);

    assert(animation_controller_handle_player_event(&controller, ticket, ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, ticket, ANIMATION_EVENT_CYCLE_COMPLETED);

    assert(animation_controller_handle_player_event(&controller, ticket, ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 2U);
    assert_event(&output, 0U, ticket, ANIMATION_EVENT_CYCLE_COMPLETED);
    assert_event(&output, 1U, ticket, ANIMATION_EVENT_COMPLETED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_STOP);

    assert(animation_controller_handle_player_event(&controller, ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_NOT_FOUND);
    assert(output.event_count == 0U);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == ANIMATION_TICKET_INVALID);
    assert(snapshot.live_ticket_count == 0U);
    assert(snapshot.emitted_terminal_count == 1U);
    assert(snapshot.visible_ticket == ticket);
    assert(snapshot.visible_type == EMOJI_ANIM_STANDBY);
}

static void test_same_priority_latest_wins_and_ticket_prevents_aba(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t first =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 10U);
    animation_request_t second = first;
    animation_ticket_t first_ticket;
    animation_ticket_t second_ticket;

    animation_controller_init(&controller);
    first_ticket = submit_ok(&controller, &first, &output);
    commit(&controller, first_ticket, &output);

    second.correlation_id++;
    second_ticket = submit_ok(&controller, &second, &output);
    assert(second_ticket != first_ticket);
    assert(output.event_count == 3U);
    assert_event(&output, 0U, first_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, second_ticket, ANIMATION_EVENT_ACCEPTED);
    assert_event(&output, 2U, second_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);
    assert(output.player_command.ticket == second_ticket);

    assert(animation_controller_handle_player_event(&controller, first_ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_NOT_FOUND);
    assert(output.event_count == 0U);
}

static void test_priority_preemption_and_delayed_promotion(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t ambient =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 1U);
    animation_request_t interaction =
        make_request(EMOJI_ANIM_SPEAKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 2U);
    animation_request_t behavior =
        make_request(EMOJI_ANIM_HAPPY, ANIM_PRIORITY_BEHAVIOR, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 3U);
    animation_ticket_t ambient_ticket;
    animation_ticket_t interaction_ticket;
    animation_ticket_t behavior_ticket;
    animation_snapshot_t snapshot;

    animation_controller_init(&controller);
    ambient_ticket = submit_ok(&controller, &ambient, &output);
    commit(&controller, ambient_ticket, &output);

    interaction_ticket = submit_ok(&controller, &interaction, &output);
    assert_event(&output, 0U, ambient_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 2U, interaction_ticket, ANIMATION_EVENT_PREPARING);
    commit(&controller, interaction_ticket, &output);

    behavior_ticket = submit_ok(&controller, &behavior, &output);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, behavior_ticket, ANIMATION_EVENT_ACCEPTED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == interaction_ticket);
    assert(snapshot.queued_count == 1U);
    assert(snapshot.desired_ticket == behavior_ticket);

    assert(animation_controller_handle_player_event(&controller, interaction_ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 2U);
    assert_event(&output, 0U, interaction_ticket, ANIMATION_EVENT_COMPLETED);
    assert_event(&output, 1U, behavior_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);
    assert(output.player_command.ticket == behavior_ticket);
}

static void test_sleep_loop_is_preempted_immediately_by_sleep_out(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_snapshot_t snapshot;
    animation_request_t sleep_loop =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 4U);
    animation_request_t sleep_out = make_request(EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_INTERACTION,
                                                 ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 4U);
    animation_ticket_t loop_ticket;
    animation_ticket_t end_ticket;

    sleep_loop.correlation_id = 1U;
    sleep_out.correlation_id = 2U;
    animation_controller_init(&controller);
    loop_ticket = submit_ok(&controller, &sleep_loop, &output);
    commit(&controller, loop_ticket, &output);

    end_ticket = submit_ok(&controller, &sleep_out, &output);
    assert(output.event_count == 3U);
    assert_event(&output, 0U, loop_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, end_ticket, ANIMATION_EVENT_ACCEPTED);
    assert_event(&output, 2U, end_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);
    assert(output.player_command.ticket == end_ticket);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == end_ticket);
    assert(snapshot.preparing_ticket == end_ticket);
    assert(snapshot.desired_ticket == end_ticket);
    assert(snapshot.visible_type == EMOJI_ANIM_STANDBY_LOOP);
}

static void test_same_owner_newer_correlation_preempts_across_priority(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t interaction =
        make_request(EMOJI_ANIM_SPEAKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 12U);
    animation_request_t ambient =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 12U);
    animation_ticket_t interaction_ticket;
    animation_ticket_t ambient_ticket;

    interaction.correlation_id = 10U;
    ambient.correlation_id = 11U;
    animation_controller_init(&controller);
    interaction_ticket = submit_ok(&controller, &interaction, &output);
    commit(&controller, interaction_ticket, &output);

    ambient_ticket = submit_ok(&controller, &ambient, &output);
    assert(output.event_count == 3U);
    assert_event(&output, 0U, interaction_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, ambient_ticket, ANIMATION_EVENT_ACCEPTED);
    assert_event(&output, 2U, ambient_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);
    assert(output.player_command.ticket == ambient_ticket);
}

static void test_same_owner_stale_or_equal_correlation_is_rejected_without_replay(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t current =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 13U);
    animation_request_t stale =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 13U);
    animation_ticket_t current_ticket;
    animation_ticket_t rejected_ticket = 99U;
    animation_snapshot_t snapshot;

    current.correlation_id = 20U;
    animation_controller_init(&controller);
    current_ticket = submit_ok(&controller, &current, &output);
    commit(&controller, current_ticket, &output);

    stale.correlation_id = 20U;
    assert(animation_controller_submit(&controller, &stale, &rejected_ticket, &output) ==
           ANIMATION_SERVICE_INVALID_TRANSITION);
    assert(rejected_ticket == ANIMATION_TICKET_INVALID);
    assert(output.event_count == 0U);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    stale.correlation_id = 19U;
    rejected_ticket = 99U;
    assert(animation_controller_submit(&controller, &stale, &rejected_ticket, &output) ==
           ANIMATION_SERVICE_INVALID_TRANSITION);
    assert(rejected_ticket == ANIMATION_TICKET_INVALID);
    assert(output.event_count == 0U);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == current_ticket);
    assert(snapshot.live_ticket_count == 1U);
}

static void test_different_owner_still_obeys_priority(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t interaction =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 14U);
    animation_request_t ambient =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 15U);
    animation_ticket_t interaction_ticket;
    animation_ticket_t ambient_ticket;
    animation_snapshot_t snapshot;

    interaction.correlation_id = 1U;
    ambient.correlation_id = 99U;
    animation_controller_init(&controller);
    interaction_ticket = submit_ok(&controller, &interaction, &output);
    commit(&controller, interaction_ticket, &output);
    ambient_ticket = submit_ok(&controller, &ambient, &output);

    assert(output.event_count == 1U);
    assert_event(&output, 0U, ambient_ticket, ANIMATION_EVENT_ACCEPTED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);
    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == interaction_ticket);
    assert(snapshot.queued_count == 1U);
}

static void test_protected_after_commit_defers_and_merges_final_target(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 20U);
    animation_request_t target =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 21U);
    animation_ticket_t protected_ticket;
    animation_ticket_t first_target_ticket;
    animation_ticket_t latest_target_ticket;

    animation_controller_init(&controller);
    protected_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, protected_ticket, &output);

    first_target_ticket = submit_ok(&controller, &target, &output);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, first_target_ticket, ANIMATION_EVENT_ACCEPTED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    target.type = EMOJI_ANIM_THINKING;
    target.correlation_id++;
    latest_target_ticket = submit_ok(&controller, &target, &output);
    assert(output.event_count == 2U);
    assert_event(&output, 0U, first_target_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, latest_target_ticket, ANIMATION_EVENT_ACCEPTED);

    assert(animation_controller_handle_player_event(&controller, protected_ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);
    assert_event(&output, 0U, protected_ticket, ANIMATION_EVENT_COMPLETED);
    assert_event(&output, 1U, latest_target_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.ticket == latest_target_ticket);
}

static void test_protected_active_keeps_only_newest_cross_priority_owner_target(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 22U);
    animation_request_t interaction =
        make_request(EMOJI_ANIM_THINKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 22U);
    animation_request_t ambient =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 22U);
    animation_ticket_t protected_ticket;
    animation_ticket_t stale_target_ticket;
    animation_ticket_t newest_target_ticket;

    protected_request.correlation_id = 1U;
    interaction.correlation_id = 2U;
    ambient.correlation_id = 3U;
    animation_controller_init(&controller);
    protected_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, protected_ticket, &output);
    stale_target_ticket = submit_ok(&controller, &interaction, &output);
    newest_target_ticket = submit_ok(&controller, &ambient, &output);

    assert(output.event_count == 2U);
    assert_event(&output, 0U, stale_target_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, newest_target_ticket, ANIMATION_EVENT_ACCEPTED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    assert(animation_controller_handle_player_event(&controller, protected_ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);
    assert_event(&output, 0U, protected_ticket, ANIMATION_EVENT_COMPLETED);
    assert_event(&output, 1U, newest_target_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.ticket == newest_target_ticket);
}

static void test_system_preempts_committed_protected_animation(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_START, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 30U);
    animation_request_t system_request =
        make_request(EMOJI_ANIM_ERROR, ANIM_PRIORITY_SYSTEM, ANIM_PREEMPTIBLE, ANIM_SOURCE_SYSTEM, 31U);
    animation_ticket_t protected_ticket;
    animation_ticket_t system_ticket;

    animation_controller_init(&controller);
    protected_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, protected_ticket, &output);
    system_ticket = submit_ok(&controller, &system_request, &output);

    assert_event(&output, 0U, protected_ticket, ANIMATION_EVENT_PREEMPTED);
    assert_event(&output, 1U, system_ticket, ANIMATION_EVENT_ACCEPTED);
    assert_event(&output, 2U, system_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.ticket == system_ticket);
}

static void test_cancel_rejects_committed_protected_animation(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 35U);
    animation_ticket_t protected_ticket;
    animation_snapshot_t snapshot;

    animation_controller_init(&controller);
    protected_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, protected_ticket, &output);

    assert(animation_controller_cancel(&controller, protected_ticket, &output) == ANIMATION_SERVICE_INVALID_TRANSITION);
    assert(output.event_count == 0U);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == protected_ticket);
    assert(snapshot.visible_ticket == protected_ticket);
    assert(snapshot.live_ticket_count == 1U);
}

static void test_cancel_owner_preserves_protected_active_and_cancels_owned_queue(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_START, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 36U);
    animation_request_t queued_request =
        make_request(EMOJI_ANIM_HAPPY, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 36U);
    animation_ticket_t protected_ticket;
    animation_ticket_t queued_ticket;
    animation_snapshot_t snapshot;

    animation_controller_init(&controller);
    protected_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, protected_ticket, &output);
    queued_request.correlation_id++;
    queued_ticket = submit_ok(&controller, &queued_request, &output);

    assert(animation_controller_cancel_owner(&controller, ANIM_SOURCE_BEHAVIOR, 36U, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, queued_ticket, ANIMATION_EVENT_CANCELLED);
    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == protected_ticket);
    assert(snapshot.queued_count == 0U);
    assert(snapshot.live_ticket_count == 1U);

    assert(animation_controller_cancel_owner(&controller, ANIM_SOURCE_BEHAVIOR, 36U, &output) ==
           ANIMATION_SERVICE_INVALID_TRANSITION);
    assert(output.event_count == 0U);
    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == protected_ticket);
}

static void test_cancel_and_cancel_owner_promote_remaining_request(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t active =
        make_request(EMOJI_ANIM_SPEAKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 40U);
    animation_request_t owned_one =
        make_request(EMOJI_ANIM_HAPPY, ANIM_PRIORITY_BEHAVIOR, ANIM_PREEMPTIBLE, ANIM_SOURCE_APP, 41U);
    animation_request_t owned_two = owned_one;
    animation_request_t survivor =
        make_request(EMOJI_ANIM_STANDBY, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 42U);
    animation_ticket_t active_ticket;
    animation_ticket_t owned_ticket;
    animation_ticket_t replacement_owned_ticket;
    animation_ticket_t survivor_ticket;
    animation_snapshot_t snapshot;

    animation_controller_init(&controller);
    active_ticket = submit_ok(&controller, &active, &output);
    commit(&controller, active_ticket, &output);
    owned_ticket = submit_ok(&controller, &owned_one, &output);
    owned_two.type = EMOJI_ANIM_BLUETOOTH;
    owned_two.correlation_id++;
    replacement_owned_ticket = submit_ok(&controller, &owned_two, &output);
    assert_event(&output, 0U, owned_ticket, ANIMATION_EVENT_PREEMPTED);
    survivor_ticket = submit_ok(&controller, &survivor, &output);

    assert(animation_controller_cancel_owner(&controller, ANIM_SOURCE_APP, 41U, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, replacement_owned_ticket, ANIMATION_EVENT_CANCELLED);

    assert(animation_controller_cancel(&controller, active_ticket, &output) == ANIMATION_SERVICE_OK);
    assert_event(&output, 0U, active_ticket, ANIMATION_EVENT_CANCELLED);
    assert_event(&output, 1U, survivor_ticket, ANIMATION_EVENT_PREPARING);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_PLAY);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.active_ticket == survivor_ticket);
    assert(snapshot.live_ticket_count == 1U);
    assert(snapshot.queued_count == 0U);
}

static void test_fixed_queue_rejects_before_acceptance(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t protected_request = make_request(EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_BEHAVIOR,
                                                         ANIM_PROTECTED_AFTER_COMMIT, ANIM_SOURCE_BEHAVIOR, 50U);
    animation_request_t queued =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 100U);
    animation_ticket_t active_ticket;
    animation_ticket_t ticket;
    size_t i;

    animation_controller_init(&controller);
    active_ticket = submit_ok(&controller, &protected_request, &output);
    commit(&controller, active_ticket, &output);

    for (i = 0U; i < ANIMATION_CONTROLLER_QUEUE_CAPACITY; ++i) {
        queued.source = (animation_source_t)(ANIM_SOURCE_AGENT + (i % 4U));
        queued.owner_epoch = (uint32_t)(100U + i);
        queued.correlation_id = (uint32_t)i;
        (void)submit_ok(&controller, &queued, &output);
    }

    queued.owner_epoch = 999U;
    ticket = 123U;
    assert(animation_controller_submit(&controller, &queued, &ticket, &output) == ANIMATION_SERVICE_QUEUE_FULL);
    assert(ticket == ANIMATION_TICKET_INVALID);
    assert(output.event_count == 0U);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);
}

static void test_failure_is_terminal_and_keeps_last_visible_snapshot(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t old_request =
        make_request(EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE, ANIM_SOURCE_BEHAVIOR, 60U);
    animation_request_t new_request =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 61U);
    animation_ticket_t old_ticket;
    animation_ticket_t new_ticket;
    animation_snapshot_t snapshot;

    animation_controller_init(&controller);
    old_ticket = submit_ok(&controller, &old_request, &output);
    commit(&controller, old_ticket, &output);
    new_ticket = submit_ok(&controller, &new_request, &output);

    assert(animation_controller_handle_player_event(&controller, new_ticket, ANIMATION_PLAYER_EVENT_FAILED,
                                                    ANIMATION_FAILURE_SD_READ_FAILED, &output) == ANIMATION_SERVICE_OK);
    assert(output.event_count == 1U);
    assert_event(&output, 0U, new_ticket, ANIMATION_EVENT_FAILED);
    assert(output.events[0].failure == ANIMATION_FAILURE_SD_READ_FAILED);
    assert(output.player_command.type == ANIMATION_PLAYER_COMMAND_NONE);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.visible_ticket == old_ticket);
    assert(snapshot.visible_type == EMOJI_ANIM_STANDBY_LOOP);
    assert(snapshot.active_ticket == ANIMATION_TICKET_INVALID);
}

static void test_invalid_request_and_invalid_player_transition(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_request_t request =
        make_request(EMOJI_ANIM_NONE, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 70U);
    animation_ticket_t ticket = 123U;

    animation_controller_init(&controller);
    assert(animation_controller_submit(&controller, &request, &ticket, &output) == ANIMATION_SERVICE_INVALID_ARGUMENT);
    assert(ticket == ANIMATION_TICKET_INVALID);
    assert(output.event_count == 0U);

    request.type = EMOJI_ANIM_LISTENING;
    request.playback_mode = ANIM_PLAYBACK_REPEAT_COUNT;
    assert(animation_controller_submit(&controller, &request, &ticket, &output) == ANIMATION_SERVICE_INVALID_ARGUMENT);
    assert(ticket == ANIMATION_TICKET_INVALID);
    assert(output.event_count == 0U);

    request.playback_mode = ANIM_PLAYBACK_RESOURCE_DEFAULT;
    ticket = submit_ok(&controller, &request, &output);
    assert(animation_controller_handle_player_event(&controller, ticket, ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED,
                                                    ANIMATION_FAILURE_NONE,
                                                    &output) == ANIMATION_SERVICE_INVALID_TRANSITION);
    assert(output.event_count == 0U);

    animation_snapshot_t snapshot;
    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.wrong_transition_count == 1U);
    assert(snapshot.active_ticket == ticket);
    assert(snapshot.live_ticket_count == 1U);
}

static void test_observability_counts_every_accepted_ticket_and_terminal_kind(void) {
    animation_controller_t controller;
    animation_controller_output_t output;
    animation_snapshot_t snapshot;
    animation_request_t first =
        make_request(EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 80U);
    animation_request_t second =
        make_request(EMOJI_ANIM_THINKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 81U);
    animation_request_t third =
        make_request(EMOJI_ANIM_SPEAKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE, ANIM_SOURCE_AGENT, 82U);
    animation_request_t fourth =
        make_request(EMOJI_ANIM_ERROR, ANIM_PRIORITY_SYSTEM, ANIM_PREEMPTIBLE, ANIM_SOURCE_SYSTEM, 83U);
    animation_ticket_t first_ticket;
    animation_ticket_t second_ticket;
    animation_ticket_t third_ticket;
    animation_ticket_t fourth_ticket;

    animation_controller_init(&controller);
    first_ticket = submit_ok(&controller, &first, &output);
    second_ticket = submit_ok(&controller, &second, &output);
    assert_event(&output, 0U, first_ticket, ANIMATION_EVENT_PREEMPTED);
    assert(animation_controller_cancel(&controller, second_ticket, &output) == ANIMATION_SERVICE_OK);

    third_ticket = submit_ok(&controller, &third, &output);
    commit(&controller, third_ticket, &output);
    assert(animation_controller_handle_player_event(&controller, third_ticket, ANIMATION_PLAYER_EVENT_COMPLETED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_OK);

    fourth_ticket = submit_ok(&controller, &fourth, &output);
    assert(animation_controller_handle_player_event(&controller, fourth_ticket, ANIMATION_PLAYER_EVENT_FAILED,
                                                    ANIMATION_FAILURE_SD_READ_FAILED, &output) == ANIMATION_SERVICE_OK);
    assert(animation_controller_handle_player_event(&controller, first_ticket, ANIMATION_PLAYER_EVENT_COMMITTED,
                                                    ANIMATION_FAILURE_NONE, &output) == ANIMATION_SERVICE_NOT_FOUND);

    animation_controller_get_snapshot(&controller, &snapshot);
    assert(snapshot.accepted_count == 4U);
    assert(snapshot.terminal_count == 4U);
    assert(snapshot.completed_count == 1U);
    assert(snapshot.preempted_count == 1U);
    assert(snapshot.cancelled_count == 1U);
    assert(snapshot.failed_count == 1U);
    assert(snapshot.orphan_player_event_count == 1U);
    assert(snapshot.live_ticket_count == 0U);
    assert(snapshot.terminal_count == snapshot.accepted_count);
}

int main(void) {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_lifecycle_repeat_count_and_terminal_exactly_once();
    test_same_priority_latest_wins_and_ticket_prevents_aba();
    test_priority_preemption_and_delayed_promotion();
    test_sleep_loop_is_preempted_immediately_by_sleep_out();
    test_same_owner_newer_correlation_preempts_across_priority();
    test_same_owner_stale_or_equal_correlation_is_rejected_without_replay();
    test_different_owner_still_obeys_priority();
    test_protected_after_commit_defers_and_merges_final_target();
    test_protected_active_keeps_only_newest_cross_priority_owner_target();
    test_system_preempts_committed_protected_animation();
    test_cancel_rejects_committed_protected_animation();
    test_cancel_owner_preserves_protected_active_and_cancels_owned_queue();
    test_cancel_and_cancel_owner_promote_remaining_request();
    test_fixed_queue_rejects_before_acceptance();
    test_failure_is_terminal_and_keeps_last_visible_snapshot();
    test_invalid_request_and_invalid_player_transition();
    test_observability_counts_every_accepted_ticket_and_terminal_kind();

    puts("animation controller core host tests passed");
    return 0;
}
