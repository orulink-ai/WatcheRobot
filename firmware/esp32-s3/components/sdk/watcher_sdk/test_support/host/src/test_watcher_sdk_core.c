#include "watcher_sdk_core.h"
#include "watcher_sdk_motion_tracker.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    watcher_sdk_domain_t cancelled[16];
    size_t cancelled_count;
} fake_executor_t;

static void fake_cancel(watcher_sdk_domain_t domain, void *context) {
    fake_executor_t *executor = (fake_executor_t *)context;

    assert(executor->cancelled_count < 16U);
    executor->cancelled[executor->cancelled_count++] = domain;
}

static watcher_sdk_core_t make_core(fake_executor_t *executor) {
    watcher_sdk_core_t core;
    watcher_sdk_core_config_t config = {
        .cancel_domain = fake_cancel,
        .executor_context = executor,
    };

    assert(watcher_sdk_core_init(&core, &config) == WATCHER_SDK_RESULT_OK);
    return core;
}

static watcher_sdk_event_t next_event(watcher_sdk_core_t *core) {
    watcher_sdk_event_t event = {0};

    assert(watcher_sdk_core_poll_event(core, &event));
    return event;
}

static void expect_no_event(watcher_sdk_core_t *core) {
    watcher_sdk_event_t event = {0};
    assert(!watcher_sdk_core_poll_event(core, &event));
}

static void test_behavior_job_transitions_to_running(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t job_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_event_t event;

    assert(watcher_sdk_core_start_behavior(&core, 100U, &job_id) == WATCHER_SDK_RESULT_OK);
    assert(job_id != WATCHER_SDK_JOB_INVALID);

    event = next_event(&core);
    assert(event.job_id == job_id);
    assert(event.domain == WATCHER_SDK_DOMAIN_BEHAVIOR);
    assert(event.state == WATCHER_SDK_JOB_STARTING);

    event = next_event(&core);
    assert(event.job_id == job_id);
    assert(event.state == WATCHER_SDK_JOB_RUNNING);
    expect_no_event(&core);
}

static void test_direct_motion_cancels_whole_behavior(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t behavior_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t motion_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_event_t event;

    assert(watcher_sdk_core_start_behavior(&core, 0U, &behavior_id) == WATCHER_SDK_RESULT_OK);
    (void)next_event(&core);
    (void)next_event(&core);

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 10U, 100U, &motion_id) ==
           WATCHER_SDK_RESULT_OK);

    event = next_event(&core);
    assert(event.job_id == behavior_id);
    assert(event.state == WATCHER_SDK_JOB_CANCELLED);
    event = next_event(&core);
    assert(event.job_id == motion_id);
    assert(event.state == WATCHER_SDK_JOB_STARTING);
    event = next_event(&core);
    assert(event.job_id == motion_id);
    assert(event.state == WATCHER_SDK_JOB_RUNNING);
    assert(executor.cancelled_count == 1U);
    assert(executor.cancelled[0] == WATCHER_SDK_DOMAIN_BEHAVIOR);
}

static void test_direct_operations_on_different_domains_can_run_together(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t motion_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t audio_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 0U, 100U, &motion_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_AUDIO, 0U, 0U, &audio_id) ==
           WATCHER_SDK_RESULT_OK);

    assert(motion_id != audio_id);
    assert(watcher_sdk_core_get_state(&core, motion_id) == WATCHER_SDK_JOB_RUNNING);
    assert(watcher_sdk_core_get_state(&core, audio_id) == WATCHER_SDK_JOB_RUNNING);
    assert(executor.cancelled_count == 0U);
}

static void test_replacing_same_domain_cancels_previous_job(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t first_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t second_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_AUDIO, 0U, 0U, &first_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_AUDIO, 1U, 0U, &second_id) ==
           WATCHER_SDK_RESULT_OK);

    assert(watcher_sdk_core_get_state(&core, first_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(watcher_sdk_core_get_state(&core, second_id) == WATCHER_SDK_JOB_RUNNING);
    assert(executor.cancelled_count == 1U);
    assert(executor.cancelled[0] == WATCHER_SDK_DOMAIN_AUDIO);
}

static void test_cancel_is_idempotent(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t job_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 0U, 100U, &job_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_cancel(&core, job_id) == WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_cancel(&core, job_id) == WATCHER_SDK_RESULT_OK);
    assert(executor.cancelled_count == 1U);
}

static void test_observed_cancel_does_not_stop_executor_again(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t job_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 0U, 0U, &job_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_cancel_observed(&core, job_id) == WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_get_state(&core, job_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(executor.cancelled_count == 0U);
}

static void test_timed_job_completes_on_tick(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t job_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 10U, 50U, &job_id) ==
           WATCHER_SDK_RESULT_OK);
    watcher_sdk_core_tick(&core, 59U);
    assert(watcher_sdk_core_get_state(&core, job_id) == WATCHER_SDK_JOB_RUNNING);
    watcher_sdk_core_tick(&core, 60U);
    assert(watcher_sdk_core_get_state(&core, job_id) == WATCHER_SDK_JOB_COMPLETED);
}

