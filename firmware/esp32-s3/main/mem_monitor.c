#include "mem_monitor.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "MEM_MON"

#if CONFIG_WATCHER_MEM_MONITOR_ENABLE

#define MEM_MONITOR_WARN_INTERNAL_FREE_BYTES ((size_t)CONFIG_WATCHER_MEM_MONITOR_WARN_INTERNAL_FREE_KB * 1024U)
#define MEM_MONITOR_WARN_INTERNAL_LARGEST_BYTES ((size_t)CONFIG_WATCHER_MEM_MONITOR_WARN_INTERNAL_LARGEST_KB * 1024U)
#define MEM_MONITOR_CRITICAL_INTERNAL_FREE_BYTES ((size_t)CONFIG_WATCHER_MEM_MONITOR_CRITICAL_INTERNAL_FREE_KB * 1024U)
#define MEM_MONITOR_CRITICAL_INTERNAL_LARGEST_BYTES                                                                    \
    ((size_t)CONFIG_WATCHER_MEM_MONITOR_CRITICAL_INTERNAL_LARGEST_KB * 1024U)
#define MEM_MONITOR_WARN_DMA_LARGEST_BYTES ((size_t)CONFIG_WATCHER_MEM_MONITOR_WARN_DMA_LARGEST_KB * 1024U)
#define MEM_MONITOR_CRITICAL_DMA_LARGEST_BYTES ((size_t)CONFIG_WATCHER_MEM_MONITOR_CRITICAL_DMA_LARGEST_KB * 1024U)
#define MEM_MONITOR_TASK_PRIORITY 1
#define MEM_MONITOR_INTEGRITY_MIN_STACK_HWM 1024U
#ifdef CONFIG_WATCHER_MEM_MONITOR_TASK_STACK_SIZE
#define MEM_MONITOR_TASK_STACK_SIZE CONFIG_WATCHER_MEM_MONITOR_TASK_STACK_SIZE
#else
#define MEM_MONITOR_TASK_STACK_SIZE 2048
#endif

typedef enum {
    MEM_MONITOR_LEVEL_OK = 0,
    MEM_MONITOR_LEVEL_WARN,
    MEM_MONITOR_LEVEL_CRITICAL,
} mem_monitor_level_t;

typedef struct {
    multi_heap_info_t heap_8bit;
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    multi_heap_info_t spiram;
    size_t min_8bit;
    size_t min_internal;
    size_t min_dma;
    size_t min_spiram;
    int64_t captured_at_us;
} mem_monitor_snapshot_t;

static StaticSemaphore_t s_monitor_lock_buffer;
static SemaphoreHandle_t s_monitor_lock = NULL;
static StaticTask_t *s_monitor_task_tcb = NULL;
static StackType_t *s_monitor_task_stack = NULL;
static TaskHandle_t s_monitor_task = NULL;
static mem_monitor_level_t s_last_level = MEM_MONITOR_LEVEL_OK;
static int64_t s_last_alert_log_us = 0;
#if CONFIG_WATCHER_MEM_MONITOR_CHECK_INTEGRITY_ON_CRITICAL
static int64_t s_last_integrity_check_us = 0;
#endif

static bool mem_monitor_lock_take(void) {
    if (s_monitor_lock == NULL) {
        s_monitor_lock = xSemaphoreCreateMutexStatic(&s_monitor_lock_buffer);
        if (s_monitor_lock == NULL) {
            return false;
        }
    }

    return xSemaphoreTake(s_monitor_lock, portMAX_DELAY) == pdTRUE;
}

static void mem_monitor_lock_give(void) {
    if (s_monitor_lock != NULL) {
        xSemaphoreGive(s_monitor_lock);
    }
}

static const char *mem_monitor_level_to_string(mem_monitor_level_t level) {
    switch (level) {
    case MEM_MONITOR_LEVEL_CRITICAL:
        return "critical";
    case MEM_MONITOR_LEVEL_WARN:
        return "warn";
    case MEM_MONITOR_LEVEL_OK:
    default:
        return "ok";
    }
}

static unsigned mem_monitor_bytes_to_kb(size_t bytes) {
    return (unsigned)(bytes / 1024U);
}

static void mem_monitor_collect_heap_caps(uint32_t caps, multi_heap_info_t *info, size_t *minimum_free) {
    if (info == NULL || minimum_free == NULL) {
        return;
    }

    memset(info, 0, sizeof(*info));
    heap_caps_get_info(info, caps);
    *minimum_free = heap_caps_get_minimum_free_size(caps);
}

