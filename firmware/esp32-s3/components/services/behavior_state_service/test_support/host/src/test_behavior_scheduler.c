#include "behavior_scheduler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static behavior_state_def_t make_state(bool loop, bool hold, uint32_t timeline_end_ms) {
    behavior_state_def_t state = {0};

    state.loop = loop;
    state.hold_until_replaced = hold;
    state.timeline_end_ms = timeline_end_ms;
    state.motion_count = 2;
    state.expression_count = 1;
    state.sound_count = 1;
    state.light_count = 1;
    return state;
}

static void test_state_motion_is_overridden_by_action(void) {
    behavior_action_def_t action = {0};

    assert(!behavior_scheduler_state_motion_overridden(NULL));
    assert(behavior_scheduler_state_motion_overridden(&action));
}

static void test_action_loop_follows_state_loop_or_hold(void) {
    behavior_action_def_t action = {0};
    behavior_state_def_t loop_state = make_state(true, false, 100);
    behavior_state_def_t hold_state = make_state(false, true, 100);
    behavior_state_def_t one_shot_state = make_state(false, false, 100);

    assert(behavior_scheduler_action_should_loop(&loop_state, &action, true));
    assert(behavior_scheduler_action_should_loop(&hold_state, &action, true));
    assert(!behavior_scheduler_action_should_loop(&one_shot_state, &action, true));
    assert(!behavior_scheduler_action_should_loop(&loop_state, NULL, true));
    assert(!behavior_scheduler_action_should_loop(&loop_state, &action, false));
    assert(!behavior_scheduler_action_should_loop(&hold_state, &action, false));
}

static void test_one_shot_resource_policy_overrides_looping_state_animation(void) {
    assert(behavior_scheduler_effective_animation_playback(ANIM_PLAYBACK_LOOP_UNTIL_REPLACED, false) ==
           ANIM_PLAYBACK_LOOP_UNTIL_REPLACED);
    assert(behavior_scheduler_effective_animation_playback(ANIM_PLAYBACK_REPEAT_COUNT, false) ==
           ANIM_PLAYBACK_REPEAT_COUNT);
    assert(behavior_scheduler_effective_animation_playback(ANIM_PLAYBACK_LOOP_UNTIL_REPLACED, true) ==
           ANIM_PLAYBACK_ONCE);
}

static void test_state_completion_ignores_state_motion_when_action_active(void) {
    behavior_state_def_t state = make_state(false, false, 100);
    behavior_action_def_t action = {.motion_count = 1};
    behavior_scheduler_snapshot_t snapshot = {
        .state = &state,
        .action = &action,
        .next_motion_index = 0,
        .next_expression_index = 1,
        .next_sound_index = 1,
        .next_light_index = 1,
        .next_action_motion_index = 1,
    };

    assert(behavior_scheduler_all_state_events_dispatched(&snapshot));
    assert(behavior_scheduler_all_action_events_dispatched(&action, snapshot.next_action_motion_index));
}

static void test_state_completion_requires_state_motion_without_action(void) {
    behavior_state_def_t state = make_state(false, false, 100);
    behavior_scheduler_snapshot_t snapshot = {
        .state = &state,
        .action = NULL,
        .next_motion_index = 1,
        .next_expression_index = 1,
        .next_sound_index = 1,
        .next_light_index = 1,
        .next_action_motion_index = 0,
    };

    assert(!behavior_scheduler_all_state_events_dispatched(&snapshot));
    snapshot.next_motion_index = 2;
    assert(behavior_scheduler_all_state_events_dispatched(&snapshot));
}

static void test_non_loop_done_uses_timeline_default_and_action_duration(void) {
    behavior_state_def_t empty_state = make_state(false, false, 0);
    behavior_state_def_t timed_state = make_state(false, false, 300);
    behavior_action_def_t short_action = {.total_duration_ms = 200};
    behavior_action_def_t long_action = {.total_duration_ms = 600};

    assert(behavior_scheduler_non_loop_done_at_ms(&empty_state, NULL, 1200) == 1200);
    assert(behavior_scheduler_non_loop_done_at_ms(&timed_state, &short_action, 1200) == 300);
    assert(behavior_scheduler_non_loop_done_at_ms(&timed_state, &long_action, 1200) == 600);
    assert(behavior_scheduler_non_loop_done_at_ms(NULL, &long_action, 1200) == 0);
}

static void test_animation_transition_requires_matching_completed_animation(void) {
    behavior_state_def_t state = {0};

    snprintf(state.animation_complete_anim, sizeof(state.animation_complete_anim), "%s", "standby_end");
    snprintf(state.animation_complete_state, sizeof(state.animation_complete_state), "%s", "listening");

    assert(behavior_scheduler_animation_transition_target(&state, NULL) == NULL);
    assert(behavior_scheduler_animation_transition_target(&state, "standby_loop") == NULL);
    assert(strcmp(behavior_scheduler_animation_transition_target(&state, "standby_end"), "listening") == 0);
}

