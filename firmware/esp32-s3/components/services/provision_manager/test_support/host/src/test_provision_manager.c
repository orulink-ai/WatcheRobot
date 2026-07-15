#include "provision_manager.h"

#include <stdbool.h>
#include <stdio.h>

static int s_failures;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                       \
            s_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static provision_event_t event_for(const provision_manager_t *manager, provision_event_type_t type) {
    provision_snapshot_t snapshot = provision_manager_snapshot(manager);
    provision_event_t event = {
        .type = type,
        .generation = snapshot.generation,
        .failure_reason = PROVISION_FAILURE_NONE,
    };
    return event;
}

static void test_unprovisioned_resume_waits_for_ble(void) {
    provision_manager_t manager;

    provision_manager_init(&manager, false, false);
    provision_snapshot_t snapshot = provision_manager_snapshot(&manager);
    CHECK(snapshot.state == PROVISION_STATE_UNPROVISIONED);
    CHECK(snapshot.generation == 1U);
    CHECK(!snapshot.ui_attached);

    CHECK(provision_manager_resume(&manager) == PROVISION_TRANSITION_APPLIED);
    snapshot = provision_manager_snapshot(&manager);
    CHECK(snapshot.state == PROVISION_STATE_WAIT_BLE);
    CHECK(snapshot.generation == 1U);
    CHECK(snapshot.ui_attached);
}

static void test_happy_path_reaches_ready(void) {
    provision_manager_t manager;
    provision_manager_init(&manager, false, false);
    (void)provision_manager_resume(&manager);

    provision_event_t event = event_for(&manager, PROVISION_EVENT_BLE_CONNECTED);
    CHECK(provision_manager_handle_event(&manager, &event) == PROVISION_TRANSITION_IGNORED);

    event = event_for(&manager, PROVISION_EVENT_CREDENTIALS_RECEIVED);
    CHECK(provision_manager_handle_event(&manager, &event) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_WIFI_CONFIGURING);

    event = event_for(&manager, PROVISION_EVENT_WIFI_CONNECT_STARTED);
    CHECK(provision_manager_handle_event(&manager, &event) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_WIFI_VALIDATING);

    event = event_for(&manager, PROVISION_EVENT_WIFI_CONNECTED);
    CHECK(provision_manager_handle_event(&manager, &event) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_READY);
    CHECK(provision_manager_handle_event(&manager, &event) == PROVISION_TRANSITION_IGNORED);
}

static void test_retry_rejects_stale_callbacks(void) {
    provision_manager_t manager;
    provision_manager_init(&manager, true, false);
    provision_snapshot_t before = provision_manager_snapshot(&manager);
    CHECK(before.state == PROVISION_STATE_WIFI_VALIDATING);

    provision_event_t failure = event_for(&manager, PROVISION_EVENT_WIFI_FAILED);
    failure.failure_reason = PROVISION_FAILURE_WIFI_AUTH;
    CHECK(provision_manager_handle_event(&manager, &failure) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_FAILED_RETRYABLE);

    provision_event_t retry = event_for(&manager, PROVISION_EVENT_RETRY);
    CHECK(provision_manager_handle_event(&manager, &retry) == PROVISION_TRANSITION_APPLIED);
    provision_snapshot_t after = provision_manager_snapshot(&manager);
    CHECK(after.state == PROVISION_STATE_WAIT_BLE);
    CHECK(after.failure_reason == PROVISION_FAILURE_NONE);
    CHECK(after.generation == before.generation + 1U);

    provision_event_t stale_connected = {
        .type = PROVISION_EVENT_WIFI_CONNECTED,
        .generation = before.generation,
        .failure_reason = PROVISION_FAILURE_NONE,
    };
    CHECK(provision_manager_handle_event(&manager, &stale_connected) == PROVISION_TRANSITION_STALE);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_WAIT_BLE);
}

static void test_illegal_transition_preserves_state(void) {
    provision_manager_t manager;
    provision_manager_init(&manager, false, false);
    (void)provision_manager_resume(&manager);

    provision_event_t connected = event_for(&manager, PROVISION_EVENT_WIFI_CONNECTED);
    CHECK(provision_manager_handle_event(&manager, &connected) == PROVISION_TRANSITION_REJECTED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_WAIT_BLE);
}

static void test_suspend_detaches_ui_without_cancelling_work(void) {
    provision_manager_t manager;
    provision_manager_init(&manager, false, false);
    (void)provision_manager_resume(&manager);
    provision_event_t credentials = event_for(&manager, PROVISION_EVENT_CREDENTIALS_RECEIVED);
    (void)provision_manager_handle_event(&manager, &credentials);
    provision_snapshot_t before = provision_manager_snapshot(&manager);

    CHECK(provision_manager_suspend(&manager) == PROVISION_TRANSITION_APPLIED);
    provision_snapshot_t after = provision_manager_snapshot(&manager);
    CHECK(after.state == before.state);
    CHECK(after.generation == before.generation);
    CHECK(!after.ui_attached);
    CHECK(provision_manager_suspend(&manager) == PROVISION_TRANSITION_IGNORED);

    CHECK(provision_manager_resume(&manager) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_WIFI_CONFIGURING);
}

static void test_repair_and_factory_reset_are_deterministic(void) {
    provision_manager_t manager;
    provision_manager_init(&manager, true, true);
    uint32_t generation = provision_manager_snapshot(&manager).generation;

    provision_event_t corrupt = event_for(&manager, PROVISION_EVENT_STORAGE_CORRUPT);
    CHECK(provision_manager_handle_event(&manager, &corrupt) == PROVISION_TRANSITION_APPLIED);
    provision_snapshot_t snapshot = provision_manager_snapshot(&manager);
    CHECK(snapshot.state == PROVISION_STATE_REPAIR_REQUIRED);
    CHECK(snapshot.failure_reason == PROVISION_FAILURE_STORAGE_CORRUPT);

    provision_event_t reset = event_for(&manager, PROVISION_EVENT_FACTORY_RESET_REQUESTED);
    CHECK(provision_manager_handle_event(&manager, &reset) == PROVISION_TRANSITION_APPLIED);
    CHECK(provision_manager_snapshot(&manager).state == PROVISION_STATE_FACTORY_RESET_PENDING);

    provision_event_t complete = event_for(&manager, PROVISION_EVENT_FACTORY_RESET_COMPLETED);
    CHECK(provision_manager_handle_event(&manager, &complete) == PROVISION_TRANSITION_APPLIED);
    snapshot = provision_manager_snapshot(&manager);
    CHECK(snapshot.state == PROVISION_STATE_UNPROVISIONED);
    CHECK(snapshot.failure_reason == PROVISION_FAILURE_NONE);
    CHECK(snapshot.generation == generation + 1U);
}

int main(void) {
    test_unprovisioned_resume_waits_for_ble();
    test_happy_path_reaches_ready();
    test_retry_rejects_stale_callbacks();
    test_illegal_transition_preserves_state();
    test_suspend_detaches_ui_without_cancelling_work();
    test_repair_and_factory_reset_are_deterministic();

    if (s_failures != 0) {
        fprintf(stderr, "%d provision manager assertion(s) failed\n", s_failures);
        return 1;
    }
    puts("provision manager host tests passed");
    return 0;
}
