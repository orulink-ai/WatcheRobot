#ifndef ANIM_WORKER_SCHEDULER_H
#define ANIM_WORKER_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ANIM_WORK_NONE = 0,
    ANIM_WORK_ACTIVE,
    ANIM_WORK_STAGING,
    ANIM_WORK_PREFETCH,
} anim_worker_target_t;

typedef struct {
    bool active_in_use;
    bool active_loadable;
    uint16_t active_buffered;
    uint16_t active_capacity;
    bool active_deadline_near;
    bool staging_in_use;
    bool staging_loadable;
    uint16_t staging_buffered;
    uint16_t staging_capacity;
    bool prefetch_in_use;
    bool prefetch_loadable;
    uint16_t prefetch_buffered;
    uint16_t prefetch_capacity;
    uint16_t active_low_water;
    uint16_t staging_prefill;
} anim_worker_schedule_input_t;

anim_worker_target_t anim_worker_schedule(const anim_worker_schedule_input_t *input);

#endif
