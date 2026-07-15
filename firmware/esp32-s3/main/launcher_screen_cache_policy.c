#include "launcher_screen_cache_policy.h"

#include <stdint.h>

static bool threshold_with_reserve(size_t value, size_t threshold, size_t reserve) {
    return threshold <= SIZE_MAX - reserve && value >= threshold + reserve;
}

bool launcher_screen_cache_policy_allows(bool app_cacheable, size_t internal_free_bytes, size_t internal_largest_bytes,
                                         size_t dma_largest_bytes, const launcher_screen_cache_limits_t *limits) {
    if (!app_cacheable || limits == NULL) {
        return false;
    }

    return threshold_with_reserve(internal_free_bytes, limits->min_internal_free_bytes,
                                  limits->target_internal_reserve_bytes) &&
           threshold_with_reserve(internal_largest_bytes, limits->min_internal_largest_bytes,
                                  limits->target_largest_reserve_bytes) &&
           threshold_with_reserve(dma_largest_bytes, limits->min_dma_largest_bytes,
                                  limits->target_largest_reserve_bytes);
}
