#ifndef WATCHER_SDK_CORE_H
#define WATCHER_SDK_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHER_SDK_JOB_CAPACITY 12U
#define WATCHER_SDK_EVENT_CAPACITY 32U

typedef uint32_t watcher_sdk_job_id_t;

#define WATCHER_SDK_JOB_INVALID ((watcher_sdk_job_id_t)0U)

typedef enum {
    WATCHER_SDK_RESULT_OK = 0,
    WATCHER_SDK_RESULT_INVALID_ARGUMENT = -1,
    WATCHER_SDK_RESULT_INVALID_STATE = -2,
    WATCHER_SDK_RESULT_BUSY = -3,
    WATCHER_SDK_RESULT_NOT_FOUND = -4,
    WATCHER_SDK_RESULT_NO_CAPACITY = -5,
    WATCHER_SDK_RESULT_EXECUTOR_FAILED = -6,
} watcher_sdk_result_t;

typedef enum {
    WATCHER_SDK_DOMAIN_NONE = 0,
    WATCHER_SDK_DOMAIN_BEHAVIOR,
    WATCHER_SDK_DOMAIN_ANIMATION,
    WATCHER_SDK_DOMAIN_MOTION,
    WATCHER_SDK_DOMAIN_AUDIO,
    WATCHER_SDK_DOMAIN_LIGHT,
    WATCHER_SDK_DOMAIN_MICROPHONE,
    WATCHER_SDK_DOMAIN_CAMERA,
    WATCHER_SDK_DOMAIN_COUNT,
} watcher_sdk_domain_t;

typedef enum {
    WATCHER_SDK_JOB_UNKNOWN = 0,
    WATCHER_SDK_JOB_STARTING,
    WATCHER_SDK_JOB_RUNNING,
    WATCHER_SDK_JOB_COMPLETED,
    WATCHER_SDK_JOB_FAILED,
    WATCHER_SDK_JOB_CANCELLED,
} watcher_sdk_job_state_t;

typedef struct {
    watcher_sdk_job_id_t job_id;
    watcher_sdk_domain_t domain;
    watcher_sdk_job_state_t state;
    int error_code;
} watcher_sdk_event_t;

typedef void (*watcher_sdk_cancel_domain_fn_t)(watcher_sdk_domain_t domain, void *context);

typedef struct {
    watcher_sdk_cancel_domain_fn_t cancel_domain;
    void *executor_context;
} watcher_sdk_core_config_t;

typedef struct {
    watcher_sdk_job_id_t id;
    watcher_sdk_domain_t domain;
    watcher_sdk_job_state_t state;
    uint32_t due_ms;
    bool timed;
} watcher_sdk_job_entry_t;

typedef struct {
    watcher_sdk_core_config_t config;
    watcher_sdk_job_entry_t jobs[WATCHER_SDK_JOB_CAPACITY];
    watcher_sdk_event_t events[WATCHER_SDK_EVENT_CAPACITY];
    watcher_sdk_job_id_t next_job_id;
    size_t event_head;
    size_t event_count;
    uint32_t dropped_event_count;
    bool initialized;
} watcher_sdk_core_t;

watcher_sdk_result_t watcher_sdk_core_init(watcher_sdk_core_t *core, const watcher_sdk_core_config_t *config);
void watcher_sdk_core_deinit(watcher_sdk_core_t *core);
watcher_sdk_result_t watcher_sdk_core_start_behavior(watcher_sdk_core_t *core, uint32_t now_ms,
                                                     watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_sdk_core_start_direct(watcher_sdk_core_t *core, watcher_sdk_domain_t domain,
                                                   uint32_t now_ms, uint32_t duration_ms,
                                                   watcher_sdk_job_id_t *out_job_id);
watcher_sdk_result_t watcher_sdk_core_complete(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id);
watcher_sdk_result_t watcher_sdk_core_fail(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id, int error_code);
watcher_sdk_result_t watcher_sdk_core_cancel(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id);
watcher_sdk_result_t watcher_sdk_core_cancel_observed(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id);
watcher_sdk_result_t watcher_sdk_core_cancel_domain(watcher_sdk_core_t *core, watcher_sdk_domain_t domain);
watcher_sdk_result_t watcher_sdk_core_preempt_direct(watcher_sdk_core_t *core, watcher_sdk_domain_t domain);
void watcher_sdk_core_cancel_all(watcher_sdk_core_t *core);
void watcher_sdk_core_tick(watcher_sdk_core_t *core, uint32_t now_ms);
watcher_sdk_job_state_t watcher_sdk_core_get_state(const watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id);
bool watcher_sdk_core_poll_event(watcher_sdk_core_t *core, watcher_sdk_event_t *out_event);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_CORE_H */