static void mem_monitor_collect_snapshot(mem_monitor_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    mem_monitor_collect_heap_caps(MALLOC_CAP_8BIT, &snapshot->heap_8bit, &snapshot->min_8bit);
    mem_monitor_collect_heap_caps(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, &snapshot->internal, &snapshot->min_internal);
    mem_monitor_collect_heap_caps(MALLOC_CAP_DMA, &snapshot->dma, &snapshot->min_dma);
#if CONFIG_SPIRAM
    mem_monitor_collect_heap_caps(MALLOC_CAP_SPIRAM, &snapshot->spiram, &snapshot->min_spiram);
#endif
    snapshot->captured_at_us = esp_timer_get_time();
}

static mem_monitor_level_t mem_monitor_get_level(const mem_monitor_snapshot_t *snapshot, const char **reason_out) {
    const char *reason = "none";

    if (snapshot == NULL) {
        if (reason_out != NULL) {
            *reason_out = "no_snapshot";
        }
        return MEM_MONITOR_LEVEL_OK;
    }

    if (snapshot->internal.total_free_bytes <= MEM_MONITOR_CRITICAL_INTERNAL_FREE_BYTES) {
        reason = "internal_free";
        goto critical;
    }
    if (snapshot->internal.largest_free_block <= MEM_MONITOR_CRITICAL_INTERNAL_LARGEST_BYTES) {
        reason = "internal_largest";
        goto critical;
    }
    if (snapshot->dma.largest_free_block <= MEM_MONITOR_CRITICAL_DMA_LARGEST_BYTES) {
        reason = "dma_largest";
        goto critical;
    }

    if (snapshot->internal.total_free_bytes <= MEM_MONITOR_WARN_INTERNAL_FREE_BYTES) {
        reason = "internal_free";
        goto warn;
    }
    if (snapshot->internal.largest_free_block <= MEM_MONITOR_WARN_INTERNAL_LARGEST_BYTES) {
        reason = "internal_largest";
        goto warn;
    }
    if (snapshot->dma.largest_free_block <= MEM_MONITOR_WARN_DMA_LARGEST_BYTES) {
        reason = "dma_largest";
        goto warn;
    }

    if (reason_out != NULL) {
        *reason_out = reason;
    }
    return MEM_MONITOR_LEVEL_OK;

critical:
    if (reason_out != NULL) {
        *reason_out = reason;
    }
    return MEM_MONITOR_LEVEL_CRITICAL;

warn:
    if (reason_out != NULL) {
        *reason_out = reason;
    }
    return MEM_MONITOR_LEVEL_WARN;
}

