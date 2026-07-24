#include "watcher_sdk_core.h"

#include <string.h>

static bool job_is_terminal(watcher_sdk_job_state_t state) {
    return state == WATCHER_SDK_JOB_COMPLETED || state == WATCHER_SDK_JOB_FAILED || state == WATCHER_SDK_JOB_CANCELLED;
}

static watcher_sdk_job_entry_t *find_job(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    size_t index;

    if (core == NULL || job_id == WATCHER_SDK_JOB_INVALID) {
        return NULL;
    }
    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        if (core->jobs[index].id == job_id) {
            return &core->jobs[index];
        }
    }
    return NULL;
}

static const watcher_sdk_job_entry_t *find_job_const(const watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    return find_job((watcher_sdk_core_t *)core, job_id);
}

static void push_event(watcher_sdk_core_t *core, const watcher_sdk_job_entry_t *job, int error_code) {
    size_t tail;

    if (core == NULL || job == NULL) {
        return;
    }
    if (core->event_count == WATCHER_SDK_EVENT_CAPACITY) {
        core->event_head = (core->event_head + 1U) % WATCHER_SDK_EVENT_CAPACITY;
        core->event_count--;
        core->dropped_event_count++;
    }
    tail = (core->event_head + core->event_count) % WATCHER_SDK_EVENT_CAPACITY;
    core->events[tail].job_id = job->id;
    core->events[tail].domain = job->domain;
    core->events[tail].state = job->state;
    core->events[tail].error_code = error_code;
    core->event_count++;
}

static watcher_sdk_job_entry_t *allocate_job(watcher_sdk_core_t *core) {
    watcher_sdk_job_entry_t *terminal_slot = NULL;
    size_t index;

    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        if (core->jobs[index].id == WATCHER_SDK_JOB_INVALID) {
            return &core->jobs[index];
        }
        if (terminal_slot == NULL && job_is_terminal(core->jobs[index].state)) {
            terminal_slot = &core->jobs[index];
        }
    }
    return terminal_slot;
}

static watcher_sdk_job_id_t next_job_id(watcher_sdk_core_t *core) {
    watcher_sdk_job_id_t candidate;

    do {
        candidate = core->next_job_id++;
        if (core->next_job_id == WATCHER_SDK_JOB_INVALID) {
            core->next_job_id = 1U;
        }
    } while (candidate == WATCHER_SDK_JOB_INVALID || find_job(core, candidate) != NULL);
    return candidate;
}

static watcher_sdk_result_t transition_terminal(watcher_sdk_core_t *core, watcher_sdk_job_entry_t *job,
                                                watcher_sdk_job_state_t target, int error_code, bool invoke_cancel) {
    if (core == NULL || job == NULL) {
        return WATCHER_SDK_RESULT_NOT_FOUND;
    }
    if (job_is_terminal(job->state)) {
        return WATCHER_SDK_RESULT_OK;
    }
    if (invoke_cancel && core->config.cancel_domain != NULL) {
        core->config.cancel_domain(job->domain, core->config.executor_context);
    }
    job->state = target;
    job->timed = false;
    push_event(core, job, error_code);
    return WATCHER_SDK_RESULT_OK;
}

static void cancel_active_domain_jobs(watcher_sdk_core_t *core, watcher_sdk_domain_t domain) {
    size_t index;

    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        watcher_sdk_job_entry_t *job = &core->jobs[index];
        if (job->id != WATCHER_SDK_JOB_INVALID && job->domain == domain && !job_is_terminal(job->state)) {
            (void)transition_terminal(core, job, WATCHER_SDK_JOB_CANCELLED, 0, true);
        }
    }
}

static watcher_sdk_result_t start_job(watcher_sdk_core_t *core, watcher_sdk_domain_t domain, uint32_t now_ms,
                                      uint32_t duration_ms, watcher_sdk_job_id_t *out_job_id) {
    watcher_sdk_job_entry_t *job;

    if (core == NULL || !core->initialized || out_job_id == NULL || domain <= WATCHER_SDK_DOMAIN_NONE ||
        domain >= WATCHER_SDK_DOMAIN_COUNT) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    job = allocate_job(core);
    if (job == NULL) {
        return WATCHER_SDK_RESULT_NO_CAPACITY;
    }
    memset(job, 0, sizeof(*job));
    job->id = next_job_id(core);
    job->domain = domain;
    job->state = WATCHER_SDK_JOB_STARTING;
    job->timed = duration_ms > 0U;
    job->due_ms = now_ms + duration_ms;
    *out_job_id = job->id;
    push_event(core, job, 0);
    job->state = WATCHER_SDK_JOB_RUNNING;
    push_event(core, job, 0);
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_sdk_core_init(watcher_sdk_core_t *core, const watcher_sdk_core_config_t *config) {
    if (core == NULL || config == NULL) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    memset(core, 0, sizeof(*core));
    core->config = *config;
    core->next_job_id = 1U;
    core->initialized = true;
    return WATCHER_SDK_RESULT_OK;
}

void watcher_sdk_core_deinit(watcher_sdk_core_t *core) {
    if (core == NULL || !core->initialized) {
        return;
    }
    watcher_sdk_core_cancel_all(core);
    memset(core, 0, sizeof(*core));
}

watcher_sdk_result_t watcher_sdk_core_start_behavior(watcher_sdk_core_t *core, uint32_t now_ms,
                                                     watcher_sdk_job_id_t *out_job_id) {
    size_t index;

    if (core == NULL || !core->initialized) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        watcher_sdk_job_entry_t *job = &core->jobs[index];
        if (job->id != WATCHER_SDK_JOB_INVALID && !job_is_terminal(job->state)) {
            (void)transition_terminal(core, job, WATCHER_SDK_JOB_CANCELLED, 0, true);
        }
    }
    return start_job(core, WATCHER_SDK_DOMAIN_BEHAVIOR, now_ms, 0U, out_job_id);
}

