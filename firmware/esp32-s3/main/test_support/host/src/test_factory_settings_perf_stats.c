#include "factory_settings_perf_stats.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s actual=%lu expected=%lu\n", message, (unsigned long)actual, (unsigned long)expected);
        return 1;
    }
    return 0;
}

static int test_empty_and_reset_snapshot(void) {
    factory_settings_perf_stats_t stats;
    factory_settings_perf_snapshot_t snapshot;
    int failures = 0;

    factory_settings_perf_stats_reset(&stats);
    factory_settings_perf_stats_snapshot(&stats, &snapshot);
    failures += expect_u32(snapshot.frame_count, 0, "empty frame count");
    failures += expect_u32(snapshot.p95_ms, 0, "empty percentile");

    factory_settings_perf_stats_record(&stats, 17, 1000);
    factory_settings_perf_stats_reset(&stats);
    factory_settings_perf_stats_snapshot(&stats, &snapshot);
    failures += expect_u32(snapshot.frame_count, 0, "reset clears samples");
    failures += expect_u32(snapshot.pixel_count, 0, "reset clears pixels");
    return failures;
}

static int test_percentile_and_budget_boundaries(void) {
    factory_settings_perf_stats_t stats;
    factory_settings_perf_snapshot_t snapshot;
    int failures = 0;

    factory_settings_perf_stats_reset(&stats);
    for (uint32_t index = 0; index < 94; ++index) {
        factory_settings_perf_stats_record(&stats, 10, 100);
    }
    factory_settings_perf_stats_record(&stats, 30, 200);
    for (uint32_t index = 0; index < 5; ++index) {
        factory_settings_perf_stats_record(&stats, 31, 300);
    }
    factory_settings_perf_stats_snapshot(&stats, &snapshot);

    failures += expect_u32(snapshot.frame_count, 100, "records every frame");
    failures += expect_u32(snapshot.p95_ms, 30, "uses nearest-rank p95");
    failures += expect_u32(snapshot.max_ms, 31, "tracks maximum frame time");
    failures += expect_u32(snapshot.over_budget_count, 5, "30 ms meets the budget while 31 ms exceeds it");
    failures += expect_u32(snapshot.max_consecutive_over_budget, 5, "tracks consecutive over-budget frames");
    failures += expect_u32(snapshot.pixel_count, 94 * 100 + 200 + 5 * 300, "accumulates flushed pixels");
    return failures;
}

static int test_consecutive_budget_run_resets_and_large_values_clamp_percentile(void) {
    factory_settings_perf_stats_t stats;
    factory_settings_perf_snapshot_t snapshot;
    const uint32_t samples[] = {31, 45, 30, 90, 300, 12};
    int failures = 0;

    factory_settings_perf_stats_reset(&stats);
    for (size_t index = 0; index < sizeof(samples) / sizeof(samples[0]); ++index) {
        factory_settings_perf_stats_record(&stats, samples[index], 0);
    }
    factory_settings_perf_stats_snapshot(&stats, &snapshot);

    failures += expect_u32(snapshot.max_consecutive_over_budget, 2, "in-budget frame resets consecutive run");
    failures += expect_u32(snapshot.max_ms, 300, "maximum keeps full frame duration");
    failures += expect_true(snapshot.p95_ms >= FACTORY_SETTINGS_PERF_LAST_BUCKET_MS,
                            "large percentile reports the overflow bucket");
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_empty_and_reset_snapshot();
    failures += test_percentile_and_budget_boundaries();
    failures += test_consecutive_budget_run_resets_and_large_values_clamp_percentile();

    if (failures != 0) {
        fprintf(stderr, "%d factory settings perf stats host test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
