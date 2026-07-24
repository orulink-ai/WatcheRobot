#include "behavior_animation_policy.h"
#include "behavior_animation_reducer.h"
#include "behavior_executor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BEHAVIOR_COMPONENT_SOURCE_DIR
#define BEHAVIOR_COMPONENT_SOURCE_DIR "."
#endif

static animation_event_t make_terminal_event(animation_ticket_t ticket, uint32_t owner_epoch, emoji_anim_type_t type,
                                             animation_event_type_t event_type) {
    animation_event_t event = {
        .ticket = ticket,
        .type = event_type,
        .failure = event_type == ANIMATION_EVENT_FAILED ? ANIMATION_FAILURE_SD_READ_FAILED : ANIMATION_FAILURE_NONE,
        .request =
            {
                .type = type,
                .priority = ANIM_PRIORITY_BEHAVIOR,
                .preempt_policy = ANIM_PREEMPTIBLE,
                .source = ANIM_SOURCE_BEHAVIOR,
                .owner_epoch = owner_epoch,
            },
    };
    return event;
}

static void assert_policy(const char *state_id, emoji_anim_type_t type, animation_priority_t priority,
                          animation_preempt_policy_t preempt_policy) {
    behavior_animation_policy_t policy = behavior_animation_policy_resolve(state_id, type);
    assert(policy.priority == priority);
    assert(policy.preempt_policy == preempt_policy);
}

static void test_priority_and_protection_policy(void) {
    assert_policy("standby", EMOJI_ANIM_STANDBY_LOOP, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE);
    assert_policy("standby_entry", EMOJI_ANIM_STANDBY, ANIM_PRIORITY_AMBIENT, ANIM_PREEMPTIBLE);
    assert_policy("standby_start", EMOJI_ANIM_STANDBY_START, ANIM_PRIORITY_BEHAVIOR, ANIM_PROTECTED_AFTER_COMMIT);
    assert_policy("standby_end", EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_BEHAVIOR, ANIM_PROTECTED_AFTER_COMMIT);
    assert_policy("listening_wake", EMOJI_ANIM_STANDBY_END, ANIM_PRIORITY_INTERACTION, ANIM_PROTECTED_AFTER_COMMIT);
    assert_policy("listening", EMOJI_ANIM_LISTENING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE);
    assert_policy("thinking", EMOJI_ANIM_THINKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE);
    assert_policy("speaking", EMOJI_ANIM_SPEAKING, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE);
    assert_policy("music", EMOJI_ANIM_MUSIC, ANIM_PRIORITY_INTERACTION, ANIM_PREEMPTIBLE);
    assert_policy("upgrade", EMOJI_ANIM_UPGRADE, ANIM_PRIORITY_SYSTEM, ANIM_PREEMPTIBLE);
    assert_policy("recharge", EMOJI_ANIM_RECHARGE, ANIM_PRIORITY_SYSTEM, ANIM_PREEMPTIBLE);
    assert_policy("happy", EMOJI_ANIM_HAPPY, ANIM_PRIORITY_BEHAVIOR, ANIM_PREEMPTIBLE);
    assert_policy("error", EMOJI_ANIM_ERROR, ANIM_PRIORITY_BEHAVIOR, ANIM_PREEMPTIBLE);
}

