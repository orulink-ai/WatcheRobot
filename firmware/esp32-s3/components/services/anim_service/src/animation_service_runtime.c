#include "animation_service.h"

#include "anim_player_internal.h"
#include "anim_player_private.h"
#include "animation_service_sync_adapter.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

#define ANIMATION_RUNTIME_COMMAND_QUEUE_CAPACITY 8U
#define ANIMATION_RUNTIME_PLAYER_EVENT_QUEUE_CAPACITY 16U
#define ANIMATION_RUNTIME_TASK_STACK_BYTES 8192U
#define ANIMATION_RUNTIME_STACK_WARN_FREE_BYTES 2048U
#define ANIMATION_RUNTIME_TASK_PRIORITY 5U
#define ANIMATION_RUNTIME_WATCHDOG_TICK_MS 20U
#define ANIMATION_RUNTIME_TIMING_CAPACITY ANIMATION_CONTROLLER_TICKET_CAPACITY
#define TAG "ANIMATION_SERVICE"

_Static_assert(ANIMATION_RUNTIME_COMMAND_QUEUE_CAPACITY == 8U, "animation command queue contract changed");
_Static_assert(ANIMATION_RUNTIME_PLAYER_EVENT_QUEUE_CAPACITY == 16U, "animation player event queue contract changed");
_Static_assert(ANIMATION_RUNTIME_TIMING_CAPACITY == 16U, "animation timing table contract changed");

typedef enum {
    RUNTIME_COMMAND_SUBMIT = 0,
    RUNTIME_COMMAND_CANCEL,
    RUNTIME_COMMAND_CANCEL_OWNER,
    RUNTIME_COMMAND_PREFETCH,
    RUNTIME_COMMAND_GET_SNAPSHOT,
    RUNTIME_COMMAND_SET_SINK,
    RUNTIME_COMMAND_BIND_SURFACE,
    RUNTIME_COMMAND_UNBIND_SURFACE,
    RUNTIME_COMMAND_SUSPEND,
    RUNTIME_COMMAND_RESUME,
    RUNTIME_COMMAND_SET_OVERLAY,
    RUNTIME_COMMAND_CLEAR_OVERLAY,
    RUNTIME_COMMAND_STOP_SERVICE,
} animation_runtime_command_type_t;

typedef struct {
    animation_service_result_t result;
    animation_ticket_t ticket;
    animation_snapshot_t snapshot;
    StaticSemaphore_t completion_storage;
    SemaphoreHandle_t completion;
} animation_runtime_response_t;

typedef struct {
    animation_runtime_command_type_t type;
    animation_runtime_response_t *response;
    union {
        animation_request_t request;
        animation_ticket_t ticket;
        struct {
            animation_source_t source;
            uint32_t owner_epoch;
        } owner;
        emoji_anim_type_t prefetch_type;
        struct {
            animation_event_sink_t sink;
            void *context;
        } event_sink;
        void *surface;
        animation_suspend_mode_t suspend_mode;
        animation_overlay_region_t overlay;
    } data;
} animation_runtime_command_t;

typedef struct {
    bool in_use;
    animation_ticket_t ticket;
    int64_t submitted_us;
    int64_t committed_us;
} animation_runtime_ticket_timing_t;

typedef struct {
    animation_service_sync_adapter_t adapter;
    StaticQueue_t command_queue_storage;
    uint8_t command_queue_buffer[ANIMATION_RUNTIME_COMMAND_QUEUE_CAPACITY * sizeof(animation_runtime_command_t)];
    QueueHandle_t command_queue;
    StaticQueue_t player_event_queue_storage;
    uint8_t
        player_event_queue_buffer[ANIMATION_RUNTIME_PLAYER_EVENT_QUEUE_CAPACITY * sizeof(anim_player_ticket_event_t)];
    QueueHandle_t player_event_queue;
    StaticSemaphore_t stopped_storage;
    SemaphoreHandle_t stopped;
    StaticSemaphore_t api_mutex_storage;
    SemaphoreHandle_t api_mutex;
    StaticTask_t controller_task_storage;
    TaskHandle_t controller_task;
    void *surface;
    bool initialized;
    bool stopping;
    animation_service_overflow_buffer_t overflow_buffer;
    animation_runtime_ticket_timing_t ticket_timings[ANIMATION_RUNTIME_TIMING_CAPACITY];
    size_t command_queue_depth_high_watermark;
    size_t player_event_queue_depth_high_watermark;
    size_t controller_stack_min_free_bytes;
} animation_runtime_t;

