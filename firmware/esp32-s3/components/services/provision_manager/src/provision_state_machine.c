#include "provision_state_machine.h"

#include <limits.h>

static void advance_generation(provision_manager_t *manager) {
    manager->generation = manager->generation == UINT32_MAX ? 1U : manager->generation + 1U;
}

static provision_transition_result_t apply_global_event(provision_manager_t *manager, const provision_event_t *event) {
    if (event->type == PROVISION_EVENT_STORAGE_CORRUPT) {
        if (manager->state == PROVISION_STATE_REPAIR_REQUIRED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        manager->state = PROVISION_STATE_REPAIR_REQUIRED;
        manager->failure_reason = PROVISION_FAILURE_STORAGE_CORRUPT;
        return PROVISION_TRANSITION_APPLIED;
    }

    if (event->type == PROVISION_EVENT_FACTORY_RESET_REQUESTED) {
        if (manager->state == PROVISION_STATE_FACTORY_RESET_PENDING) {
            return PROVISION_TRANSITION_IGNORED;
        }
        manager->state = PROVISION_STATE_FACTORY_RESET_PENDING;
        return PROVISION_TRANSITION_APPLIED;
    }

    return PROVISION_TRANSITION_REJECTED;
}

static provision_transition_result_t apply_wifi_failure(provision_manager_t *manager, const provision_event_t *event) {
    manager->state = PROVISION_STATE_FAILED_RETRYABLE;
    manager->failure_reason =
        event->failure_reason == PROVISION_FAILURE_NONE ? PROVISION_FAILURE_INTERNAL : event->failure_reason;
    return PROVISION_TRANSITION_APPLIED;
}

provision_transition_result_t provision_state_machine_apply(provision_manager_t *manager,
                                                            const provision_event_t *event) {
    provision_transition_result_t global_result = apply_global_event(manager, event);
    if (global_result != PROVISION_TRANSITION_REJECTED) {
        return global_result;
    }

    switch (manager->state) {
    case PROVISION_STATE_UNPROVISIONED:
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_WAIT_BLE:
        if (event->type == PROVISION_EVENT_BLE_CONNECTED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        if (event->type == PROVISION_EVENT_CREDENTIALS_RECEIVED) {
            manager->state = PROVISION_STATE_WIFI_CONFIGURING;
            return PROVISION_TRANSITION_APPLIED;
        }
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_WIFI_CONFIGURING:
        if (event->type == PROVISION_EVENT_CREDENTIALS_RECEIVED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        if (event->type == PROVISION_EVENT_WIFI_CONNECT_STARTED) {
            manager->state = PROVISION_STATE_WIFI_VALIDATING;
            return PROVISION_TRANSITION_APPLIED;
        }
        if (event->type == PROVISION_EVENT_WIFI_CONNECTED) {
            manager->state = PROVISION_STATE_READY;
            manager->failure_reason = PROVISION_FAILURE_NONE;
            return PROVISION_TRANSITION_APPLIED;
        }
        if (event->type == PROVISION_EVENT_WIFI_FAILED) {
            return apply_wifi_failure(manager, event);
        }
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_WIFI_VALIDATING:
        if (event->type == PROVISION_EVENT_WIFI_CONNECT_STARTED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        if (event->type == PROVISION_EVENT_WIFI_CONNECTED) {
            manager->state = PROVISION_STATE_READY;
            manager->failure_reason = PROVISION_FAILURE_NONE;
            return PROVISION_TRANSITION_APPLIED;
        }
        if (event->type == PROVISION_EVENT_WIFI_FAILED) {
            return apply_wifi_failure(manager, event);
        }
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_READY:
        if (event->type == PROVISION_EVENT_WIFI_CONNECTED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        if (event->type == PROVISION_EVENT_CREDENTIALS_RECEIVED) {
            advance_generation(manager);
            manager->state = PROVISION_STATE_WIFI_CONFIGURING;
            manager->failure_reason = PROVISION_FAILURE_NONE;
            return PROVISION_TRANSITION_APPLIED;
        }
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_FAILED_RETRYABLE:
        if (event->type == PROVISION_EVENT_WIFI_FAILED) {
            return PROVISION_TRANSITION_IGNORED;
        }
        if (event->type == PROVISION_EVENT_RETRY) {
            advance_generation(manager);
            manager->state = PROVISION_STATE_WAIT_BLE;
            manager->failure_reason = PROVISION_FAILURE_NONE;
            return PROVISION_TRANSITION_APPLIED;
        }
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_REPAIR_REQUIRED:
        return PROVISION_TRANSITION_REJECTED;

    case PROVISION_STATE_FACTORY_RESET_PENDING:
        if (event->type == PROVISION_EVENT_FACTORY_RESET_COMPLETED) {
            advance_generation(manager);
            manager->state = PROVISION_STATE_UNPROVISIONED;
            manager->failure_reason = PROVISION_FAILURE_NONE;
            return PROVISION_TRANSITION_APPLIED;
        }
        return PROVISION_TRANSITION_REJECTED;
    }

    return PROVISION_TRANSITION_REJECTED;
}
