#ifndef PROVISION_MANAGER_H
#define PROVISION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROVISION_STATE_UNPROVISIONED = 0,
    PROVISION_STATE_WAIT_BLE,
    PROVISION_STATE_WIFI_CONFIGURING,
    PROVISION_STATE_WIFI_VALIDATING,
    PROVISION_STATE_READY,
    PROVISION_STATE_FAILED_RETRYABLE,
    PROVISION_STATE_REPAIR_REQUIRED,
    PROVISION_STATE_FACTORY_RESET_PENDING,
} provision_state_t;

typedef enum {
    PROVISION_FAILURE_NONE = 0,
    PROVISION_FAILURE_WIFI_AUTH,
    PROVISION_FAILURE_WIFI_TIMEOUT,
    PROVISION_FAILURE_STORAGE_CORRUPT,
    PROVISION_FAILURE_INTERNAL,
} provision_failure_reason_t;

typedef enum {
    PROVISION_EVENT_BLE_CONNECTED = 0,
    PROVISION_EVENT_CREDENTIALS_RECEIVED,
    PROVISION_EVENT_WIFI_CONNECT_STARTED,
    PROVISION_EVENT_WIFI_CONNECTED,
    PROVISION_EVENT_WIFI_FAILED,
    PROVISION_EVENT_RETRY,
    PROVISION_EVENT_STORAGE_CORRUPT,
    PROVISION_EVENT_FACTORY_RESET_REQUESTED,
    PROVISION_EVENT_FACTORY_RESET_COMPLETED,
} provision_event_type_t;

typedef enum {
    PROVISION_TRANSITION_APPLIED = 0,
    PROVISION_TRANSITION_IGNORED,
    PROVISION_TRANSITION_REJECTED,
    PROVISION_TRANSITION_STALE,
} provision_transition_result_t;

typedef struct {
    provision_event_type_t type;
    uint32_t generation;
    provision_failure_reason_t failure_reason;
} provision_event_t;

typedef struct {
    provision_state_t state;
    provision_failure_reason_t failure_reason;
    uint32_t generation;
    bool ui_attached;
} provision_snapshot_t;

typedef struct {
    provision_state_t state;
    provision_failure_reason_t failure_reason;
    uint32_t generation;
    bool ui_attached;
} provision_manager_t;

void provision_manager_init(provision_manager_t *manager, bool has_credentials, bool wifi_connected);
provision_transition_result_t provision_manager_resume(provision_manager_t *manager);
provision_transition_result_t provision_manager_suspend(provision_manager_t *manager);
provision_transition_result_t provision_manager_handle_event(provision_manager_t *manager,
                                                             const provision_event_t *event);
provision_snapshot_t provision_manager_snapshot(const provision_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif
