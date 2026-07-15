#include "anim_frame_pool_core.h"

#include <stddef.h>
#include <string.h>

bool anim_frame_pool_core_init(anim_frame_pool_core_t *pool, uint16_t capacity) {
    if (pool == NULL || capacity == 0U || capacity > ANIM_FRAME_POOL_CORE_MAX_SLOTS) {
        return false;
    }
    memset(pool, 0, sizeof(*pool));
    pool->capacity = capacity;
    return true;
}

bool anim_frame_pool_core_acquire(anim_frame_pool_core_t *pool, uint16_t count, uint16_t *indices) {
    if (pool == NULL || indices == NULL || count == 0U || count > pool->capacity - pool->used_count) {
        return false;
    }

    uint16_t found = 0U;
    for (uint16_t index = 0U; index < pool->capacity && found < count; ++index) {
        if (!pool->used[index]) {
            indices[found++] = index;
        }
    }
    if (found != count) {
        return false;
    }
    for (uint16_t index = 0U; index < count; ++index) {
        pool->used[indices[index]] = true;
    }
    pool->used_count = (uint16_t)(pool->used_count + count);
    return true;
}

void anim_frame_pool_core_release(anim_frame_pool_core_t *pool, uint16_t count, const uint16_t *indices) {
    if (pool == NULL || indices == NULL) {
        return;
    }
    for (uint16_t index = 0U; index < count; ++index) {
        uint16_t slot = indices[index];
        if (slot < pool->capacity && pool->used[slot]) {
            pool->used[slot] = false;
            pool->used_count--;
        }
    }
}