watcher_sdk_result_t watcher_sdk_core_start_direct(watcher_sdk_core_t *core, watcher_sdk_domain_t domain,
                                                   uint32_t now_ms, uint32_t duration_ms,
                                                   watcher_sdk_job_id_t *out_job_id) {
    if (domain == WATCHER_SDK_DOMAIN_BEHAVIOR || domain == WATCHER_SDK_DOMAIN_MICROPHONE ||
        domain == WATCHER_SDK_DOMAIN_CAMERA) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (core == NULL || !core->initialized) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    (void)watcher_sdk_core_preempt_direct(core, domain);
    return start_job(core, domain, now_ms, duration_ms, out_job_id);
}

watcher_sdk_result_t watcher_sdk_core_complete(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    return transition_terminal(core, find_job(core, job_id), WATCHER_SDK_JOB_COMPLETED, 0, false);
}

watcher_sdk_result_t watcher_sdk_core_fail(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id, int error_code) {
    return transition_terminal(core, find_job(core, job_id), WATCHER_SDK_JOB_FAILED, error_code, true);
}

watcher_sdk_result_t watcher_sdk_core_cancel(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    return transition_terminal(core, find_job(core, job_id), WATCHER_SDK_JOB_CANCELLED, 0, true);
}

watcher_sdk_result_t watcher_sdk_core_cancel_observed(watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    return transition_terminal(core, find_job(core, job_id), WATCHER_SDK_JOB_CANCELLED, 0, false);
}

watcher_sdk_result_t watcher_sdk_core_cancel_domain(watcher_sdk_core_t *core, watcher_sdk_domain_t domain) {
    if (core == NULL || !core->initialized) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    if (domain <= WATCHER_SDK_DOMAIN_NONE || domain >= WATCHER_SDK_DOMAIN_COUNT) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    cancel_active_domain_jobs(core, domain);
    return WATCHER_SDK_RESULT_OK;
}

watcher_sdk_result_t watcher_sdk_core_preempt_direct(watcher_sdk_core_t *core, watcher_sdk_domain_t domain) {
    if (domain == WATCHER_SDK_DOMAIN_BEHAVIOR || domain == WATCHER_SDK_DOMAIN_MICROPHONE ||
        domain == WATCHER_SDK_DOMAIN_CAMERA) {
        return WATCHER_SDK_RESULT_INVALID_ARGUMENT;
    }
    if (core == NULL || !core->initialized) {
        return WATCHER_SDK_RESULT_INVALID_STATE;
    }
    cancel_active_domain_jobs(core, WATCHER_SDK_DOMAIN_BEHAVIOR);
    cancel_active_domain_jobs(core, domain);
    return WATCHER_SDK_RESULT_OK;
}

void watcher_sdk_core_cancel_all(watcher_sdk_core_t *core) {
    size_t index;

    if (core == NULL || !core->initialized) {
        return;
    }
    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        watcher_sdk_job_entry_t *job = &core->jobs[index];
        if (job->id != WATCHER_SDK_JOB_INVALID && !job_is_terminal(job->state)) {
            (void)transition_terminal(core, job, WATCHER_SDK_JOB_CANCELLED, 0, true);
        }
    }
}

void watcher_sdk_core_tick(watcher_sdk_core_t *core, uint32_t now_ms) {
    size_t index;

    if (core == NULL || !core->initialized) {
        return;
    }
    for (index = 0U; index < WATCHER_SDK_JOB_CAPACITY; ++index) {
        watcher_sdk_job_entry_t *job = &core->jobs[index];
        if (job->id != WATCHER_SDK_JOB_INVALID && job->state == WATCHER_SDK_JOB_RUNNING && job->timed &&
            (int32_t)(now_ms - job->due_ms) >= 0) {
            (void)transition_terminal(core, job, WATCHER_SDK_JOB_COMPLETED, 0, false);
        }
    }
}

watcher_sdk_job_state_t watcher_sdk_core_get_state(const watcher_sdk_core_t *core, watcher_sdk_job_id_t job_id) {
    const watcher_sdk_job_entry_t *job = find_job_const(core, job_id);
    return job != NULL ? job->state : WATCHER_SDK_JOB_UNKNOWN;
}

bool watcher_sdk_core_poll_event(watcher_sdk_core_t *core, watcher_sdk_event_t *out_event) {
    if (core == NULL || out_event == NULL || core->event_count == 0U) {
        return false;
    }
    *out_event = core->events[core->event_head];
    core->event_head = (core->event_head + 1U) % WATCHER_SDK_EVENT_CAPACITY;
    core->event_count--;
    return true;
}
