#include "anim_prepare_watchdog.h"

#include <string.h>

void anim_prepare_watchdog_reset(anim_prepare_watchdog_t *watchdog) {
    if (watchdog != NULL) {
        memset(watchdog, 0, sizeof(*watchdog));
    }
}

void anim_prepare_watchdog_start(anim_prepare_watchdog_t *watchdog, uint32_t generation, uint32_t ticket,
                                 uint64_t now_ms, uint32_t timeout_ms) {
    if (watchdog == NULL) {
        return;
    }
    watchdog->armed = true;
    watchdog->generation = generation;
    watchdog->ticket = ticket;
    watchdog->timeout_ms = timeout_ms;
    watchdog->deadline_ms = now_ms + timeout_ms;
}

bool anim_prepare_watchdog_note_progress(anim_prepare_watchdog_t *watchdog, uint32_t generation, uint32_t ticket,
                                         uint64_t now_ms) {
    if (watchdog == NULL || !watchdog->armed || watchdog->generation != generation || watchdog->ticket != ticket) {
        return false;
    }
    watchdog->deadline_ms = now_ms + watchdog->timeout_ms;
    return true;
}

bool anim_prepare_watchdog_poll(anim_prepare_watchdog_t *watchdog, uint64_t now_ms, uint32_t *generation,
                                uint32_t *ticket) {
    if (watchdog == NULL || !watchdog->armed || now_ms < watchdog->deadline_ms) {
        return false;
    }
    if (generation != NULL) {
        *generation = watchdog->generation;
    }
    if (ticket != NULL) {
        *ticket = watchdog->ticket;
    }
    watchdog->armed = false;
    return true;
}
