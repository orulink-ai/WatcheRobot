#ifndef ANIM_FRAME_POOL_CORE_H
#define ANIM_FRAME_POOL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define ANIM_FRAME_POOL_CORE_MAX_SLOTS 12U
#define ANIM_FRAME_POOL_INVALID_INDEX UINT16_MAX

typedef struct {
    uint16_t capacity;
    uint16_t used_count;
    bool used[ANIM_FRAME_POOL_CORE_MAX_SLOTS];
} anim_frame_pool_core_t;

bool anim_frame_pool_core_init(anim_frame_pool_core_t *pool, uint16_t capacity);
bool anim_frame_pool_core_acquire(anim_frame_pool_core_t *pool, uint16_t count, uint16_t *indices);
void anim_frame_pool_core_release(anim_frame_pool_core_t *pool, uint16_t count, const uint16_t *indices);

#endif
