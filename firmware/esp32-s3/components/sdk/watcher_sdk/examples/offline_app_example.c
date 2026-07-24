/*
 * Minimal lifecycle example for an App Runtime Application.
 * Copy the callbacks into a real watcher_app_t; this file isn't built into the firmware.
 */
#include "watcher_sdk.h"

#include <stddef.h>

static watcher_sdk_context_t *s_sdk = NULL;

void example_sdk_app_on_open(void) {
    const watcher_sdk_config_t config = {.app_id = "example.offline.app"};
    watcher_sdk_job_id_t job_id = WATCHER_SDK_JOB_INVALID;

    if (watcher_sdk_open(&config, &s_sdk) == WATCHER_SDK_RESULT_OK) {
        (void)watcher_behavior_play(s_sdk, "greeting", 1U, &job_id);
    }
}

void example_sdk_app_on_tick(void) {
    watcher_sdk_event_t event;

    if (s_sdk == NULL) {
        return;
    }
    watcher_sdk_tick(s_sdk);
    while (watcher_sdk_poll_event(s_sdk, &event)) {
        if (event.state == WATCHER_SDK_JOB_FAILED) {
            /* The Application decides its own fallback behavior. */
        }
    }
}

void example_sdk_app_on_close(void) {
    watcher_sdk_close(s_sdk);
    s_sdk = NULL;
}
