#ifndef ANIM_PREPARE_WATCHDOG_H
#define ANIM_PREPARE_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool armed;
    uint32_t generation;
    uint32_t ticket;
    uint64_t deadline_ms;
    uint32_t timeout_ms;
} anim_prepare_watchdog_t;

void anim_prepare_watchdog_reset(anim_prepare_watchdog_t *watchdog);
void anim_prepare_watchdog_start(anim_prepare_watchdog_t *watchdog, uint32_t generation, uint32_t ticket,
                                 uint64_t now_ms, uint32_t timeout_ms);
bool anim_prepare_watchdog_note_progress(anim_prepare_watchdog_t *watchdog, uint32_t generation, uint32_t ticket,
                                         uint64_t now_ms);
bool anim_prepare_watchdog_poll(anim_prepare_watchdog_t *watchdog, uint64_t now_ms, uint32_t *generation,
                                uint32_t *ticket);

#endif