static void mem_monitor_log_snapshot(const char *stage, const mem_monitor_snapshot_t *snapshot,
                                     mem_monitor_level_t level, const char *reason, bool recovery) {
    const char *safe_stage = (stage != NULL && stage[0] != '\0') ? stage : "periodic";
    const char *safe_reason = (reason != NULL && reason[0] != '\0') ? reason : "none";
    UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);

    if (snapshot == NULL) {
        ESP_LOGW(TAG, "[%s] heap snapshot unavailable", safe_stage);
        return;
    }

    const char *status = recovery ? "recovered" : mem_monitor_level_to_string(level);

    if (level == MEM_MONITOR_LEVEL_CRITICAL && !recovery) {
        ESP_LOGE(
            TAG,
            "[%s] %s(%s) int{free=%uKB largest=%uKB min=%uKB} dma{free=%uKB largest=%uKB min=%uKB} "
            "8bit{free=%uKB largest=%uKB min=%uKB}"
#if CONFIG_SPIRAM
            " psram{free=%uKB largest=%uKB min=%uKB}"
#endif
            " stack_hwm=%u",
            safe_stage, status, safe_reason, mem_monitor_bytes_to_kb(snapshot->internal.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->internal.largest_free_block),
            mem_monitor_bytes_to_kb(snapshot->min_internal), mem_monitor_bytes_to_kb(snapshot->dma.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->dma.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_dma),
            mem_monitor_bytes_to_kb(snapshot->heap_8bit.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->heap_8bit.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_8bit)
#if CONFIG_SPIRAM
                                                                                 ,
            mem_monitor_bytes_to_kb(snapshot->spiram.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->spiram.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_spiram)
#endif
                                                                              ,
            (unsigned)stack_hwm);
        return;
    }

    if (level == MEM_MONITOR_LEVEL_WARN && !recovery) {
        ESP_LOGW(
            TAG,
            "[%s] %s(%s) int{free=%uKB largest=%uKB min=%uKB} dma{free=%uKB largest=%uKB min=%uKB} "
            "8bit{free=%uKB largest=%uKB min=%uKB}"
#if CONFIG_SPIRAM
            " psram{free=%uKB largest=%uKB min=%uKB}"
#endif
            " stack_hwm=%u",
            safe_stage, status, safe_reason, mem_monitor_bytes_to_kb(snapshot->internal.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->internal.largest_free_block),
            mem_monitor_bytes_to_kb(snapshot->min_internal), mem_monitor_bytes_to_kb(snapshot->dma.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->dma.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_dma),
            mem_monitor_bytes_to_kb(snapshot->heap_8bit.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->heap_8bit.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_8bit)
#if CONFIG_SPIRAM
                                                                                 ,
            mem_monitor_bytes_to_kb(snapshot->spiram.total_free_bytes),
            mem_monitor_bytes_to_kb(snapshot->spiram.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_spiram)
#endif
                                                                              ,
            (unsigned)stack_hwm);
        return;
    }

    ESP_LOGI(TAG,
             "[%s] %s int{free=%uKB largest=%uKB min=%uKB} dma{free=%uKB largest=%uKB min=%uKB} "
             "8bit{free=%uKB largest=%uKB min=%uKB}"
#if CONFIG_SPIRAM
             " psram{free=%uKB largest=%uKB min=%uKB}"
#endif
             " stack_hwm=%u",
             safe_stage, status, mem_monitor_bytes_to_kb(snapshot->internal.total_free_bytes),
             mem_monitor_bytes_to_kb(snapshot->internal.largest_free_block),
             mem_monitor_bytes_to_kb(snapshot->min_internal), mem_monitor_bytes_to_kb(snapshot->dma.total_free_bytes),
             mem_monitor_bytes_to_kb(snapshot->dma.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_dma),
             mem_monitor_bytes_to_kb(snapshot->heap_8bit.total_free_bytes),
             mem_monitor_bytes_to_kb(snapshot->heap_8bit.largest_free_block),
             mem_monitor_bytes_to_kb(snapshot->min_8bit)
#if CONFIG_SPIRAM
                 ,
             mem_monitor_bytes_to_kb(snapshot->spiram.total_free_bytes),
             mem_monitor_bytes_to_kb(snapshot->spiram.largest_free_block), mem_monitor_bytes_to_kb(snapshot->min_spiram)
#endif
                                                                               ,
             (unsigned)stack_hwm);
}

#if CONFIG_WATCHER_MEM_MONITOR_CHECK_INTEGRITY_ON_CRITICAL
static bool mem_monitor_has_integrity_stack_headroom(void) {
    UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    return stack_hwm >= MEM_MONITOR_INTEGRITY_MIN_STACK_HWM;
}
#endif

static bool mem_monitor_should_repeat_alert(int64_t now_us) {
    int64_t repeat_us = (int64_t)CONFIG_WATCHER_MEM_MONITOR_ALERT_REPEAT_MS * 1000LL;
    return s_last_alert_log_us == 0 || (now_us - s_last_alert_log_us) >= repeat_us;
}

#if CONFIG_WATCHER_MEM_MONITOR_CHECK_INTEGRITY_ON_CRITICAL
static bool mem_monitor_should_run_integrity_check(int64_t now_us) {
    int64_t repeat_us = (int64_t)CONFIG_WATCHER_MEM_MONITOR_ALERT_REPEAT_MS * 1000LL;
    return s_last_integrity_check_us == 0 || (now_us - s_last_integrity_check_us) >= repeat_us;
}
#endif