static void test_terminal_reducer_matches_ticket_epoch_type_and_source(void) {
    animation_event_t event = make_terminal_event(42U, 7U, EMOJI_ANIM_STANDBY_END, ANIMATION_EVENT_COMPLETED);

    assert(behavior_animation_reduce_terminal(42U, 7U, EMOJI_ANIM_STANDBY_END, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_COMPLETED);
    assert(behavior_animation_reduce_terminal(41U, 7U, EMOJI_ANIM_STANDBY_END, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_IGNORE);
    assert(behavior_animation_reduce_terminal(42U, 8U, EMOJI_ANIM_STANDBY_END, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_IGNORE);
    assert(behavior_animation_reduce_terminal(42U, 7U, EMOJI_ANIM_LISTENING, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_IGNORE);
    event.request.source = ANIM_SOURCE_AGENT;
    assert(behavior_animation_reduce_terminal(42U, 7U, EMOJI_ANIM_STANDBY_END, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_IGNORE);
}

static void test_terminal_reducer_distinguishes_failure_and_release(void) {
    animation_event_t event = make_terminal_event(8U, 3U, EMOJI_ANIM_STANDBY, ANIMATION_EVENT_FAILED);

    assert(behavior_animation_reduce_terminal(8U, 3U, EMOJI_ANIM_STANDBY, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_FAILED);
    event.type = ANIMATION_EVENT_PREEMPTED;
    assert(behavior_animation_reduce_terminal(8U, 3U, EMOJI_ANIM_STANDBY, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_RELEASED);
    event.type = ANIMATION_EVENT_CANCELLED;
    assert(behavior_animation_reduce_terminal(8U, 3U, EMOJI_ANIM_STANDBY, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_RELEASED);
    event.type = ANIMATION_EVENT_COMMITTED;
    assert(behavior_animation_reduce_terminal(8U, 3U, EMOJI_ANIM_STANDBY, &event) ==
           BEHAVIOR_ANIMATION_TERMINAL_IGNORE);
}

static char *read_source(const char *relative_path) {
    char path[512];
    FILE *file;
    long size;
    char *contents;

    snprintf(path, sizeof(path), "%s/%s", BEHAVIOR_COMPONENT_SOURCE_DIR, relative_path);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    contents = malloc((size_t)size + 1U);
    assert(contents != NULL);
    assert(fread(contents, 1U, (size_t)size, file) == (size_t)size);
    contents[size] = '\0';
    fclose(file);
    return contents;
}

static size_t count_occurrences(const char *haystack, const char *needle) {
    size_t count = 0U;
    size_t needle_len = strlen(needle);
    const char *cursor = haystack;

    while ((cursor = strstr(cursor, needle)) != NULL) {
        count++;
        cursor += needle_len;
    }
    return count;
}

static void test_static_animation_architecture_gate(void) {
    char *service = read_source("src/behavior_state_service.c");
    char *executor = read_source("src/behavior_executor.c");

    assert(strstr(service, "display_emoji_animation_completed") == NULL);
    assert(strstr(service, "#include \"anim_player.h\"") == NULL);
    assert(strstr(service, "emoji_anim_start(") == NULL);
    assert(strstr(service, "emoji_anim_stop(") == NULL);
    assert(strstr(executor, "#include \"anim_player.h\"") == NULL);
    assert(strstr(executor, "emoji_anim_start(") == NULL);
    assert(strstr(executor, "hal_display_set_emoji") == NULL);
    assert(strstr(executor, "animation_cancel(") == NULL);
    assert(strstr(service, "animation_set_event_sink(") != NULL);
    assert(strstr(service, "behavior_state_refresh_animation(") != NULL);
    assert(strstr(service, "force_animation_refresh") != NULL);
    assert(strstr(executor, "animation_submit(") != NULL);

    free(service);
    free(executor);
}

static void test_display_command_carries_repeat_owner_and_correlation(void) {
    behavior_display_command_t command = {0};
    command.playback_mode = ANIM_PLAYBACK_REPEAT_COUNT;
    command.repeat_count = 2U;
    command.fade_in_ms = 3000U;
    command.owner_epoch = 99U;
    command.correlation_id = 123U;
    assert(command.repeat_count == 2U);
    assert(command.playback_mode == ANIM_PLAYBACK_REPEAT_COUNT);
    assert(command.fade_in_ms == 3000U);
    assert(command.owner_epoch == 99U);
    assert(command.correlation_id == 123U);
}

static void test_same_state_override_refresh_advances_animation_correlation(void) {
    char *service = read_source("src/behavior_state_service.c");
    const char *helper = strstr(service, "static void behavior_refresh_same_state_overrides_locked");
    const char *helper_end = helper != NULL ? strstr(helper, "\n}") : NULL;
    const char *correlation_advance =
        helper != NULL ? strstr(helper, "behavior_begin_animation_correlation_locked();") : NULL;

    assert(helper != NULL);
    assert(helper_end != NULL);
    assert(correlation_advance != NULL);
    assert(correlation_advance < helper_end);
    assert(count_occurrences(service, "behavior_refresh_same_state_overrides_locked(") == 3U);
    free(service);
}

static void test_one_shot_resources_are_restartable_and_do_not_inherit_state_loop(void) {
    char *service = read_source("src/behavior_state_service.c");
    char *header = read_source("include/behavior_state_service.h");

    assert(strstr(header, "behavior_state_set_with_resources_and_action_once") != NULL);
    assert(strstr(service, "!s_ctx.resources_one_shot") != NULL);
    assert(strstr(service, "state_request.resources_one_shot") != NULL);
    assert(strstr(service, "behavior_scheduler_effective_animation_playback(event->playback_mode,") != NULL);
    assert(strstr(service, "behavior_scheduler_action_should_loop(s_ctx.current_state, s_ctx.current_action,") != NULL);
    assert(strstr(service, "if (!resources_one_shot && !s_ctx.resources_one_shot &&") != NULL);

    free(header);
    free(service);
}

int main(void) {
    test_priority_and_protection_policy();
    test_terminal_reducer_matches_ticket_epoch_type_and_source();
    test_terminal_reducer_distinguishes_failure_and_release();
    test_static_animation_architecture_gate();
    test_display_command_carries_repeat_owner_and_correlation();
    test_same_state_override_refresh_advances_animation_correlation();
    test_one_shot_resources_are_restartable_and_do_not_inherit_state_loop();
    puts("behavior animation integration host tests passed");
    return 0;
}
