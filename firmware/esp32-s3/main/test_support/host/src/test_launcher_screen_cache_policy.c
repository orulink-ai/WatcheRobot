#include "launcher_screen_cache_policy.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    const launcher_screen_cache_limits_t limits = {
        .min_internal_free_bytes = 112U * 1024U,
        .min_internal_largest_bytes = 40U * 1024U,
        .min_dma_largest_bytes = 40U * 1024U,
    };

    failures += expect_true(launcher_screen_cache_policy_allows(true, 120U * 1024U, 54U * 1024U, 54U * 1024U, &limits),
                            "healthy heap allows cache");
    failures += expect_true(launcher_screen_cache_policy_allows(true, 112U * 1024U, 40U * 1024U, 40U * 1024U, &limits),
                            "values exactly at thresholds allow cache");
    failures +=
        expect_true(!launcher_screen_cache_policy_allows(false, 120U * 1024U, 54U * 1024U, 54U * 1024U, &limits),
                    "non-cacheable app never retains launcher");
    failures += expect_true(!launcher_screen_cache_policy_allows(true, 94U * 1024U, 54U * 1024U, 54U * 1024U, &limits),
                            "low internal free rejects cache");
    failures += expect_true(!launcher_screen_cache_policy_allows(true, 120U * 1024U, 32U * 1024U, 54U * 1024U, &limits),
                            "small internal block rejects cache");
    failures += expect_true(!launcher_screen_cache_policy_allows(true, 120U * 1024U, 54U * 1024U, 32U * 1024U, &limits),
                            "small DMA block rejects cache");
    failures += expect_true(!launcher_screen_cache_policy_allows(true, 120U * 1024U, 54U * 1024U, 54U * 1024U, NULL),
                            "missing limits fail closed");
    {
        const launcher_screen_cache_limits_t heavy_limits = {
            .min_internal_free_bytes = 112U * 1024U,
            .min_internal_largest_bytes = 40U * 1024U,
            .min_dma_largest_bytes = 40U * 1024U,
            .target_internal_reserve_bytes = 40U * 1024U,
            .target_largest_reserve_bytes = 16U * 1024U,
        };
        failures += expect_true(
            !launcher_screen_cache_policy_allows(true, 143U * 1024U, 88U * 1024U, 88U * 1024U, &heavy_limits),
            "heavy target reserve protects current 143KB launcher baseline");
        failures += expect_true(
            launcher_screen_cache_policy_allows(true, 152U * 1024U, 56U * 1024U, 56U * 1024U, &heavy_limits),
            "heavy target cache allowed exactly at reserve-adjusted thresholds");
    }
    return failures == 0 ? 0 : 1;
}