static void mem_monitor_sample(const char *stage, bool force_log) {
    const char *reason = NULL;
    mem_monitor_snapshot_t snapshot;
    mem_monitor_level_t level;
    bool recovery = false;
    bool should_log = force_log;

    if (!mem_monitor_lock_take()) {
        ESP_LOGW(TAG, "[%s] monitor lock unavailable", stage ? stage : "periodic");
        return;
    }

    mem_monitor_collect_snapshot(&snapshot);
    level = mem_monitor_get_level(&snapshot, &reason);

    if (!force_log) {
        if (level == MEM_MONITOR_LEVEL_OK && s_last_level != MEM_MONITOR_LEVEL_OK) {
            recovery = true;
            should_log = true;
        } else if (level != MEM_MONITOR_LEVEL_OK &&
                   (level != s_last_level || mem_monitor_should_repeat_alert(snapshot.captured_at_us))) {
            should_log = true;
        }
    }

    if (should_log) {
        mem_monitor_log_snapshot(stage, &snapshot, level, reason, recovery);
        if (!recovery && level != MEM_MONITOR_LEVEL_OK) {
            s_last_alert_log_us = snapshot.captured_at_us;
        }
    }

#if CONFIG_WATCHER_MEM_MONITOR_CHECK_INTEGRITY_ON_CRITICAL
    if (level == MEM_MONITOR_LEVEL_CRITICAL && mem_monitor_should_run_integrity_check(snapshot.captured_at_us)) {
        if (!mem_monitor_has_integrity_stack_headroom()) {
            ESP_LOGW(TAG, "[%s] heap integrity skipped: low monitor stack hwm=%u", stage ? stage : "periodic",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            s_last_integrity_check_us = snapshot.captured_at_us;
            s_last_level = level;
            mem_monitor_lock_give();
            return;
        }
        bool ok = heap_caps_check_integrity_all(false);
        s_last_integrity_check_us = snapshot.captured_at_us;
        if (ok) {
            ESP_LOGW(TAG, "[%s] heap integrity OK during critical pressure", stage ? stage : "periodic");
        } else {
            ESP_LOGE(TAG, "[%s] heap integrity FAILED during critical pressure", stage ? stage : "periodic");
        }
    }
#endif

    s_last_level = level;
    mem_monitor_lock_give();
}

static void mem_monitor_task(void *arg) {
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WATCHER_MEM_MONITOR_PERIOD_MS));
        mem_monitor_sample("periodic", false);
    }
}

void mem_monitor_init(void) {
    if (s_monitor_lock == NULL) {
        s_monitor_lock = xSemaphoreCreateMutexStatic(&s_monitor_lock_buffer);
    }

    if (s_monitor_task != NULL) {
        return;
    }

    s_monitor_task_tcb =
        (StaticTask_t *)heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_monitor_task_tcb == NULL) {
        ESP_LOGW(TAG, "memory monitor task TCB allocation failed");
        return;
    }

#if CONFIG_SPIRAM
    s_monitor_task_stack = (StackType_t *)heap_caps_calloc(1, (size_t)MEM_MONITOR_TASK_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    s_monitor_task_stack = (StackType_t *)heap_caps_calloc(1, (size_t)MEM_MONITOR_TASK_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (s_monitor_task_stack == NULL) {
        ESP_LOGW(TAG, "memory monitor task stack allocation failed");
        free(s_monitor_task_tcb);
        s_monitor_task_tcb = NULL;
        return;
    }

    s_monitor_task = xTaskCreateStatic(mem_monitor_task, "mem_monitor", MEM_MONITOR_TASK_STACK_SIZE, NULL,
                                       MEM_MONITOR_TASK_PRIORITY, s_monitor_task_stack, s_monitor_task_tcb);
    if (s_monitor_task == NULL) {
        ESP_LOGW(TAG, "memory monitor task create failed");
        free(s_monitor_task_stack);
        free(s_monitor_task_tcb);
        s_monitor_task_stack = NULL;
        s_monitor_task_tcb = NULL;
    }
}

void mem_monitor_snapshot(const char *stage) {
    mem_monitor_sample(stage, CONFIG_WATCHER_MEM_MONITOR_LOG_STAGE_SNAPSHOTS);
}

bool mem_monitor_check_integrity(const char *stage) {
    if (!mem_monitor_lock_take()) {
        ESP_LOGW(TAG, "[%s] monitor lock unavailable for integrity check", stage ? stage : "manual");
        return false;
    }

    bool ok = heap_caps_check_integrity_all(true);

    if (ok) {
        ESP_LOGI(TAG, "[%s] heap integrity OK", stage ? stage : "manual");
    } else {
        ESP_LOGE(TAG, "[%s] heap integrity FAILED", stage ? stage : "manual");
    }

    mem_monitor_lock_give();
    return ok;
}

#else

void mem_monitor_init(void) {}

void mem_monitor_snapshot(const char *stage) {
    (void)stage;
}

bool mem_monitor_check_integrity(const char *stage) {
    (void)stage;
    return true;
}

#endif
