#include "factory_settings_perf_stats.h"

#include <stddef.h>
#include <string.h>

void factory_settings_perf_stats_reset(factory_settings_perf_stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

void factory_settings_perf_stats_record(factory_settings_perf_stats_t *stats, uint32_t frame_time_ms,
                                        uint32_t pixel_count) {
    uint32_t bucket;

    if (stats == NULL) {
        return;
    }

    bucket =
        frame_time_ms < FACTORY_SETTINGS_PERF_LAST_BUCKET_MS ? frame_time_ms : FACTORY_SETTINGS_PERF_LAST_BUCKET_MS;
    stats->frame_time_histogram[bucket]++;
    stats->frame_count++;
    stats->pixel_count += pixel_count;
    if (frame_time_ms > stats->max_ms) {
        stats->max_ms = frame_time_ms;
    }
    if (frame_time_ms > FACTORY_SETTINGS_PERF_FRAME_BUDGET_MS) {
        stats->over_budget_count++;
        stats->current_consecutive_over_budget++;
        if (stats->current_consecutive_over_budget > stats->max_consecutive_over_budget) {
            stats->max_consecutive_over_budget = stats->current_consecutive_over_budget;
        }
    } else {
        stats->current_consecutive_over_budget = 0;
    }
}

void factory_settings_perf_stats_snapshot(const factory_settings_perf_stats_t *stats,
                                          factory_settings_perf_snapshot_t *snapshot) {
    uint64_t percentile_rank;
    uint64_t cumulative = 0;

    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (stats == NULL || stats->frame_count == 0) {
        return;
    }

    percentile_rank = ((uint64_t)stats->frame_count * 95U + 99U) / 100U;
    for (uint32_t bucket = 0; bucket < FACTORY_SETTINGS_PERF_BUCKET_COUNT; ++bucket) {
        cumulative += stats->frame_time_histogram[bucket];
        if (cumulative >= percentile_rank) {
            snapshot->p95_ms = bucket;
            break;
        }
    }

    snapshot->frame_count = stats->frame_count;
    snapshot->pixel_count = stats->pixel_count;
    snapshot->max_ms = stats->max_ms;
    snapshot->over_budget_count = stats->over_budget_count;
    snapshot->max_consecutive_over_budget = stats->max_consecutive_over_budget;
}
