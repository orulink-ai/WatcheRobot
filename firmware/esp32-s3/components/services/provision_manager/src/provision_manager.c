#include "provision_manager.h"

#include "provision_state_machine.h"

#include <string.h>

void provision_manager_init(provision_manager_t *manager, bool has_credentials, bool wifi_connected) {
    if (manager == NULL) {
        return;
    }

    memset(manager, 0, sizeof(*manager));
    manager->generation = 1U;
    if (wifi_connected) {
        manager->state = PROVISION_STATE_READY;
    } else if (has_credentials) {
        manager->state = PROVISION_STATE_WIFI_VALIDATING;
    } else {
        manager->state = PROVISION_STATE_UNPROVISIONED;
    }
}

provision_transition_result_t provision_manager_resume(provision_manager_t *manager) {
    if (manager == NULL || manager->generation == 0U) {
        return PROVISION_TRANSITION_REJECTED;
    }
    if (manager->ui_attached) {
        return PROVISION_TRANSITION_IGNORED;
    }

    manager->ui_attached = true;
    if (manager->state == PROVISION_STATE_UNPROVISIONED) {
        manager->state = PROVISION_STATE_WAIT_BLE;
    }
    return PROVISION_TRANSITION_APPLIED;
}

provision_transition_result_t provision_manager_suspend(provision_manager_t *manager) {
    if (manager == NULL || manager->generation == 0U) {
        return PROVISION_TRANSITION_REJECTED;
    }
    if (!manager->ui_attached) {
        return PROVISION_TRANSITION_IGNORED;
    }

    manager->ui_attached = false;
    return PROVISION_TRANSITION_APPLIED;
}

provision_transition_result_t provision_manager_handle_event(provision_manager_t *manager,
                                                             const provision_event_t *event) {
    if (manager == NULL || event == NULL || manager->generation == 0U) {
        return PROVISION_TRANSITION_REJECTED;
    }
    if (event->generation != manager->generation) {
        return PROVISION_TRANSITION_STALE;
    }
    return provision_state_machine_apply(manager, event);
}

provision_snapshot_t provision_manager_snapshot(const provision_manager_t *manager) {
    provision_snapshot_t snapshot = {0};
    if (manager == NULL) {
        return snapshot;
    }

    snapshot.state = manager->state;
    snapshot.failure_reason = manager->failure_reason;
    snapshot.generation = manager->generation;
    snapshot.ui_attached = manager->ui_attached;
    return snapshot;
}