static animation_runtime_t g_runtime;
EXT_RAM_BSS_ATTR static StackType_t g_animation_controller_task_stack[ANIMATION_RUNTIME_TASK_STACK_BYTES];
_Static_assert(sizeof(StackType_t) == 1U, "ESP-IDF task stack units must remain bytes");
_Static_assert(sizeof(g_animation_controller_task_stack) >= ANIMATION_RUNTIME_TASK_STACK_BYTES,
               "Animation Controller stack backing storage is too small");
static portMUX_TYPE g_runtime_lock = portMUX_INITIALIZER_UNLOCKED;
static bool g_runtime_initializing;

static bool runtime_event_is_terminal(animation_event_type_t type) {
    return type == ANIMATION_EVENT_COMPLETED || type == ANIMATION_EVENT_PREEMPTED ||
           type == ANIMATION_EVENT_CANCELLED || type == ANIMATION_EVENT_FAILED;
}

static const char *runtime_event_phase_name(animation_event_type_t type) {
    switch (type) {
    case ANIMATION_EVENT_ACCEPTED:
        return "accepted";
    case ANIMATION_EVENT_PREPARING:
        return "preparing";
    case ANIMATION_EVENT_COMMITTED:
        return "committed";
    case ANIMATION_EVENT_CYCLE_COMPLETED:
        return "cycle_completed";
    case ANIMATION_EVENT_COMPLETED:
        return "completed";
    case ANIMATION_EVENT_PREEMPTED:
        return "preempted";
    case ANIMATION_EVENT_CANCELLED:
        return "cancelled";
    case ANIMATION_EVENT_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static const char *runtime_failure_name(animation_failure_t failure) {
    switch (failure) {
    case ANIMATION_FAILURE_NONE:
        return "none";
    case ANIMATION_FAILURE_INVALID_RESOURCE:
        return "invalid_resource";
    case ANIMATION_FAILURE_NO_SURFACE:
        return "no_surface";
    case ANIMATION_FAILURE_NO_MEMORY:
        return "no_memory";
    case ANIMATION_FAILURE_SD_OPEN_FAILED:
        return "sd_open_failed";
    case ANIMATION_FAILURE_SD_READ_FAILED:
        return "sd_read_failed";
    case ANIMATION_FAILURE_PACK_CORRUPT:
        return "pack_corrupt";
    case ANIMATION_FAILURE_PREPARE_STALLED:
        return "prepare_stalled";
    case ANIMATION_FAILURE_RENDER_FAILED:
        return "render_failed";
    case ANIMATION_FAILURE_SERVICE_STOPPED:
        return "service_stopped";
    case ANIMATION_FAILURE_QUEUE_FULL:
        return "queue_full";
    case ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL:
        return "player_event_queue_full";
    default:
        return "unknown_failure";
    }
}

static const char *runtime_event_reason(const animation_event_t *event) {
    if (event->failure != ANIMATION_FAILURE_NONE) {
        return runtime_failure_name(event->failure);
    }
    switch (event->type) {
    case ANIMATION_EVENT_COMPLETED:
        return "completed";
    case ANIMATION_EVENT_PREEMPTED:
        return "preempted";
    case ANIMATION_EVENT_CANCELLED:
        return "cancelled";
    default:
        return "none";
    }
}

static animation_runtime_ticket_timing_t *runtime_find_timing(animation_ticket_t ticket) {
    size_t index;

    for (index = 0U; index < ANIMATION_RUNTIME_TIMING_CAPACITY; ++index) {
        if (g_runtime.ticket_timings[index].in_use && g_runtime.ticket_timings[index].ticket == ticket) {
            return &g_runtime.ticket_timings[index];
        }
    }
    return NULL;
}

static animation_runtime_ticket_timing_t *runtime_begin_timing(animation_ticket_t ticket, int64_t now_us) {
    animation_runtime_ticket_timing_t *timing = runtime_find_timing(ticket);
    size_t index;

    if (timing != NULL) {
        return timing;
    }
    for (index = 0U; index < ANIMATION_RUNTIME_TIMING_CAPACITY; ++index) {
        if (!g_runtime.ticket_timings[index].in_use) {
            timing = &g_runtime.ticket_timings[index];
            memset(timing, 0, sizeof(*timing));
            timing->in_use = true;
            timing->ticket = ticket;
            timing->submitted_us = now_us;
            return timing;
        }
    }
    return NULL;
}

static uint32_t runtime_elapsed_ms(const animation_runtime_ticket_timing_t *timing, int64_t now_us) {
    uint64_t elapsed_ms;

    if (timing == NULL || now_us <= timing->submitted_us) {
        return 0U;
    }
    elapsed_ms = (uint64_t)(now_us - timing->submitted_us) / 1000U;
    return elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
}

static void runtime_read_queue_depths(size_t *command_depth, size_t *player_depth) {
    *command_depth = g_runtime.command_queue != NULL ? (size_t)uxQueueMessagesWaiting(g_runtime.command_queue) : 0U;
    *player_depth =
        g_runtime.player_event_queue != NULL ? (size_t)uxQueueMessagesWaiting(g_runtime.player_event_queue) : 0U;
}

static void runtime_note_queue_high_watermark(bool player_queue, size_t depth) {
    portENTER_CRITICAL(&g_runtime_lock);
    if (player_queue) {
        if (depth > g_runtime.player_event_queue_depth_high_watermark) {
            g_runtime.player_event_queue_depth_high_watermark = depth;
        }
    } else if (depth > g_runtime.command_queue_depth_high_watermark) {
        g_runtime.command_queue_depth_high_watermark = depth;
    }
    portEXIT_CRITICAL(&g_runtime_lock);
}

static void runtime_add_runtime_snapshot_metrics(animation_snapshot_t *snapshot) {
    portENTER_CRITICAL(&g_runtime_lock);
    snapshot->player_event_overflow_count = g_runtime.overflow_buffer.recorded_count;
    snapshot->command_queue_depth_high_watermark = g_runtime.command_queue_depth_high_watermark;
    snapshot->player_event_queue_depth_high_watermark = g_runtime.player_event_queue_depth_high_watermark;
    portEXIT_CRITICAL(&g_runtime_lock);
}

static void runtime_observe_event(const animation_event_t *event, void *context) {
    animation_runtime_t *runtime = context;
    animation_runtime_ticket_timing_t *timing;
    animation_snapshot_t snapshot;
    int64_t now_us;
    uint32_t latency_ms;
    int32_t frame_index;
    size_t command_depth;
    size_t player_depth;

    if (event == NULL || runtime == NULL) {
        return;
    }
    now_us = esp_timer_get_time();
    timing = event->type == ANIMATION_EVENT_ACCEPTED ? runtime_begin_timing(event->ticket, now_us)
                                                     : runtime_find_timing(event->ticket);
    if (timing != NULL && event->type == ANIMATION_EVENT_COMMITTED) {
        timing->committed_us = now_us;
    }
    latency_ms = runtime_elapsed_ms(timing, now_us);
    frame_index = event->type == ANIMATION_EVENT_COMMITTED ? 0 : -1;
    animation_controller_get_snapshot(&runtime->adapter.controller, &snapshot);
    runtime_read_queue_depths(&command_depth, &player_depth);

    ESP_LOGI(TAG,
             "evt=animation ticket=%lu source=%d owner_epoch=%lu priority=%d desired=%s desired_ticket=%lu "
             "preparing=%s preparing_ticket=%lu visible=%s visible_ticket=%lu phase=%s reason=%s "
             "latency_ms=%lu frame_index=%ld completed_cycles=%u queue_depth=%u "
             "command_queue_depth=%u player_queue_depth=%u heap=%u psram=%u",
             (unsigned long)event->ticket, (int)event->request.source, (unsigned long)event->request.owner_epoch,
             (int)event->request.priority, animation_registry_name(snapshot.desired_type),
             (unsigned long)snapshot.desired_ticket, animation_registry_name(snapshot.preparing_type),
             (unsigned long)snapshot.preparing_ticket, animation_registry_name(snapshot.visible_type),
             (unsigned long)snapshot.visible_ticket, runtime_event_phase_name(event->type), runtime_event_reason(event),
             (unsigned long)latency_ms, (long)frame_index, (unsigned)event->completed_cycles,
             (unsigned)snapshot.queued_count, (unsigned)command_depth, (unsigned)player_depth,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    if (timing != NULL && runtime_event_is_terminal(event->type)) {
        memset(timing, 0, sizeof(*timing));
    }
}

static void runtime_log_rejected_player_event(const anim_player_ticket_event_t *event,
                                              animation_service_result_t result) {
    animation_runtime_ticket_timing_t *timing = runtime_find_timing(event->ticket);
    animation_snapshot_t snapshot;
    size_t command_depth;
    size_t player_depth;
    uint32_t latency_ms = runtime_elapsed_ms(timing, esp_timer_get_time());
    int32_t frame_index = event->type == ANIMATION_EVENT_COMMITTED ? 0 : -1;
    const char *reason = result == ANIMATION_SERVICE_NOT_FOUND ? "orphan_or_stale" : "wrong_transition";

    animation_controller_get_snapshot(&g_runtime.adapter.controller, &snapshot);
    runtime_read_queue_depths(&command_depth, &player_depth);
    ESP_LOGW(TAG,
             "evt=animation_player_drop ticket=%lu source=%d owner_epoch=%lu priority=%d desired=%s "
             "desired_ticket=%lu preparing=%s preparing_ticket=%lu visible=%s visible_ticket=%lu "
             "phase=%s reason=%s latency_ms=%lu frame_index=%ld completed_cycles=%u "
             "queue_depth=%u command_queue_depth=%u player_queue_depth=%u heap=%u psram=%u",
             (unsigned long)event->ticket, (int)ANIM_SOURCE_UNKNOWN, 0UL, (int)ANIM_PRIORITY_AMBIENT,
             animation_registry_name(snapshot.desired_type), (unsigned long)snapshot.desired_ticket,
             animation_registry_name(snapshot.preparing_type), (unsigned long)snapshot.preparing_ticket,
             animation_registry_name(snapshot.visible_type), (unsigned long)snapshot.visible_ticket,
             runtime_event_phase_name(event->type), reason, (unsigned long)latency_ms, (long)frame_index,
             (unsigned)event->completed_cycles, (unsigned)snapshot.queued_count, (unsigned)command_depth,
             (unsigned)player_depth, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static animation_failure_t runtime_player_play(void *context, animation_ticket_t ticket,
                                               const animation_request_t *request) {
    int result;
    (void)context;

    result = emoji_anim_start_request_with_ticket(request, ticket);
    if (result == 0) {
        return ANIMATION_FAILURE_NONE;
    }
    if (result == EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL) {
        return ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL;
    }
    return ANIMATION_FAILURE_INVALID_RESOURCE;
}

static void runtime_player_stop(void *context) {
    (void)context;
    emoji_anim_stop();
}

static bool runtime_is_initialized(void) {
    bool initialized;
    portENTER_CRITICAL(&g_runtime_lock);
    initialized = g_runtime.initialized && !g_runtime.stopping;
    portEXIT_CRITICAL(&g_runtime_lock);
    return initialized;
}

static void runtime_player_event_sink(const anim_player_ticket_event_t *event, void *context) {
    animation_runtime_t *runtime = context;
    BaseType_t queued;

    if (event == NULL || runtime == NULL || runtime->player_event_queue == NULL) {
        return;
    }
    queued = xQueueSend(runtime->player_event_queue, event, 0);
    if (queued != pdTRUE) {
        portENTER_CRITICAL(&g_runtime_lock);
        (void)animation_service_overflow_buffer_record(&runtime->overflow_buffer, event->ticket);
        portEXIT_CRITICAL(&g_runtime_lock);
    } else {
        runtime_note_queue_high_watermark(true, (size_t)uxQueueMessagesWaiting(runtime->player_event_queue));
    }
    if (runtime->controller_task != NULL) {
        xTaskNotifyGive(runtime->controller_task);
    }
}

static void runtime_handle_player_event(const anim_player_ticket_event_t *event) {
    animation_player_event_type_t player_event;
    animation_service_result_t result;

    if (event == NULL || event->ticket == ANIMATION_TICKET_INVALID) {
        return;
    }
    switch (event->type) {
    case ANIMATION_EVENT_COMMITTED:
        player_event = ANIMATION_PLAYER_EVENT_COMMITTED;
        break;
    case ANIMATION_EVENT_CYCLE_COMPLETED:
        player_event = ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED;
        break;
    case ANIMATION_EVENT_COMPLETED:
        player_event = ANIMATION_PLAYER_EVENT_COMPLETED;
        break;
    case ANIMATION_EVENT_FAILED:
        player_event = ANIMATION_PLAYER_EVENT_FAILED;
        break;
    case ANIMATION_EVENT_PREPARING:
    default:
        return;
    }
    result = animation_service_sync_player_event(&g_runtime.adapter, event->ticket, player_event, event->failure);
    if (result == ANIMATION_SERVICE_NOT_FOUND || result == ANIMATION_SERVICE_INVALID_TRANSITION) {
        runtime_log_rejected_player_event(event, result);
    }
}

static bool runtime_take_overflow(anim_player_ticket_event_t *event) {
    animation_ticket_t ticket = ANIMATION_TICKET_INVALID;
    bool pending;
    portENTER_CRITICAL(&g_runtime_lock);
    pending = animation_service_overflow_buffer_take(&g_runtime.overflow_buffer, &ticket);
    portEXIT_CRITICAL(&g_runtime_lock);
    if (pending) {
        memset(event, 0, sizeof(*event));
        event->ticket = ticket;
        event->type = ANIMATION_EVENT_FAILED;
        event->failure = ANIMATION_FAILURE_PLAYER_EVENT_QUEUE_FULL;
    }
    return pending;
}

static bool runtime_has_overflow(void) {
    bool pending;
    portENTER_CRITICAL(&g_runtime_lock);
    pending = animation_service_overflow_buffer_has_pending(&g_runtime.overflow_buffer);
    portEXIT_CRITICAL(&g_runtime_lock);
    return pending;
}

static void runtime_observe_controller_stack(const char *phase) {
    const size_t min_free_bytes = (size_t)uxTaskGetStackHighWaterMark(NULL);
    const bool warning = min_free_bytes < ANIMATION_RUNTIME_STACK_WARN_FREE_BYTES;

    if (min_free_bytes >= g_runtime.controller_stack_min_free_bytes) {
        return;
    }
    g_runtime.controller_stack_min_free_bytes = min_free_bytes;
    if (warning) {
        ESP_LOGW(TAG, "controller_stack configured_bytes=%u min_free_bytes=%u status=%s phase=%s",
                 (unsigned)ANIMATION_RUNTIME_TASK_STACK_BYTES, (unsigned)min_free_bytes, "warn",
                 phase != NULL ? phase : "unknown");
    } else {
        ESP_LOGI(TAG, "controller_stack configured_bytes=%u min_free_bytes=%u status=%s phase=%s",
                 (unsigned)ANIMATION_RUNTIME_TASK_STACK_BYTES, (unsigned)min_free_bytes, "ok",
                 phase != NULL ? phase : "unknown");
    }
}

static void runtime_complete(animation_runtime_response_t *response, animation_service_result_t result) {
    response->result = result;
    xSemaphoreGive(response->completion);
}

static bool runtime_handle_command(const animation_runtime_command_t *command) {
    animation_runtime_response_t *response = command->response;
    animation_service_result_t result = ANIMATION_SERVICE_INVALID_ARGUMENT;

    switch (command->type) {
    case RUNTIME_COMMAND_SUBMIT: {
        animation_controller_output_t output;
        result = animation_service_sync_prepare_submit(&g_runtime.adapter, &command->data.request, &response->ticket,
                                                       &output);
        runtime_complete(response, result);
        if (result == ANIMATION_SERVICE_OK) {
            animation_service_sync_dispatch_output(&g_runtime.adapter, &output);
        }
        return false;
    }
    case RUNTIME_COMMAND_CANCEL:
        result = animation_service_sync_cancel(&g_runtime.adapter, command->data.ticket);
        break;
    case RUNTIME_COMMAND_CANCEL_OWNER:
        result = animation_service_sync_cancel_owner(&g_runtime.adapter, command->data.owner.source,
                                                     command->data.owner.owner_epoch);
        break;
    case RUNTIME_COMMAND_PREFETCH:
        if (!g_runtime.adapter.surface_bound) {
            result = ANIMATION_SERVICE_NO_SURFACE;
        } else {
            result = emoji_anim_prefetch_type(command->data.prefetch_type) == 0 ? ANIMATION_SERVICE_OK
                                                                                : ANIMATION_SERVICE_PLAYER_ERROR;
        }
        break;
    case RUNTIME_COMMAND_GET_SNAPSHOT:
        result = animation_service_sync_get_snapshot(&g_runtime.adapter, &response->snapshot);
        if (result == ANIMATION_SERVICE_OK) {
            runtime_add_runtime_snapshot_metrics(&response->snapshot);
        }
        break;
    case RUNTIME_COMMAND_SET_SINK:
        result = animation_service_sync_set_sink(&g_runtime.adapter, command->data.event_sink.sink,
                                                 command->data.event_sink.context);
        break;
    case RUNTIME_COMMAND_BIND_SURFACE:
        if (command->data.surface == NULL) {
            result = ANIMATION_SERVICE_INVALID_ARGUMENT;
        } else if (g_runtime.surface == command->data.surface && g_runtime.adapter.surface_bound) {
            result = ANIMATION_SERVICE_OK;
        } else if (g_runtime.adapter.surface_bound) {
            result = ANIMATION_SERVICE_INVALID_TRANSITION;
        } else if (emoji_anim_init(command->data.surface) != 0) {
            result = ANIMATION_SERVICE_PLAYER_ERROR;
        } else {
            g_runtime.surface = command->data.surface;
            result = animation_service_sync_set_surface(&g_runtime.adapter, true);
        }
        break;
    case RUNTIME_COMMAND_UNBIND_SURFACE:
        if (g_runtime.surface != NULL) {
            if (emoji_anim_deinit() != 0) {
                result = ANIMATION_SERVICE_PLAYER_ERROR;
            } else {
                g_runtime.surface = NULL;
                result = animation_service_sync_set_surface(&g_runtime.adapter, false);
            }
        } else {
            result = animation_service_sync_set_surface(&g_runtime.adapter, false);
        }
        break;
    case RUNTIME_COMMAND_SUSPEND:
        result = command->data.suspend_mode == ANIMATION_SUSPEND_HOLD_LAST_FRAME
                     ? animation_service_sync_set_suspended(&g_runtime.adapter, true)
                     : ANIMATION_SERVICE_INVALID_ARGUMENT;
        break;
    case RUNTIME_COMMAND_RESUME:
        result = animation_service_sync_set_suspended(&g_runtime.adapter, false);
        break;
    case RUNTIME_COMMAND_SET_OVERLAY:
        if (command->data.overlay.x2 <= command->data.overlay.x1 ||
            command->data.overlay.y2 <= command->data.overlay.y1) {
            result = ANIMATION_SERVICE_INVALID_ARGUMENT;
        } else {
            emoji_anim_set_direct_lcd_protected_region(command->data.overlay.x1, command->data.overlay.y1,
                                                       command->data.overlay.x2, command->data.overlay.y2);
            result = ANIMATION_SERVICE_OK;
        }
        break;
    case RUNTIME_COMMAND_CLEAR_OVERLAY:
        emoji_anim_clear_direct_lcd_protected_region();
        result = ANIMATION_SERVICE_OK;
        break;
    case RUNTIME_COMMAND_STOP_SERVICE:
        result = animation_service_sync_stop(&g_runtime.adapter);
        (void)emoji_anim_player_set_event_sink(NULL, NULL);
        if (g_runtime.surface != NULL) {
            if (emoji_anim_deinit() != 0) {
                runtime_complete(response, ANIMATION_SERVICE_PLAYER_ERROR);
                return false;
            }
            g_runtime.surface = NULL;
        }
        runtime_complete(response, result);
        return true;
    default:
        break;
    }
    runtime_complete(response, result);
    return false;
}

static void animation_controller_task(void *context) {
    animation_runtime_command_t command;
    anim_player_ticket_event_t player_event;
    bool should_stop = false;
    (void)context;
    runtime_observe_controller_stack("started");

    while (!should_stop) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ANIMATION_RUNTIME_WATCHDOG_TICK_MS));
        emoji_anim_player_poll_prepare_watchdog();
        do {
            while (xQueueReceive(g_runtime.player_event_queue, &player_event, 0) == pdTRUE) {
                runtime_handle_player_event(&player_event);
                runtime_observe_controller_stack("player_event");
            }
            if (runtime_take_overflow(&player_event)) {
                runtime_handle_player_event(&player_event);
                runtime_observe_controller_stack("overflow_event");
                continue;
            }
            if (xQueueReceive(g_runtime.command_queue, &command, 0) == pdTRUE) {
                should_stop = runtime_handle_command(&command);
                runtime_observe_controller_stack("command");
            }
        } while (!should_stop && (uxQueueMessagesWaiting(g_runtime.player_event_queue) > 0U ||
                                  uxQueueMessagesWaiting(g_runtime.command_queue) > 0U || runtime_has_overflow()));
    }

    portENTER_CRITICAL(&g_runtime_lock);
    g_runtime.initialized = false;
    g_runtime.stopping = false;
    g_runtime.controller_task = NULL;
    portEXIT_CRITICAL(&g_runtime_lock);
    xSemaphoreGive(g_runtime.stopped);
    vTaskDelete(NULL);
}

static animation_service_result_t runtime_send_command(animation_runtime_command_t *command,
                                                       animation_runtime_response_t *response) {
    TaskHandle_t controller_task;

    if (command == NULL || response == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    if (!runtime_is_initialized()) {
        return ANIMATION_SERVICE_NOT_INITIALIZED;
    }
    if (xTaskGetCurrentTaskHandle() == g_runtime.controller_task) {
        return ANIMATION_SERVICE_REENTRANT;
    }
    if (g_runtime.api_mutex == NULL || xSemaphoreTake(g_runtime.api_mutex, portMAX_DELAY) != pdTRUE) {
        return ANIMATION_SERVICE_PLAYER_ERROR;
    }
    if (!runtime_is_initialized()) {
        xSemaphoreGive(g_runtime.api_mutex);
        return ANIMATION_SERVICE_NOT_INITIALIZED;
    }
    memset(response, 0, sizeof(*response));
    response->ticket = ANIMATION_TICKET_INVALID;
    response->completion = xSemaphoreCreateBinaryStatic(&response->completion_storage);
    command->response = response;
    if (xQueueSend(g_runtime.command_queue, command, 0) != pdTRUE) {
        xSemaphoreGive(g_runtime.api_mutex);
        return ANIMATION_SERVICE_QUEUE_FULL;
    }
    runtime_note_queue_high_watermark(false, (size_t)uxQueueMessagesWaiting(g_runtime.command_queue));
    controller_task = g_runtime.controller_task;
    xSemaphoreGive(g_runtime.api_mutex);
    xTaskNotifyGive(controller_task);
    (void)xSemaphoreTake(response->completion, portMAX_DELAY);
    return response->result;
}

animation_service_result_t animation_service_init(void) {
    animation_service_player_ops_t player_ops = {
        .context = NULL,
        .play = runtime_player_play,
        .stop = runtime_player_stop,
    };

    portENTER_CRITICAL(&g_runtime_lock);
    if (g_runtime.initialized || g_runtime.stopping || g_runtime_initializing) {
        portEXIT_CRITICAL(&g_runtime_lock);
        return ANIMATION_SERVICE_INVALID_TRANSITION;
    }
    g_runtime_initializing = true;
    portEXIT_CRITICAL(&g_runtime_lock);

    memset(&g_runtime, 0, sizeof(g_runtime));
    g_runtime.controller_stack_min_free_bytes = ANIMATION_RUNTIME_TASK_STACK_BYTES;
    g_runtime.command_queue =
        xQueueCreateStatic(ANIMATION_RUNTIME_COMMAND_QUEUE_CAPACITY, sizeof(animation_runtime_command_t),
                           g_runtime.command_queue_buffer, &g_runtime.command_queue_storage);
    g_runtime.player_event_queue =
        xQueueCreateStatic(ANIMATION_RUNTIME_PLAYER_EVENT_QUEUE_CAPACITY, sizeof(anim_player_ticket_event_t),
                           g_runtime.player_event_queue_buffer, &g_runtime.player_event_queue_storage);
    g_runtime.stopped = xSemaphoreCreateBinaryStatic(&g_runtime.stopped_storage);
    g_runtime.api_mutex = xSemaphoreCreateMutexStatic(&g_runtime.api_mutex_storage);
    animation_service_sync_adapter_init(&g_runtime.adapter, &player_ops);
    animation_service_sync_set_event_observer(&g_runtime.adapter, runtime_observe_event, &g_runtime);
    if (g_runtime.command_queue == NULL || g_runtime.player_event_queue == NULL || g_runtime.stopped == NULL ||
        g_runtime.api_mutex == NULL) {
        portENTER_CRITICAL(&g_runtime_lock);
        g_runtime_initializing = false;
        portEXIT_CRITICAL(&g_runtime_lock);
        return ANIMATION_SERVICE_PLAYER_ERROR;
    }
    g_runtime.controller_task = xTaskCreateStatic(
        animation_controller_task, "animation_ctl", ANIMATION_RUNTIME_TASK_STACK_BYTES, NULL,
        ANIMATION_RUNTIME_TASK_PRIORITY, g_animation_controller_task_stack, &g_runtime.controller_task_storage);
    if (g_runtime.controller_task == NULL) {
        portENTER_CRITICAL(&g_runtime_lock);
        g_runtime.initialized = false;
        g_runtime_initializing = false;
        portEXIT_CRITICAL(&g_runtime_lock);
        return ANIMATION_SERVICE_PLAYER_ERROR;
    }
    if (emoji_anim_player_set_event_sink(runtime_player_event_sink, &g_runtime) != 0) {
        portENTER_CRITICAL(&g_runtime_lock);
        g_runtime.initialized = true;
        g_runtime_initializing = false;
        portEXIT_CRITICAL(&g_runtime_lock);
        (void)animation_service_deinit();
        return ANIMATION_SERVICE_PLAYER_ERROR;
    }
    portENTER_CRITICAL(&g_runtime_lock);
    g_runtime.initialized = true;
    g_runtime_initializing = false;
    portEXIT_CRITICAL(&g_runtime_lock);
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_service_deinit(void) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_STOP_SERVICE};
    animation_runtime_response_t response;
    animation_service_result_t result;

    if (!runtime_is_initialized()) {
        return ANIMATION_SERVICE_NOT_INITIALIZED;
    }
    if (xTaskGetCurrentTaskHandle() == g_runtime.controller_task) {
        return ANIMATION_SERVICE_REENTRANT;
    }
    if (g_runtime.api_mutex == NULL || xSemaphoreTake(g_runtime.api_mutex, portMAX_DELAY) != pdTRUE) {
        return ANIMATION_SERVICE_PLAYER_ERROR;
    }
    if (!runtime_is_initialized()) {
        xSemaphoreGive(g_runtime.api_mutex);
        return ANIMATION_SERVICE_NOT_INITIALIZED;
    }
    portENTER_CRITICAL(&g_runtime_lock);
    g_runtime.stopping = true;
    portEXIT_CRITICAL(&g_runtime_lock);
    /* STOP must remain enqueueable even after the public stopping gate closes. */
    memset(&response, 0, sizeof(response));
    response.completion = xSemaphoreCreateBinaryStatic(&response.completion_storage);
    command.response = &response;
    if (xQueueSend(g_runtime.command_queue, &command, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(g_runtime.api_mutex);
        return ANIMATION_SERVICE_QUEUE_FULL;
    }
    runtime_note_queue_high_watermark(false, (size_t)uxQueueMessagesWaiting(g_runtime.command_queue));
    TaskHandle_t controller_task = g_runtime.controller_task;
    xSemaphoreGive(g_runtime.api_mutex);
    xTaskNotifyGive(controller_task);
    (void)xSemaphoreTake(response.completion, portMAX_DELAY);
    result = response.result;
    (void)xSemaphoreTake(g_runtime.stopped, portMAX_DELAY);
    return result;
}

animation_service_result_t animation_submit(const animation_request_t *request, animation_ticket_t *out_ticket) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_SUBMIT};
    animation_runtime_response_t response = {.ticket = ANIMATION_TICKET_INVALID};
    animation_service_result_t result;

    if (request == NULL || out_ticket == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    *out_ticket = ANIMATION_TICKET_INVALID;
    command.data.request = *request;
    result = runtime_send_command(&command, &response);
    if (result == ANIMATION_SERVICE_OK) {
        *out_ticket = response.ticket;
    }
    return result;
}

animation_service_result_t animation_cancel(animation_ticket_t ticket) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_CANCEL};
    animation_runtime_response_t response;
    command.data.ticket = ticket;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_cancel_owner(animation_source_t source, uint32_t owner_epoch) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_CANCEL_OWNER};
    animation_runtime_response_t response;
    command.data.owner.source = source;
    command.data.owner.owner_epoch = owner_epoch;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_prefetch_hint(emoji_anim_type_t type) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_PREFETCH};
    animation_runtime_response_t response;
    command.data.prefetch_type = type;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_get_snapshot(animation_snapshot_t *out_snapshot) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_GET_SNAPSHOT};
    animation_runtime_response_t response;
    animation_service_result_t result;
    if (out_snapshot == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    result = runtime_send_command(&command, &response);
    if (result == ANIMATION_SERVICE_OK) {
        *out_snapshot = response.snapshot;
    }
    return result;
}

animation_service_result_t animation_set_event_sink(animation_event_sink_t sink, void *context) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_SET_SINK};
    animation_runtime_response_t response;
    command.data.event_sink.sink = sink;
    command.data.event_sink.context = context;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_service_bind_surface(void *surface) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_BIND_SURFACE};
    animation_runtime_response_t response;
    command.data.surface = surface;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_service_unbind_surface(void) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_UNBIND_SURFACE};
    animation_runtime_response_t response;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_service_suspend(animation_suspend_mode_t mode) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_SUSPEND};
    animation_runtime_response_t response;
    command.data.suspend_mode = mode;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_service_resume(void) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_RESUME};
    animation_runtime_response_t response;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_set_overlay_region(const animation_overlay_region_t *region) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_SET_OVERLAY};
    animation_runtime_response_t response;
    if (region == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    command.data.overlay = *region;
    return runtime_send_command(&command, &response);
}

animation_service_result_t animation_clear_overlay_region(void) {
    animation_runtime_command_t command = {.type = RUNTIME_COMMAND_CLEAR_OVERLAY};
    animation_runtime_response_t response;
    return runtime_send_command(&command, &response);
}
