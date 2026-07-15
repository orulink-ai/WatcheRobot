#ifndef WATCHER_FACTORY_SETTINGS_PERF_STATS_H
#define WATCHER_FACTORY_SETTINGS_PERF_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACTORY_SETTINGS_PERF_FRAME_BUDGET_MS 30U
#define FACTORY_SETTINGS_PERF_LAST_BUCKET_MS 255U
#define FACTORY_SETTINGS_PERF_BUCKET_COUNT (FACTORY_SETTINGS_PERF_LAST_BUCKET_MS + 1U)

typedef struct {
    uint32_t frame_time_histogram[FACTORY_SETTINGS_PERF_BUCKET_COUNT];
    uint32_t frame_count;
    uint32_t pixel_count;
    uint32_t max_ms;
    uint32_t over_budget_count;
    uint32_t current_consecutive_over_budget;
    uint32_t max_consecutive_over_budget;
} factory_settings_perf_stats_t;

typedef struct {
    uint32_t frame_count;
    uint32_t pixel_count;
    uint32_t p95_ms;
    uint32_t max_ms;
    uint32_t over_budget_count;
    uint32_t max_consecutive_over_budget;
} factory_settings_perf_snapshot_t;

void factory_settings_perf_stats_reset(factory_settings_perf_stats_t *stats);
void factory_settings_perf_stats_record(factory_settings_perf_stats_t *stats, uint32_t frame_time_ms,
                                        uint32_t pixel_count);
void factory_settings_perf_stats_snapshot(const factory_settings_perf_stats_t *stats,
                                          factory_settings_perf_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