static void test_animation_transition_defers_early_target_request(void) {
    behavior_state_def_t state = {0};

    snprintf(state.animation_complete_anim, sizeof(state.animation_complete_anim), "%s", "standby_end");
    snprintf(state.animation_complete_state, sizeof(state.animation_complete_state), "%s", "listening");

    assert(behavior_scheduler_should_defer_animation_transition_target(&state, "listening", NULL));
    assert(behavior_scheduler_should_defer_animation_transition_target(&state, "listening", "standby_loop"));
    assert(!behavior_scheduler_should_defer_animation_transition_target(&state, "thinking", NULL));
    assert(!behavior_scheduler_should_defer_animation_transition_target(&state, "listening", "standby_end"));
}

static void test_collect_due_events_dispatches_in_stable_order(void) {
    behavior_motion_event_t state_motion[] = {
        {.at_ms = 0, .x_deg = 90, .y_deg = 120, .duration_ms = 100},
    };
    behavior_expression_event_t expressions[] = {
        {.at_ms = 0, .anim = "standby"},
    };
    behavior_sound_event_t sounds[] = {
        {.at_ms = 10, .sound_id = "boot"},
    };
    behavior_light_event_t lights[] = {
        {.at_ms = 5, .effect = "breathing", .red = 77, .green = 163, .blue = 255},
    };
    behavior_state_def_t state = {
        .motion = state_motion,
        .motion_count = 1,
        .expression = expressions,
        .expression_count = 1,
        .sound = sounds,
        .sound_count = 1,
        .light = lights,
        .light_count = 1,
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .state_elapsed_ms = 10,
    };
    behavior_scheduler_tick_result_t result = {0};

    behavior_scheduler_collect_due_events(&input, &result);

    assert(result.command_count == 4);
    assert(result.commands[0].type == BEHAVIOR_SCHEDULER_COMMAND_STATE_MOTION);
    assert(result.commands[0].motion == &state_motion[0]);
    assert(result.commands[1].type == BEHAVIOR_SCHEDULER_COMMAND_EXPRESSION);
    assert(result.commands[1].expression == &expressions[0]);
    assert(result.commands[2].type == BEHAVIOR_SCHEDULER_COMMAND_SOUND);
    assert(result.commands[2].sound == &sounds[0]);
    assert(result.commands[3].type == BEHAVIOR_SCHEDULER_COMMAND_LIGHT);
    assert(result.commands[3].light == &lights[0]);
    assert(result.next_motion_index == 1);
    assert(result.next_expression_index == 1);
    assert(result.next_sound_index == 1);
    assert(result.next_light_index == 1);
    assert(!result.has_more_due_events);
}

static void test_collect_due_events_never_dispatches_second_primary_animation(void) {
    behavior_expression_event_t expressions[] = {
        {.at_ms = 0, .anim = "standby"},
        {.at_ms = 100, .anim = "happy"},
    };
    behavior_state_def_t state = {
        .expression = expressions,
        .expression_count = 2,
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .state_elapsed_ms = 100,
    };
    behavior_scheduler_tick_result_t result = {0};

    behavior_scheduler_collect_due_events(&input, &result);

    assert(result.command_count == 1);
    assert(result.commands[0].type == BEHAVIOR_SCHEDULER_COMMAND_EXPRESSION);
    assert(result.commands[0].expression == &expressions[0]);
    assert(result.next_expression_index == 2);
}

static void test_collect_due_events_action_overrides_state_motion(void) {
    behavior_motion_event_t state_motion[] = {
        {.at_ms = 0, .x_deg = 90, .y_deg = 120, .duration_ms = 100},
    };
    behavior_motion_event_t action_motion[] = {
        {.at_ms = 0, .x_deg = 120, .y_deg = 80, .duration_ms = 150},
    };
    behavior_state_def_t state = {
        .motion = state_motion,
        .motion_count = 1,
    };
    behavior_action_def_t action = {
        .motion = action_motion,
        .motion_count = 1,
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .action = &action,
        .state_elapsed_ms = 10,
        .action_elapsed_ms = 10,
    };
    behavior_scheduler_tick_result_t result = {0};

    behavior_scheduler_collect_due_events(&input, &result);

    assert(result.command_count == 1);
    assert(result.commands[0].type == BEHAVIOR_SCHEDULER_COMMAND_ACTION_MOTION);
    assert(result.commands[0].motion == &action_motion[0]);
    assert(result.next_motion_index == 0);
    assert(result.next_action_motion_index == 1);
}