static void test_disconnect_cancels_every_running_job(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t motion_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t audio_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 0U, 0U, &motion_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_AUDIO, 0U, 0U, &audio_id) ==
           WATCHER_SDK_RESULT_OK);

    watcher_sdk_core_cancel_all(&core);

    assert(watcher_sdk_core_get_state(&core, motion_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(watcher_sdk_core_get_state(&core, audio_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(executor.cancelled_count == 2U);
}

static void test_realtime_direct_command_preempts_without_creating_anonymous_job(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t behavior_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t light_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_behavior(&core, 0U, &behavior_id) == WATCHER_SDK_RESULT_OK);
    (void)next_event(&core);
    (void)next_event(&core);
    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_LIGHT, 1U, 100U, &light_id) ==
           WATCHER_SDK_RESULT_OK);
    (void)next_event(&core); /* behavior cancelled */
    (void)next_event(&core); /* light starting */
    (void)next_event(&core); /* light running */

    assert(watcher_sdk_core_preempt_direct(&core, WATCHER_SDK_DOMAIN_LIGHT) == WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_get_state(&core, light_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(next_event(&core).state == WATCHER_SDK_JOB_CANCELLED);
    expect_no_event(&core);
}

static void test_cancel_domain_cancels_only_matching_jobs(void) {
    fake_executor_t executor = {0};
    watcher_sdk_core_t core = make_core(&executor);
    watcher_sdk_job_id_t motion_id = WATCHER_SDK_JOB_INVALID;
    watcher_sdk_job_id_t audio_id = WATCHER_SDK_JOB_INVALID;

    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_MOTION, 0U, 100U, &motion_id) ==
           WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_start_direct(&core, WATCHER_SDK_DOMAIN_AUDIO, 0U, 100U, &audio_id) ==
           WATCHER_SDK_RESULT_OK);

    assert(watcher_sdk_core_cancel_domain(&core, WATCHER_SDK_DOMAIN_MOTION) == WATCHER_SDK_RESULT_OK);
    assert(watcher_sdk_core_get_state(&core, motion_id) == WATCHER_SDK_JOB_CANCELLED);
    assert(watcher_sdk_core_get_state(&core, audio_id) == WATCHER_SDK_JOB_RUNNING);
    assert(watcher_sdk_core_cancel_domain(&core, WATCHER_SDK_DOMAIN_MOTION) == WATCHER_SDK_RESULT_OK);
    assert(executor.cancelled_count == 1U);
}

static void test_motion_tracker_correlates_only_matching_sequence(void) {
    watcher_sdk_motion_tracker_t tracker;

    watcher_sdk_motion_tracker_init(&tracker);
    watcher_sdk_motion_tracker_bind(&tracker, 42U, 7U, 100U, 500U);

    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 41U, WATCHER_SDK_MOTION_SIGNAL_SUCCESS) ==
           WATCHER_SDK_MOTION_OUTCOME_NONE);
    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 42U, WATCHER_SDK_MOTION_SIGNAL_ACKED) ==
           WATCHER_SDK_MOTION_OUTCOME_NONE);
    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 42U, WATCHER_SDK_MOTION_SIGNAL_SUCCESS) ==
           WATCHER_SDK_MOTION_OUTCOME_COMPLETED);
    assert(!watcher_sdk_motion_tracker_is_active(&tracker));
}

static void test_motion_tracker_maps_stop_fault_and_timeout_to_terminal_outcomes(void) {
    watcher_sdk_motion_tracker_t tracker;

    watcher_sdk_motion_tracker_init(&tracker);
    watcher_sdk_motion_tracker_bind(&tracker, 10U, 1U, 100U, 500U);
    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 10U, WATCHER_SDK_MOTION_SIGNAL_STOPPED) ==
           WATCHER_SDK_MOTION_OUTCOME_CANCELLED);

    watcher_sdk_motion_tracker_bind(&tracker, 11U, 2U, 100U, 500U);
    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 11U, WATCHER_SDK_MOTION_SIGNAL_FAULT) ==
           WATCHER_SDK_MOTION_OUTCOME_FAILED);

    watcher_sdk_motion_tracker_bind(&tracker, 12U, 3U, 100U, 500U);
    assert(watcher_sdk_motion_tracker_on_signal(&tracker, 0U, WATCHER_SDK_MOTION_SIGNAL_FAULT) ==
           WATCHER_SDK_MOTION_OUTCOME_FAILED);

    watcher_sdk_motion_tracker_bind(&tracker, 13U, 4U, 100U, 500U);
    assert(!watcher_sdk_motion_tracker_poll_timeout(&tracker, 2599U));
    assert(watcher_sdk_motion_tracker_poll_timeout(&tracker, 2600U));
    assert(!watcher_sdk_motion_tracker_is_active(&tracker));
}

int main(void) {
    test_behavior_job_transitions_to_running();
    test_direct_motion_cancels_whole_behavior();
    test_direct_operations_on_different_domains_can_run_together();
    test_replacing_same_domain_cancels_previous_job();
    test_cancel_is_idempotent();
    test_observed_cancel_does_not_stop_executor_again();
    test_timed_job_completes_on_tick();
    test_disconnect_cancels_every_running_job();
    test_realtime_direct_command_preempts_without_creating_anonymous_job();
    test_cancel_domain_cancels_only_matching_jobs();
    test_motion_tracker_correlates_only_matching_sequence();
    test_motion_tracker_maps_stop_fault_and_timeout_to_terminal_outcomes();
    puts("watcher_sdk_core_host_tests: PASS");
    return 0;
}
