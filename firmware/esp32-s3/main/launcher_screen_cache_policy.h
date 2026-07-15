#ifndef LAUNCHER_SCREEN_CACHE_POLICY_H
#define LAUNCHER_SCREEN_CACHE_POLICY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t min_internal_free_bytes;
    size_t min_internal_largest_bytes;
    size_t min_dma_largest_bytes;
    size_t target_internal_reserve_bytes;
    size_t target_largest_reserve_bytes;
} launcher_screen_cache_limits_t;

bool launcher_screen_cache_policy_allows(bool app_cacheable, size_t internal_free_bytes, size_t internal_largest_bytes,
                                         size_t dma_largest_bytes, const launcher_screen_cache_limits_t *limits);

#endif /* LAUNCHER_SCREEN_CACHE_POLICY_H */
