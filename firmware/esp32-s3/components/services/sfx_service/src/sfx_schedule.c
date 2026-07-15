#include "sfx_schedule.h"

#include <string.h>

static void sfx_schedule_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static int sfx_schedule_find_due(const sfx_schedule_t *schedule, int64_t now_ms) {
    int selected = -1;
    size_t i;

    if (schedule == NULL) {
        return -1;
    }

    for (i = 0; i < SFX_SCHEDULE_CAPACITY; ++i) {
        const sfx_scheduled_request_t *request = &schedule->requests[i];

        if (!request->active || request->due_ms > now_ms) {
            continue;
        }
        if (selected < 0 || request->due_ms < schedule->requests[selected].due_ms ||
            (request->due_ms == schedule->requests[selected].due_ms &&
             request->sequence < schedule->requests[selected].sequence)) {
            selected = (int)i;
        }
    }
    return selected;
}

static int sfx_schedule_find_slot(const sfx_schedule_t *schedule) {
    int replace = 0;
    size_t i;

    if (schedule == NULL) {
        return -1;
    }

    for (i = 0; i < SFX_SCHEDULE_CAPACITY; ++i) {
        const sfx_scheduled_request_t *request = &schedule->requests[i];

        if (!request->active) {
            return (int)i;
        }
        if (request->due_ms > schedule->requests[replace].due_ms ||
            (request->due_ms == schedule->requests[replace].due_ms &&
             request->sequence > schedule->requests[replace].sequence)) {
            replace = (int)i;
        }
    }
    return replace;
}

void sfx_schedule_clear(sfx_schedule_t *schedule) {
    if (schedule == NULL) {
        return;
    }

    memset(schedule->requests, 0, sizeof(schedule->requests));
}

bool sfx_schedule_has_active(const sfx_schedule_t *schedule) {
    size_t i;

    if (schedule == NULL) {
        return false;
    }

    for (i = 0; i < SFX_SCHEDULE_CAPACITY; ++i) {
        if (schedule->requests[i].active) {
            return true;
        }
    }
    return false;
}

bool sfx_schedule_has_due(const sfx_schedule_t *schedule, int64_t now_ms) {
    return sfx_schedule_find_due(schedule, now_ms) >= 0;
}

bool sfx_schedule_enqueue(sfx_schedule_t *schedule, const char *sound_id, int64_t due_ms, char *replaced_sound_id,
                          size_t replaced_sound_id_size) {
    int slot;

    if (schedule == NULL || sound_id == NULL || sound_id[0] == '\0') {
        return false;
    }

    slot = sfx_schedule_find_slot(schedule);
    if (slot < 0) {
        return false;
    }

    if (replaced_sound_id != NULL && replaced_sound_id_size > 0U) {
        replaced_sound_id[0] = '\0';
        if (schedule->requests[slot].active) {
            sfx_schedule_copy_string(replaced_sound_id, replaced_sound_id_size, schedule->requests[slot].sound_id);
        }
    }

    schedule->next_sequence++;
    if (schedule->next_sequence == 0U) {
        schedule->next_sequence = 1U;
    }

    schedule->requests[slot].active = true;
    schedule->requests[slot].due_ms = due_ms;
    schedule->requests[slot].sequence = schedule->next_sequence;
    sfx_schedule_copy_string(schedule->requests[slot].sound_id, sizeof(schedule->requests[slot].sound_id), sound_id);
    return true;
}

bool sfx_schedule_take_due(sfx_schedule_t *schedule, int64_t now_ms, char *sound_id, size_t sound_id_size,
                           int64_t *late_ms) {
    int request_index;

    if (schedule == NULL) {
        return false;
    }

    request_index = sfx_schedule_find_due(schedule, now_ms);
    if (request_index < 0) {
        return false;
    }

    sfx_schedule_copy_string(sound_id, sound_id_size, schedule->requests[request_index].sound_id);
    if (late_ms != NULL) {
        *late_ms = now_ms - schedule->requests[request_index].due_ms;
    }
    schedule->requests[request_index].active = false;
    return true;
}
