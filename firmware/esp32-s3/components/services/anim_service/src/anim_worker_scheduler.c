#include "anim_worker_scheduler.h"

#include <stddef.h>

static bool can_load(bool in_use, bool loadable, uint16_t buffered, uint16_t capacity) {
    return in_use && loadable && capacity > 0U && buffered < capacity;
}

anim_worker_target_t anim_worker_schedule(const anim_worker_schedule_input_t *input) {
    if (input == NULL) {
        return ANIM_WORK_NONE;
    }

    bool active_can_load =
        can_load(input->active_in_use, input->active_loadable, input->active_buffered, input->active_capacity);
    bool staging_can_load =
        can_load(input->staging_in_use, input->staging_loadable, input->staging_buffered, input->staging_capacity);
    bool prefetch_can_load =
        can_load(input->prefetch_in_use, input->prefetch_loadable, input->prefetch_buffered, input->prefetch_capacity);
    uint16_t staging_goal =
        input->staging_prefill < input->staging_capacity ? input->staging_prefill : input->staging_capacity;

    if (active_can_load && (input->active_buffered <= input->active_low_water || input->active_deadline_near)) {
        return ANIM_WORK_ACTIVE;
    }
    if (staging_can_load && input->staging_buffered < staging_goal) {
        return ANIM_WORK_STAGING;
    }
    if (active_can_load) {
        return ANIM_WORK_ACTIVE;
    }
    if (prefetch_can_load) {
        return ANIM_WORK_PREFETCH;
    }
    return ANIM_WORK_NONE;
}