static void test_collect_due_events_prequeues_contiguous_action_motion(void) {
    behavior_state_def_t state = {0};
    behavior_motion_event_t action_motion[] = {
        {.at_ms = 0, .x_deg = 90, .y_deg = 100, .duration_ms = 1000},
        {.at_ms = 1000, .x_deg = 90, .y_deg = 120, .duration_ms = 1000},
    };
    behavior_action_def_t action = {
        .motion = action_motion,
        .motion_count = 2,
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .action = &action,
        .action_elapsed_ms = 950,
        .next_action_motion_index = 1,
    };
    behavior_scheduler_tick_result_t result = {0};

    behavior_scheduler_collect_due_events(&input, &result);

    assert(result.command_count == 1);
    assert(result.commands[0].type == BEHAVIOR_SCHEDULER_COMMAND_ACTION_MOTION);
    assert(result.commands[0].motion == &action_motion[1]);
    assert(result.next_action_motion_index == 2);
}

static void test_collect_due_events_does_not_prequeue_after_hold_gap(void) {
    behavior_state_def_t state = {0};
    behavior_motion_event_t action_motion[] = {
        {.at_ms = 0, .x_deg = 90, .y_deg = 100, .duration_ms = 500},
        {.at_ms = 1000, .x_deg = 90, .y_deg = 120, .duration_ms = 1000},
    };
    behavior_action_def_t action = {
        .motion = action_motion,
        .motion_count = 2,
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .action = &action,
        .action_elapsed_ms = 950,
        .next_action_motion_index = 1,
    };
    behavior_scheduler_tick_result_t result = {0};

    behavior_scheduler_collect_due_events(&input, &result);

    assert(result.command_count == 0);
    assert(result.next_action_motion_index == 1);
}

static void test_collect_due_events_uses_fixed_command_capacity(void) {
    behavior_motion_event_t motions[BEHAVIOR_SCHEDULER_COMMAND_CAPACITY + 1U];
    behavior_state_def_t state = {
        .motion = motions,
        .motion_count = (int)(BEHAVIOR_SCHEDULER_COMMAND_CAPACITY + 1U),
    };
    behavior_scheduler_tick_input_t input = {
        .state = &state,
        .state_elapsed_ms = 1,
    };
    behavior_scheduler_tick_result_t first = {0};
    behavior_scheduler_tick_result_t second = {0};
    size_t i;

    for (i = 0; i < BEHAVIOR_SCHEDULER_COMMAND_CAPACITY + 1U; ++i) {
        motions[i].at_ms = 0;
        motions[i].x_deg = 90;
        motions[i].y_deg = 120;
        motions[i].duration_ms = 50;
    }

    behavior_scheduler_collect_due_events(&input, &first);

    assert(first.command_count == BEHAVIOR_SCHEDULER_COMMAND_CAPACITY);
    assert(first.has_more_due_events);
    assert(first.next_motion_index == (int)BEHAVIOR_SCHEDULER_COMMAND_CAPACITY);

    input.next_motion_index = first.next_motion_index;
    behavior_scheduler_collect_due_events(&input, &second);

    assert(second.command_count == 1);
    assert(!second.has_more_due_events);
    assert(second.next_motion_index == (int)(BEHAVIOR_SCHEDULER_COMMAND_CAPACITY + 1U));
}

int main(void) {
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"state_motion_is_overridden_by_action", test_state_motion_is_overridden_by_action},
        {"action_loop_follows_state_loop_or_hold", test_action_loop_follows_state_loop_or_hold},
        {"one_shot_resource_policy_overrides_looping_state_animation",
         test_one_shot_resource_policy_overrides_looping_state_animation},
        {"state_completion_ignores_state_motion_when_action_active",
         test_state_completion_ignores_state_motion_when_action_active},
        {"state_completion_requires_state_motion_without_action",
         test_state_completion_requires_state_motion_without_action},
        {"non_loop_done_uses_timeline_default_and_action_duration",
         test_non_loop_done_uses_timeline_default_and_action_duration},
        {"animation_transition_requires_matching_completed_animation",
         test_animation_transition_requires_matching_completed_animation},
        {"animation_transition_defers_early_target_request", test_animation_transition_defers_early_target_request},
        {"collect_due_events_dispatches_in_stable_order", test_collect_due_events_dispatches_in_stable_order},
        {"collect_due_events_never_dispatches_second_primary_animation",
         test_collect_due_events_never_dispatches_second_primary_animation},
        {"collect_due_events_action_overrides_state_motion", test_collect_due_events_action_overrides_state_motion},
        {"collect_due_events_prequeues_contiguous_action_motion",
         test_collect_due_events_prequeues_contiguous_action_motion},
        {"collect_due_events_does_not_prequeue_after_hold_gap",
         test_collect_due_events_does_not_prequeue_after_hold_gap},
        {"collect_due_events_uses_fixed_command_capacity", test_collect_due_events_uses_fixed_command_capacity},
    };
    size_t i;

    for (i = 0u; i < ARRAY_SIZE(tests); ++i) {
        tests[i].fn();
        printf("[PASS] %s\n", tests[i].name);
    }

    return 0;
}
