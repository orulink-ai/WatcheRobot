#include "behavior_state_service.h"

#include "anim_storage.h"
#include "animation_service.h"
#include "behavior_action_parser.h"
#include "behavior_animation_reducer.h"
#include "behavior_catalog_loader.h"
#include "behavior_catalog_parser.h"
#include "behavior_executor.h"
#include "behavior_memory.h"
#include "behavior_scheduler.h"
#include "behavior_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"
#include "sfx_service.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TAG "BEHAVIOR_STATE"

#define BEHAVIOR_STATES_PATH "/spiffs/behavior/states.json"
#define BEHAVIOR_STATES_SD_PATH "/sdcard/behavior/states.json"
#define BEHAVIOR_ACTIONS_DIR "/spiffs/actions"
#define BEHAVIOR_TASK_STACK 6144
#define BEHAVIOR_TASK_PRIORITY 5
#define BEHAVIOR_TICK_MS 10
#define BEHAVIOR_MAX_FILE_BYTES 16384
#define BEHAVIOR_ACTION_MAX_FILE_BYTES 32768
#define BEHAVIOR_ACTION_PATH_LEN 160
#define BEHAVIOR_DEFAULT_ONESHOT_HOLD_MS 1200U
#define BEHAVIOR_REQUEST_QUEUE_DEPTH 1
#define BEHAVIOR_ANIMATION_EVENT_QUEUE_DEPTH 16
#define BEHAVIOR_ANIMATION_OBSERVATION_QUEUE_DEPTH 4

typedef struct {
    SemaphoreHandle_t lock;
    QueueHandle_t request_queue;
    QueueHandle_t animation_event_queue;
    StaticQueue_t animation_event_queue_storage;
    uint8_t animation_event_queue_buffer[BEHAVIOR_ANIMATION_EVENT_QUEUE_DEPTH * sizeof(animation_event_t)];
    QueueHandle_t animation_observation_queue;
    StaticQueue_t animation_observation_queue_storage;
    uint8_t animation_observation_queue_buffer[BEHAVIOR_ANIMATION_OBSERVATION_QUEUE_DEPTH *
                                               sizeof(behavior_animation_event_t)];
    TaskHandle_t task;
    bool initialized;
    behavior_catalog_t catalog;
    behavior_action_catalog_t action_catalog;
    behavior_state_def_t *current_state;
    behavior_action_def_t *current_action;
    char current_state_id[BEHAVIOR_STATE_ID_LEN];
    char current_action_id[BEHAVIOR_STATE_ID_LEN];
    uint32_t state_started_ms;
    uint32_t action_started_ms;
    int next_motion_index;
    int next_action_motion_index;
    int next_expression_index;
    int next_sound_index;
    int next_light_index;
    char text_override[BEHAVIOR_TEXT_LEN];
    int text_override_font_size;
    bool text_override_valid;
    bool text_override_alert;
    char anim_override[BEHAVIOR_STATE_ID_LEN];
    bool anim_override_valid;
    bool anim_override_suppressed;
    char sound_override[BEHAVIOR_SOUND_ID_LEN];
    bool sound_override_valid;
    bool sound_override_suppressed;
    bool suppress_state_sound_events;
    bool wait_for_local_sfx_completion;
    bool resources_one_shot;
    bool hold_logged;
    animation_ticket_t animation_ticket;
    uint32_t animation_owner_epoch;
    uint32_t animation_correlation_id;
    emoji_anim_type_t animation_type;
    uint32_t animation_event_overflow_count;
} behavior_context_t;

typedef behavior_display_command_t behavior_display_request_t;

typedef struct {
    bool force_animation_refresh;
    char state_id[BEHAVIOR_STATE_ID_LEN];
    char text[BEHAVIOR_TEXT_LEN];
    int font_size;
    bool has_text;
    bool alert_text;
    char anim_id[BEHAVIOR_STATE_ID_LEN];
    bool has_anim_override;
    bool suppress_anim;
    char sound_id[BEHAVIOR_SOUND_ID_LEN];
    bool has_sound_override;
    bool suppress_sound;
    char action_id[BEHAVIOR_STATE_ID_LEN];
    bool resources_one_shot;
} behavior_state_request_t;

static behavior_context_t s_ctx = {0};
static volatile bool s_state_busy_snapshot = false;
static volatile bool s_action_active_snapshot = false;
static volatile uint32_t s_action_active_until_ms = 0;

static bool behavior_animation_event_is_terminal(animation_event_type_t type) {
    return type == ANIMATION_EVENT_COMPLETED || type == ANIMATION_EVENT_PREEMPTED ||
           type == ANIMATION_EVENT_CANCELLED || type == ANIMATION_EVENT_FAILED;
}

static bool behavior_animation_event_is_observable(animation_event_type_t type) {
    return type == ANIMATION_EVENT_COMMITTED || behavior_animation_event_is_terminal(type);
}

static void behavior_animation_event_sink(const animation_event_t *event, void *context) {
    BaseType_t queued;

    (void)context;
    if (event == NULL || event->request.source != ANIM_SOURCE_BEHAVIOR ||
        !behavior_animation_event_is_observable(event->type) || s_ctx.animation_event_queue == NULL) {
        return;
    }
    queued = xQueueSend(s_ctx.animation_event_queue, event, 0);
    if (queued != pdTRUE) {
        s_ctx.animation_event_overflow_count++;
        ESP_LOGE(TAG, "Animation terminal event queue full ticket=%lu type=%d overflows=%lu",
                 (unsigned long)event->ticket, (int)event->type, (unsigned long)s_ctx.animation_event_overflow_count);
    }
    if (s_ctx.task != NULL) {
        xTaskNotifyGive(s_ctx.task);
    }
}

static void behavior_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool behavior_lock(void) {
    if (s_ctx.lock == NULL) {
        return false;
    }

    return xSemaphoreTake(s_ctx.lock, portMAX_DELAY) == pdTRUE;
}

static bool behavior_should_suppress_sound(const char *sound_id) {
    return sound_id != NULL && sound_id[0] == '\0';
}

static bool behavior_should_suppress_anim(const char *anim_id) {
    return anim_id != NULL && anim_id[0] == '\0';
}

static void behavior_get_display_defaults_locked(const char **text, const char **anim, int *font_size);
static uint32_t behavior_now_ms(void);

static void behavior_unlock(void) {
    if (s_ctx.lock != NULL) {
        xSemaphoreGive(s_ctx.lock);
    }
}

static void behavior_clear_display_request(behavior_display_request_t *request) {
    behavior_executor_clear_display_command(request);
}

static void behavior_fill_state_request(behavior_state_request_t *request, const char *state_id, const char *text,
                                        int font_size, bool alert_text, const char *anim_id, const char *sound_id,
                                        const char *action_id, bool resources_one_shot) {
    if (request == NULL) {
        return;
    }

    memset(request, 0, sizeof(*request));
    behavior_copy_string(request->state_id, sizeof(request->state_id), state_id);
    behavior_copy_string(request->text, sizeof(request->text), text);
    request->font_size = font_size;
    request->has_text = (text != NULL);
    request->alert_text = alert_text;
    request->has_anim_override = (anim_id != NULL);
    request->suppress_anim = behavior_should_suppress_anim(anim_id);
    if (anim_id != NULL && anim_id[0] != '\0') {
        behavior_copy_string(request->anim_id, sizeof(request->anim_id), anim_id);
    }
    request->has_sound_override = (sound_id != NULL);
    request->suppress_sound = behavior_should_suppress_sound(sound_id);
    if (sound_id != NULL && sound_id[0] != '\0') {
        behavior_copy_string(request->sound_id, sizeof(request->sound_id), sound_id);
    }
    behavior_copy_string(request->action_id, sizeof(request->action_id), action_id);
    request->resources_one_shot = resources_one_shot;
}

static esp_err_t behavior_submit_state_request(const behavior_state_request_t *request) {
    BaseType_t queue_result;

    if (request == NULL || (!request->force_animation_refresh && request->state_id[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx.request_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    queue_result = xQueueOverwrite(s_ctx.request_queue, request);
    if (queue_result != pdPASS) {
        return ESP_FAIL;
    }

    if (s_ctx.task != NULL) {
        xTaskNotifyGive(s_ctx.task);
    }
    return ESP_OK;
}

static bool behavior_animation_is_current_locked(const char *anim_id) {
    emoji_anim_type_t type = EMOJI_ANIM_NONE;

    return anim_id != NULL && s_ctx.animation_ticket != ANIMATION_TICKET_INVALID &&
           animation_registry_type_from_name(anim_id, &type) && type == s_ctx.animation_type;
}

static void behavior_capture_display_request_locked_ex(behavior_display_request_t *request,
                                                       bool force_animation_refresh) {
    const char *text = NULL;
    const char *anim = NULL;
    int font_size = 0;

    if (request == NULL) {
        return;
    }

    behavior_clear_display_request(request);
    behavior_get_display_defaults_locked(&text, &anim, &font_size);
    if (s_ctx.text_override_valid) {
        text = s_ctx.text_override;
        font_size = s_ctx.text_override_font_size;
    }
    if (s_ctx.anim_override_suppressed) {
        anim = NULL;
    } else if (s_ctx.anim_override_valid) {
        anim = s_ctx.anim_override;
    }
    if (!force_animation_refresh && behavior_animation_is_current_locked(anim)) {
        anim = NULL;
    }

    request->pending = true;
    request->has_text = (text != NULL);
    request->has_anim = (anim != NULL);
    request->font_size = font_size;
    request->alert_text = s_ctx.text_override_valid && s_ctx.text_override_alert;
    behavior_copy_string(request->text, sizeof(request->text), text);
    behavior_copy_string(request->anim, sizeof(request->anim), anim);
    behavior_copy_string(request->state_id, sizeof(request->state_id), s_ctx.current_state_id);
    request->playback_mode = behavior_scheduler_effective_animation_playback(
        s_ctx.current_state != NULL && s_ctx.current_state->expression_count > 0
            ? s_ctx.current_state->expression[0].playback_mode
            : ANIM_PLAYBACK_RESOURCE_DEFAULT,
        s_ctx.resources_one_shot);
    request->repeat_count =
        !s_ctx.resources_one_shot && s_ctx.current_state != NULL && s_ctx.current_state->expression_count > 0
            ? s_ctx.current_state->expression[0].repeat_count
            : 0U;
    request->fade_in_ms = s_ctx.current_state != NULL && s_ctx.current_state->expression_count > 0
                              ? s_ctx.current_state->expression[0].fade_in_ms
                              : 0U;
    request->owner_epoch = s_ctx.animation_owner_epoch;
    request->correlation_id = s_ctx.animation_correlation_id;
}

static void behavior_capture_display_request_locked(behavior_display_request_t *request) {
    behavior_capture_display_request_locked_ex(request, false);
}

static void behavior_apply_display_request(const behavior_display_request_t *request, const char *reason,
                                           behavior_display_execution_result_t *result) {
    behavior_executor_apply_display(request, reason, result);
}

static uint32_t behavior_now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool behavior_action_active_snapshot_is_active(uint32_t now_ms) {
    if (!s_action_active_snapshot) {
        return false;
    }

    return (int32_t)(s_action_active_until_ms - now_ms) > 0;
}

static void behavior_update_action_active_snapshot_locked(uint32_t now_ms) {
    bool active = false;
    uint32_t active_until_ms = now_ms;

    if (s_ctx.current_action != NULL && s_ctx.current_action->total_duration_ms > 0) {
        uint32_t elapsed_ms = now_ms - s_ctx.action_started_ms;
        active = elapsed_ms < s_ctx.current_action->total_duration_ms;
        active_until_ms = s_ctx.action_started_ms + s_ctx.current_action->total_duration_ms;
    }

    s_action_active_until_ms = active_until_ms;
    s_action_active_snapshot = active;
}

static void behavior_update_state_busy_snapshot_locked(void) {
    s_state_busy_snapshot =
        s_ctx.current_state_id[0] != '\0' && strcmp(s_ctx.current_state_id, s_ctx.catalog.default_state) != 0;
}

static bool behavior_is_json_filename(const char *name) {
    size_t len;

    if (name == NULL) {
        return false;
    }

    len = strlen(name);
    return len > 5 && strcasecmp(&name[len - 5], ".json") == 0;
}

static void behavior_normalize_file_stem(const char *name, char *dst, size_t dst_size) {
    const char *base = name;
    const char *ext = NULL;
    size_t i;
    size_t len;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (name == NULL || name[0] == '\0') {
        return;
    }

    for (i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/' || name[i] == '\\') {
            base = &name[i + 1];
        }
    }

    ext = strrchr(base, '.');
    len = (ext != NULL && ext > base) ? (size_t)(ext - base) : strlen(base);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    for (i = 0; i < len; ++i) {
        dst[i] = (char)tolower((unsigned char)base[i]);
    }
    dst[len] = '\0';
}

static behavior_state_def_t *behavior_find_state_locked(const char *state_id) {
    int i;

    if (state_id == NULL || state_id[0] == '\0') {
        return NULL;
    }

    for (i = 0; i < s_ctx.catalog.state_count; ++i) {
        if (strcmp(s_ctx.catalog.states[i].id, state_id) == 0) {
            return &s_ctx.catalog.states[i];
        }
    }

    return NULL;
}

static behavior_action_def_t *behavior_find_action_locked(const char *action_id) {
    int i;

    if (action_id == NULL || action_id[0] == '\0') {
        return NULL;
    }

    for (i = 0; i < s_ctx.action_catalog.action_count; ++i) {
        if (strcmp(s_ctx.action_catalog.actions[i].id, action_id) == 0) {
            return &s_ctx.action_catalog.actions[i];
        }
    }

    return NULL;
}

static bool behavior_state_is_missing_from_catalog(const char *state_id) {
    bool missing = false;

    if (state_id == NULL || state_id[0] == '\0') {
        return false;
    }
    if (!behavior_lock()) {
        return false;
    }

    if (strcmp(state_id, s_ctx.catalog.default_state) != 0 && strcmp(state_id, "standby") != 0) {
        missing = behavior_find_state_locked(state_id) == NULL;
    }

    behavior_unlock();
    return missing;
}

static char *behavior_read_text_file(const char *path, size_t max_bytes) {
    FILE *file = NULL;
    char *buffer = NULL;
    long file_size;
    size_t read_size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0 || (size_t)file_size > max_bytes) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)calloc((size_t)file_size + 1U, 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    buffer[read_size] = '\0';
    return buffer;
}

static esp_err_t behavior_parse_action_file(const char *action_id, const char *path,
                                            behavior_action_def_t *out_action) {
    char *json = NULL;
    esp_err_t ret;

    if (action_id == NULL || action_id[0] == '\0' || path == NULL || out_action == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    json = behavior_read_text_file(path, BEHAVIOR_ACTION_MAX_FILE_BYTES);
    if (json == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ret = behavior_action_parse_json(action_id, json, strlen(json), out_action);
    free(json);
    return ret;
}

static esp_err_t behavior_load_actions_from_dir(behavior_action_catalog_t *out_catalog) {
    behavior_action_catalog_t catalog = {0};
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int action_count = 0;
    int index = 0;

    if (out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dir = opendir(BEHAVIOR_ACTIONS_DIR);
    if (dir == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (behavior_is_json_filename(entry->d_name)) {
            action_count++;
        }
    }
    closedir(dir);

    if (action_count <= 0) {
        *out_catalog = catalog;
        return ESP_OK;
    }

    catalog.actions =
        (behavior_action_def_t *)behavior_persistent_calloc((size_t)action_count, sizeof(behavior_action_def_t));
    if (catalog.actions == NULL) {
        return ESP_ERR_NO_MEM;
    }

    dir = opendir(BEHAVIOR_ACTIONS_DIR);
    if (dir == NULL) {
        behavior_free_action_catalog(&catalog);
        return ESP_ERR_NOT_FOUND;
    }

    while ((entry = readdir(dir)) != NULL) {
        char action_id[BEHAVIOR_STATE_ID_LEN];
        char path[BEHAVIOR_ACTION_PATH_LEN];
        size_t dir_len;
        size_t name_len;
        esp_err_t ret;

        if (!behavior_is_json_filename(entry->d_name)) {
            continue;
        }

        behavior_normalize_file_stem(entry->d_name, action_id, sizeof(action_id));
        dir_len = strlen(BEHAVIOR_ACTIONS_DIR);
        name_len = strlen(entry->d_name);
        if (dir_len + 1 + name_len >= sizeof(path)) {
            ESP_LOGE(TAG, "Action path too long: %s/%s", BEHAVIOR_ACTIONS_DIR, entry->d_name);
            closedir(dir);
            behavior_free_action_catalog(&catalog);
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(path, BEHAVIOR_ACTIONS_DIR, dir_len);
        path[dir_len] = '/';
        memcpy(path + dir_len + 1, entry->d_name, name_len);
        path[dir_len + 1 + name_len] = '\0';
        ret = behavior_parse_action_file(action_id, path, &catalog.actions[index]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to parse action '%s': %s", path, esp_err_to_name(ret));
            closedir(dir);
            behavior_free_action_catalog(&catalog);
            return ret;
        }

        ESP_LOGI(TAG, "Loaded action '%s': motion_count=%d duration_ms=%lu", catalog.actions[index].id,
                 catalog.actions[index].motion_count, (unsigned long)catalog.actions[index].total_duration_ms);
        index++;
    }
    closedir(dir);

    catalog.action_count = index;
    *out_catalog = catalog;
    return ESP_OK;
}

static bool behavior_validate_anim_id_for_catalog(const char *anim_id, void *ctx) {
    emoji_anim_type_t type = EMOJI_ANIM_NONE;
    (void)ctx;
    return anim_id != NULL && anim_id[0] != '\0' && animation_registry_type_from_name(anim_id, &type);
}

static esp_err_t behavior_load_catalog_from_file(behavior_catalog_t *out_catalog) {
    const behavior_catalog_candidate_t candidates[] = {
        {BEHAVIOR_STATES_SD_PATH, "sd"},
        {BEHAVIOR_STATES_PATH, "internal"},
    };
    size_t selected_index = SIZE_MAX;
    esp_err_t ret;

    ret = bsp_spiffs_init_default();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Behavior SPIFFS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = behavior_catalog_load_first_valid(candidates, sizeof(candidates) / sizeof(candidates[0]),
                                            BEHAVIOR_MAX_FILE_BYTES, behavior_validate_anim_id_for_catalog, NULL,
                                            out_catalog, &selected_index);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Behavior catalog source=%s path=%s", candidates[selected_index].label,
                 candidates[selected_index].path);
    } else {
        ESP_LOGW(TAG, "No valid behavior catalog on SD or internal SPIFFS: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void behavior_reset_runtime_locked(void) {
    uint32_t now_ms = behavior_now_ms();
    uint32_t next_owner_epoch = s_ctx.animation_owner_epoch + 1U;

    if (next_owner_epoch == 0U) {
        next_owner_epoch = 1U;
    }

    s_ctx.current_state = NULL;
    s_ctx.current_action = NULL;
    behavior_copy_string(s_ctx.current_state_id, sizeof(s_ctx.current_state_id), s_ctx.catalog.default_state);
    s_ctx.current_action_id[0] = '\0';
    s_ctx.state_started_ms = now_ms;
    s_ctx.action_started_ms = s_ctx.state_started_ms;
    s_ctx.next_motion_index = 0;
    s_ctx.next_action_motion_index = 0;
    s_ctx.next_expression_index = 0;
    s_ctx.next_sound_index = 0;
    s_ctx.next_light_index = 0;
    s_ctx.text_override[0] = '\0';
    s_ctx.text_override_font_size = 0;
    s_ctx.text_override_valid = false;
    s_ctx.text_override_alert = false;
    s_ctx.anim_override[0] = '\0';
    s_ctx.anim_override_valid = false;
    s_ctx.anim_override_suppressed = false;
    s_ctx.sound_override[0] = '\0';
    s_ctx.sound_override_valid = false;
    s_ctx.sound_override_suppressed = false;
    s_ctx.suppress_state_sound_events = false;
    s_ctx.wait_for_local_sfx_completion = false;
    s_ctx.resources_one_shot = false;
    s_ctx.hold_logged = false;
    s_ctx.animation_ticket = ANIMATION_TICKET_INVALID;
    s_ctx.animation_owner_epoch = next_owner_epoch;
    s_ctx.animation_correlation_id = 0U;
    s_ctx.animation_type = EMOJI_ANIM_NONE;
    s_ctx.animation_event_overflow_count = 0U;
    behavior_update_state_busy_snapshot_locked();
    behavior_update_action_active_snapshot_locked(now_ms);
}

static bool behavior_is_valid_anim_id(const char *anim_id) {
    emoji_anim_type_t type = EMOJI_ANIM_NONE;
    return anim_id != NULL && anim_id[0] != '\0' && animation_registry_type_from_name(anim_id, &type);
}

static bool behavior_is_nonempty_string(const char *value) {
    return value != NULL && value[0] != '\0';
}

static bool behavior_matches_nullable_override(const char *current, bool current_valid, const char *requested) {
    if (!behavior_is_nonempty_string(requested)) {
        return !current_valid;
    }

    return current_valid && strcmp(current, requested) == 0;
}

static bool behavior_is_same_state_action_request_locked(const char *state_id, const char *action_id) {
    if (state_id == NULL || state_id[0] == '\0' || strcmp(state_id, s_ctx.current_state_id) != 0) {
        return false;
    }

    if (action_id == NULL || action_id[0] == '\0') {
        return s_ctx.current_action_id[0] == '\0';
    }

    return strcmp(action_id, s_ctx.current_action_id) == 0;
}

static bool behavior_is_same_display_request_locked(const char *text, int font_size, bool alert_text,
                                                    const char *anim_id, bool suppress_anim) {
    if ((text != NULL) != s_ctx.text_override_valid) {
        return false;
    }
    if (text != NULL && strcmp(s_ctx.text_override, text) != 0) {
        return false;
    }
    if (text != NULL && (s_ctx.text_override_font_size != font_size || s_ctx.text_override_alert != alert_text)) {
        return false;
    }
    if (s_ctx.anim_override_suppressed != suppress_anim) {
        return false;
    }
    if (suppress_anim) {
        return true;
    }
    if (!behavior_matches_nullable_override(s_ctx.anim_override, s_ctx.anim_override_valid, anim_id)) {
        return false;
    }

    return true;
}

static bool behavior_is_same_override_request_locked(const char *text, int font_size, bool alert_text,
                                                     const char *anim_id, bool suppress_anim, const char *sound_id,
                                                     bool suppress_sound) {
    if (!behavior_is_same_display_request_locked(text, font_size, alert_text, anim_id, suppress_anim)) {
        return false;
    }

    if (s_ctx.sound_override_suppressed != suppress_sound) {
        return false;
    }
    if (suppress_sound) {
        return true;
    }

    return behavior_matches_nullable_override(s_ctx.sound_override, s_ctx.sound_override_valid, sound_id);
}

static void behavior_set_anim_override_locked(const char *anim_id, bool suppress_anim) {
    s_ctx.anim_override_suppressed = suppress_anim;
    if (suppress_anim) {
        s_ctx.anim_override[0] = '\0';
        s_ctx.anim_override_valid = false;
    } else if (behavior_is_valid_anim_id(anim_id)) {
        behavior_copy_string(s_ctx.anim_override, sizeof(s_ctx.anim_override), anim_id);
        s_ctx.anim_override_valid = true;
    } else {
        s_ctx.anim_override[0] = '\0';
        s_ctx.anim_override_valid = false;
    }
}

static void behavior_set_sound_override_locked(const char *sound_id, bool suppress_sound) {
    s_ctx.sound_override_suppressed = suppress_sound;
    if (suppress_sound) {
        s_ctx.sound_override[0] = '\0';
        s_ctx.sound_override_valid = false;
    } else if (behavior_is_nonempty_string(sound_id)) {
        behavior_copy_string(s_ctx.sound_override, sizeof(s_ctx.sound_override), sound_id);
        s_ctx.sound_override_valid = true;
    } else {
        s_ctx.sound_override[0] = '\0';
        s_ctx.sound_override_valid = false;
    }
}

static void behavior_get_display_defaults_locked(const char **text, const char **anim, int *font_size) {
    if (text != NULL) {
        *text = NULL;
    }
    if (anim != NULL) {
        *anim = NULL;
    }
    if (font_size != NULL) {
        *font_size = 0;
    }

    if (s_ctx.current_state != NULL && s_ctx.current_state->expression_count > 0) {
        if (text != NULL) {
            *text = s_ctx.current_state->expression[0].text;
        }
        if (anim != NULL && s_ctx.current_state->expression[0].anim[0] != '\0') {
            *anim = s_ctx.current_state->expression[0].anim;
        }
        if (font_size != NULL) {
            *font_size = s_ctx.current_state->expression[0].font_size;
        }
    } else if (anim != NULL) {
        *anim = s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state;
    }
}

static void behavior_refresh_display_locked(behavior_display_request_t *request) {
    behavior_capture_display_request_locked(request);
}

static bool behavior_stop_current_action_locked(const char *source) {
    if (s_ctx.current_action == NULL) {
        return false;
    }

    ESP_LOGI(TAG, "Interrupt action loop '%s' for state '%s' via %s",
             s_ctx.current_action_id[0] != '\0' ? s_ctx.current_action_id : "<none>",
             s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state,
             (source != NULL && source[0] != '\0') ? source : "external_control");

    s_ctx.current_action = NULL;
    s_ctx.current_action_id[0] = '\0';
    s_ctx.next_action_motion_index = 0;
    s_ctx.action_started_ms = behavior_now_ms();
    behavior_update_action_active_snapshot_locked(s_ctx.action_started_ms);
    (void)behavior_executor_cancel_behavior_motion();
    return true;
}

static bool behavior_should_loop_action_locked(void) {
    return behavior_scheduler_action_should_loop(s_ctx.current_state, s_ctx.current_action, !s_ctx.resources_one_shot);
}

static uint32_t behavior_action_elapsed_ms_locked(uint32_t now_ms) {
    return now_ms - s_ctx.action_started_ms;
}

static void behavior_log_action_start_locked(const char *state_id, const behavior_action_def_t *action_def) {
    if (action_def == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Starting action '%s' for state '%s': motion_count=%d duration_ms=%lu", action_def->id,
             state_id != NULL ? state_id : s_ctx.catalog.default_state, action_def->motion_count,
             (unsigned long)action_def->total_duration_ms);
}

static bool behavior_start_action_motion_sequence_locked(const behavior_action_def_t *action_def) {
    esp_err_t ret;

    if (action_def == NULL || action_def->motion_count <= 0) {
        return false;
    }

    ret = behavior_executor_play_motion_sequence(action_def->motion, action_def->motion_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Action '%s' sequence submission failed; retaining timed fallback err=%s",
                 action_def->id[0] != '\0' ? action_def->id : "<none>", esp_err_to_name(ret));
        return false;
    }

    s_ctx.next_action_motion_index = action_def->motion_count;
    ESP_LOGI(TAG, "Action '%s' motion submitted as sequence: motion_count=%d",
             action_def->id[0] != '\0' ? action_def->id : "<none>", action_def->motion_count);
    return true;
}

static void behavior_dispatch_motion_locked(const behavior_motion_event_t *event) {
    if (event == NULL) {
        return;
    }

    (void)behavior_executor_move_motion(event);
}

static void behavior_dispatch_expression_locked(const behavior_expression_event_t *event,
                                                behavior_display_request_t *request) {
    const char *text = NULL;
    const char *anim = NULL;
    int font_size = 0;

    if (event == NULL) {
        return;
    }

    if (request == NULL) {
        return;
    }

    behavior_clear_display_request(request);

    text = event->text;
    anim = event->anim[0] != '\0' ? event->anim : NULL;
    font_size = event->font_size;
    if (s_ctx.text_override_valid) {
        text = s_ctx.text_override;
        font_size = s_ctx.text_override_font_size;
    }
    if (s_ctx.anim_override_suppressed) {
        anim = NULL;
    } else if (s_ctx.anim_override_valid) {
        anim = s_ctx.anim_override;
    }
    if (behavior_animation_is_current_locked(anim)) {
        anim = NULL;
    }

    request->pending = true;
    request->has_text = (text != NULL);
    request->has_anim = (anim != NULL);
    request->font_size = font_size;
    request->alert_text = s_ctx.text_override_valid && s_ctx.text_override_alert;
    behavior_copy_string(request->text, sizeof(request->text), text);
    behavior_copy_string(request->anim, sizeof(request->anim), anim);
    behavior_copy_string(request->state_id, sizeof(request->state_id), s_ctx.current_state_id);
    request->playback_mode =
        behavior_scheduler_effective_animation_playback(event->playback_mode, s_ctx.resources_one_shot);
    request->repeat_count = s_ctx.resources_one_shot ? 0U : event->repeat_count;
    request->fade_in_ms = event->fade_in_ms;
    request->owner_epoch = s_ctx.animation_owner_epoch;
    request->correlation_id = s_ctx.animation_correlation_id;
}

static esp_err_t behavior_dispatch_sound_id_locked(const char *sound_id) {
    if (sound_id == NULL || sound_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    return behavior_executor_play_sound(sound_id);
}

static void behavior_dispatch_sound_locked(const behavior_sound_event_t *event) {
    esp_err_t ret;

    if (event == NULL) {
        return;
    }

    ret = behavior_dispatch_sound_id_locked(event->sound_id);
    if (ret == ESP_OK) {
        s_ctx.wait_for_local_sfx_completion = true;
    }
}

static void behavior_dispatch_light_locked(const behavior_light_event_t *event) {
    esp_err_t ret;

    if (event == NULL) {
        return;
    }
    ret = behavior_executor_apply_light(event);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Light event failed: effect=%s rgb=%u,%u,%u err=%s", event->effect, (unsigned)event->red,
                 (unsigned)event->green, (unsigned)event->blue, esp_err_to_name(ret));
    }
}

static void behavior_dispatch_scheduler_command_locked(const behavior_scheduler_command_t *command,
                                                       behavior_display_request_t *request) {
    if (command == NULL) {
        return;
    }

    switch (command->type) {
    case BEHAVIOR_SCHEDULER_COMMAND_STATE_MOTION:
        behavior_dispatch_motion_locked(command->motion);
        break;

    case BEHAVIOR_SCHEDULER_COMMAND_ACTION_MOTION:
        if (command->motion != NULL) {
            ESP_LOGI(TAG, "Dispatch action '%s': at_ms=%lu x=%d y=%d duration_ms=%d profile=%u",
                     s_ctx.current_action_id[0] != '\0' ? s_ctx.current_action_id : "<none>",
                     (unsigned long)command->motion->at_ms, command->motion->x_deg, command->motion->y_deg,
                     command->motion->duration_ms, (unsigned)command->motion->motion_profile);
        }
        behavior_dispatch_motion_locked(command->motion);
        break;

    case BEHAVIOR_SCHEDULER_COMMAND_EXPRESSION:
        behavior_dispatch_expression_locked(command->expression, request);
        break;

    case BEHAVIOR_SCHEDULER_COMMAND_SOUND:
        behavior_dispatch_sound_locked(command->sound);
        break;

    case BEHAVIOR_SCHEDULER_COMMAND_LIGHT:
        behavior_dispatch_light_locked(command->light);
        break;

    case BEHAVIOR_SCHEDULER_COMMAND_NONE:
    default:
        break;
    }
}

static bool behavior_apply_sound_override_locked(const char *sound_id, bool suppress_sound) {
    esp_err_t ret;

    behavior_set_sound_override_locked(sound_id, suppress_sound);
    if (suppress_sound) {
        return true;
    }
    if (!s_ctx.sound_override_valid) {
        return false;
    }

    ret = behavior_dispatch_sound_id_locked(s_ctx.sound_override);
    if (ret == ESP_OK) {
        s_ctx.wait_for_local_sfx_completion = true;
    }
    return ret == ESP_OK || ret == ESP_ERR_INVALID_STATE;
}

static bool behavior_all_state_events_dispatched_locked(void) {
    const behavior_scheduler_snapshot_t snapshot = {
        .state = s_ctx.current_state,
        .action = s_ctx.current_action,
        .next_motion_index = s_ctx.next_motion_index,
        .next_expression_index = s_ctx.next_expression_index,
        .next_sound_index = s_ctx.next_sound_index,
        .next_light_index = s_ctx.next_light_index,
        .next_action_motion_index = s_ctx.next_action_motion_index,
    };

    return behavior_scheduler_all_state_events_dispatched(&snapshot);
}

static bool behavior_all_action_events_dispatched_locked(void) {
    return behavior_scheduler_all_action_events_dispatched(s_ctx.current_action, s_ctx.next_action_motion_index);
}

static void behavior_dispatch_due_events_locked(uint32_t now_ms, behavior_display_request_t *request) {
    bool has_more_due_events;

    if (s_ctx.current_state == NULL) {
        return;
    }

    do {
        behavior_scheduler_tick_input_t scheduler_input = {
            .state = s_ctx.current_state,
            .action = s_ctx.current_action,
            .state_elapsed_ms = now_ms - s_ctx.state_started_ms,
            .action_elapsed_ms = behavior_action_elapsed_ms_locked(now_ms),
            .next_motion_index = s_ctx.next_motion_index,
            .next_action_motion_index = s_ctx.next_action_motion_index,
            .next_expression_index = s_ctx.next_expression_index,
            .next_sound_index = s_ctx.next_sound_index,
            .next_light_index = s_ctx.next_light_index,
        };
        behavior_scheduler_tick_result_t scheduler_result;
        size_t index;

        behavior_scheduler_collect_due_events(&scheduler_input, &scheduler_result);
        for (index = 0; index < scheduler_result.command_count; ++index) {
            behavior_dispatch_scheduler_command_locked(&scheduler_result.commands[index], request);
        }

        s_ctx.next_motion_index = scheduler_result.next_motion_index;
        s_ctx.next_action_motion_index = scheduler_result.next_action_motion_index;
        s_ctx.next_expression_index = scheduler_result.next_expression_index;
        s_ctx.next_sound_index = scheduler_result.next_sound_index;
        s_ctx.next_light_index = scheduler_result.next_light_index;
        has_more_due_events = scheduler_result.has_more_due_events;
    } while (has_more_due_events);

    if (s_ctx.current_state->expression_count == 0 && (s_ctx.text_override_valid || s_ctx.anim_override_valid)) {
        behavior_refresh_display_locked(request);
    }
}

static uint32_t behavior_non_loop_done_at_ms_locked(void) {
    return behavior_scheduler_non_loop_done_at_ms(s_ctx.current_state, s_ctx.current_action,
                                                  BEHAVIOR_DEFAULT_ONESHOT_HOLD_MS);
}

static void behavior_begin_animation_correlation_locked(void) {
    s_ctx.animation_ticket = ANIMATION_TICKET_INVALID;
    s_ctx.animation_type = EMOJI_ANIM_NONE;
    s_ctx.animation_correlation_id++;
    if (s_ctx.animation_correlation_id == 0U) {
        s_ctx.animation_correlation_id = 1U;
    }
}

static void behavior_refresh_same_state_overrides_locked(const char *text, int font_size, bool alert_text,
                                                         const char *anim_id, bool suppress_anim, const char *sound_id,
                                                         bool suppress_sound,
                                                         behavior_display_request_t *display_request) {
    /* Same-state ambient rotation is a new presentation request. The animation
     * controller rejects it as stale unless the correlation id advances. */
    behavior_begin_animation_correlation_locked();
    s_ctx.text_override_valid = (text != NULL);
    behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
    s_ctx.text_override_font_size = font_size;
    s_ctx.text_override_alert = alert_text;
    behavior_set_anim_override_locked(anim_id, suppress_anim);
    s_ctx.suppress_state_sound_events = false;
    if (behavior_apply_sound_override_locked(sound_id, suppress_sound)) {
        s_ctx.suppress_state_sound_events = true;
        s_ctx.next_sound_index = s_ctx.current_state != NULL ? s_ctx.current_state->sound_count : 0;
    }
    behavior_refresh_display_locked(display_request);
}

static void behavior_force_animation_refresh_locked(behavior_display_request_t *display_request) {
    behavior_begin_animation_correlation_locked();
    behavior_capture_display_request_locked_ex(display_request, true);
    ESP_LOGI(TAG, "Explicit animation refresh queued state=%s owner=%lu correlation=%lu",
             s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state,
             (unsigned long)s_ctx.animation_owner_epoch, (unsigned long)s_ctx.animation_correlation_id);
}

static esp_err_t behavior_schedule_state_locked(const char *state_id, const char *text, int font_size, bool alert_text,
                                                const char *anim_id, bool suppress_anim, const char *sound_id,
                                                bool suppress_sound, const char *action_id, bool resources_one_shot,
                                                behavior_display_request_t *display_request) {
    behavior_state_def_t *state_def = behavior_find_state_locked(state_id);
    behavior_action_def_t *action_def = behavior_find_action_locked(action_id);
    const char *effective_state_id = NULL;
    uint32_t now_ms = behavior_now_ms();

    if (s_ctx.current_state != NULL && s_ctx.animation_ticket != ANIMATION_TICKET_INVALID &&
        behavior_scheduler_should_defer_animation_transition_target(s_ctx.current_state, state_id, NULL)) {
        s_ctx.text_override_valid = (text != NULL);
        behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
        s_ctx.text_override_font_size = font_size;
        s_ctx.text_override_alert = alert_text;
        behavior_set_sound_override_locked(sound_id, suppress_sound);
        behavior_capture_display_request_locked(display_request);
        display_request->has_anim = false;
        display_request->anim[0] = '\0';
        ESP_LOGI(TAG, "Deferred target state=%s until animation completes state=%s anim=%s", state_id,
                 s_ctx.current_state->id, s_ctx.current_state->animation_complete_anim);
        return ESP_OK;
    }

    if (state_def == NULL) {
        if (strcmp(state_id, s_ctx.catalog.default_state) != 0 && strcmp(state_id, "standby") != 0) {
            return ESP_ERR_NOT_FOUND;
        }

        effective_state_id = s_ctx.catalog.default_state;
        if (!resources_one_shot && !s_ctx.resources_one_shot &&
            behavior_is_same_state_action_request_locked(effective_state_id,
                                                         action_def != NULL ? action_def->id : NULL)) {
            bool same_overrides = behavior_is_same_override_request_locked(text, font_size, alert_text, anim_id,
                                                                           suppress_anim, sound_id, suppress_sound);

            if (same_overrides) {
                ESP_LOGI(TAG, "Ignoring repeated request with unchanged overrides: state=%s action=%s",
                         effective_state_id, action_def != NULL ? action_def->id : "<none>");
            } else {
                ESP_LOGI(TAG, "Refreshing repeated state/action request with updated overrides: state=%s action=%s",
                         effective_state_id, action_def != NULL ? action_def->id : "<none>");
            }
            if (same_overrides) {
                return ESP_OK;
            }
            behavior_refresh_same_state_overrides_locked(text, font_size, alert_text, anim_id, suppress_anim, sound_id,
                                                         suppress_sound, display_request);
            return ESP_OK;
        }

        if (s_ctx.current_state != NULL || s_ctx.current_action != NULL) {
            (void)behavior_executor_cancel_behavior_motion();
        }

        behavior_begin_animation_correlation_locked();
        s_ctx.current_state = NULL;
        s_ctx.current_action = action_def;
        behavior_copy_string(s_ctx.current_state_id, sizeof(s_ctx.current_state_id), s_ctx.catalog.default_state);
        behavior_copy_string(s_ctx.current_action_id, sizeof(s_ctx.current_action_id),
                             action_def != NULL ? action_def->id : NULL);
        s_ctx.state_started_ms = now_ms;
        s_ctx.action_started_ms = now_ms;
        s_ctx.next_motion_index = 0;
        s_ctx.next_action_motion_index = 0;
        s_ctx.next_expression_index = 0;
        s_ctx.next_sound_index = 0;
        s_ctx.next_light_index = 0;
        s_ctx.text_override_valid = (text != NULL);
        behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
        s_ctx.text_override_font_size = font_size;
        s_ctx.text_override_alert = alert_text;
        behavior_set_anim_override_locked(anim_id, suppress_anim);
        behavior_set_sound_override_locked(sound_id, suppress_sound);
        s_ctx.suppress_state_sound_events = false;
        s_ctx.wait_for_local_sfx_completion = false;
        s_ctx.resources_one_shot = resources_one_shot;
        s_ctx.hold_logged = false;
        if (behavior_apply_sound_override_locked(sound_id, suppress_sound)) {
            s_ctx.suppress_state_sound_events = true;
            s_ctx.next_sound_index = s_ctx.current_state != NULL ? s_ctx.current_state->sound_count : 0;
        }
        behavior_log_action_start_locked(effective_state_id, action_def);
        (void)behavior_start_action_motion_sequence_locked(action_def);
        behavior_capture_display_request_locked(display_request);
        return ESP_OK;
    }

    effective_state_id = state_def->id;
    if (!resources_one_shot && !s_ctx.resources_one_shot &&
        behavior_is_same_state_action_request_locked(effective_state_id, action_def != NULL ? action_def->id : NULL)) {
        bool same_overrides = behavior_is_same_override_request_locked(text, font_size, alert_text, anim_id,
                                                                       suppress_anim, sound_id, suppress_sound);

        if (same_overrides) {
            ESP_LOGI(TAG, "Ignoring repeated request with unchanged overrides: state=%s action=%s", effective_state_id,
                     action_def != NULL ? action_def->id : "<none>");
        } else {
            ESP_LOGI(TAG, "Refreshing repeated state/action request with updated overrides: state=%s action=%s",
                     effective_state_id, action_def != NULL ? action_def->id : "<none>");
        }
        if (same_overrides) {
            return ESP_OK;
        }
        behavior_refresh_same_state_overrides_locked(text, font_size, alert_text, anim_id, suppress_anim, sound_id,
                                                     suppress_sound, display_request);
        return ESP_OK;
    }

    if (s_ctx.current_state != NULL || s_ctx.current_action != NULL) {
        (void)behavior_executor_cancel_behavior_motion();
    }

    behavior_begin_animation_correlation_locked();
    s_ctx.current_state = state_def;
    s_ctx.current_action = action_def;
    behavior_copy_string(s_ctx.current_state_id, sizeof(s_ctx.current_state_id), state_def->id);
    behavior_copy_string(s_ctx.current_action_id, sizeof(s_ctx.current_action_id),
                         action_def != NULL ? action_def->id : NULL);
    s_ctx.state_started_ms = now_ms;
    s_ctx.action_started_ms = now_ms;
    s_ctx.next_motion_index = 0;
    s_ctx.next_action_motion_index = 0;
    s_ctx.next_expression_index = 0;
    s_ctx.next_sound_index = 0;
    s_ctx.next_light_index = 0;
    s_ctx.text_override_valid = (text != NULL);
    behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
    s_ctx.text_override_font_size = font_size;
    s_ctx.text_override_alert = alert_text;
    behavior_set_anim_override_locked(anim_id, suppress_anim);
    behavior_set_sound_override_locked(sound_id, suppress_sound);
    s_ctx.wait_for_local_sfx_completion = false;
    s_ctx.resources_one_shot = resources_one_shot;
    s_ctx.suppress_state_sound_events = behavior_apply_sound_override_locked(sound_id, suppress_sound);
    s_ctx.hold_logged = false;
    if (s_ctx.suppress_state_sound_events) {
        s_ctx.next_sound_index = s_ctx.current_state->sound_count;
    }
    behavior_log_action_start_locked(effective_state_id, action_def);
    (void)behavior_start_action_motion_sequence_locked(action_def);
    behavior_dispatch_due_events_locked(now_ms, display_request);
    return ESP_OK;
}

static bool behavior_apply_animation_terminal_transition_locked(behavior_animation_terminal_t terminal,
                                                                behavior_display_request_t *display_request) {
    const behavior_state_def_t *state = s_ctx.current_state;
    const char *target_state;
    const char *completed_animation = NULL;
    char target_state_copy[BEHAVIOR_STATE_ID_LEN];
    char text_copy[BEHAVIOR_TEXT_LEN];
    char sound_copy[BEHAVIOR_SOUND_ID_LEN];
    const char *text = NULL;
    const char *sound = NULL;
    int font_size = s_ctx.text_override_font_size;
    bool alert_text = s_ctx.text_override_alert;
    bool suppress_sound = s_ctx.sound_override_suppressed;
    esp_err_t ret;

    if (state == NULL ||
        (terminal != BEHAVIOR_ANIMATION_TERMINAL_COMPLETED && terminal != BEHAVIOR_ANIMATION_TERMINAL_FAILED)) {
        return false;
    }

    if (terminal == BEHAVIOR_ANIMATION_TERMINAL_COMPLETED) {
        completed_animation = animation_registry_name(s_ctx.animation_type);
        target_state = behavior_scheduler_animation_transition_target(state, completed_animation);
    } else {
        target_state = state->animation_failure_state[0] != '\0' ? state->animation_failure_state : NULL;
    }
    s_ctx.animation_ticket = ANIMATION_TICKET_INVALID;
    s_ctx.animation_type = EMOJI_ANIM_NONE;
    if (target_state == NULL) {
        return false;
    }

    behavior_copy_string(target_state_copy, sizeof(target_state_copy), target_state);
    if (s_ctx.text_override_valid) {
        behavior_copy_string(text_copy, sizeof(text_copy), s_ctx.text_override);
        text = text_copy;
    }
    if (s_ctx.sound_override_valid) {
        behavior_copy_string(sound_copy, sizeof(sound_copy), s_ctx.sound_override);
        sound = sound_copy;
    }

    ESP_LOGI(TAG, "Animation terminal transition state=%s outcome=%s anim=%s target=%s", state->id,
             terminal == BEHAVIOR_ANIMATION_TERMINAL_COMPLETED ? "completed" : "failed",
             completed_animation != NULL ? completed_animation : state->animation_complete_anim, target_state_copy);
    ret = behavior_schedule_state_locked(target_state_copy, text, font_size, alert_text, NULL, false, sound,
                                         suppress_sound, NULL, false, display_request);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Animation terminal transition failed state=%s target=%s err=%s", state->id, target_state_copy,
                 esp_err_to_name(ret));
        return false;
    }

    return true;
}

static bool behavior_apply_animation_event_locked(const animation_event_t *event,
                                                  behavior_display_request_t *display_request) {
    behavior_animation_event_t observation;
    behavior_animation_terminal_t terminal = behavior_animation_reduce_terminal(
        s_ctx.animation_ticket, s_ctx.animation_owner_epoch, s_ctx.animation_type, event);

    if (event != NULL && event->request.source == ANIM_SOURCE_BEHAVIOR &&
        s_ctx.animation_ticket != ANIMATION_TICKET_INVALID && event->ticket == s_ctx.animation_ticket &&
        event->request.owner_epoch == s_ctx.animation_owner_epoch && event->request.type == s_ctx.animation_type &&
        s_ctx.animation_observation_queue != NULL) {
        memset(&observation, 0, sizeof(observation));
        behavior_copy_string(observation.state_id, sizeof(observation.state_id), s_ctx.current_state_id);
        observation.event = *event;
        if (xQueueSend(s_ctx.animation_observation_queue, &observation, 0) != pdTRUE) {
            if (behavior_animation_event_is_terminal(event->type)) {
                behavior_animation_event_t discarded;
                (void)xQueueReceive(s_ctx.animation_observation_queue, &discarded, 0);
                if (xQueueSend(s_ctx.animation_observation_queue, &observation, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "Animation terminal observation lost state=%s ticket=%lu type=%d",
                             s_ctx.current_state_id, (unsigned long)event->ticket, (int)event->type);
                }
            } else {
                ESP_LOGW(TAG, "Animation commit observation coalesced state=%s ticket=%lu", s_ctx.current_state_id,
                         (unsigned long)event->ticket);
            }
        }
    }
    if (terminal == BEHAVIOR_ANIMATION_TERMINAL_IGNORE) {
        return false;
    }
    if (terminal == BEHAVIOR_ANIMATION_TERMINAL_RELEASED) {
        s_ctx.animation_ticket = ANIMATION_TICKET_INVALID;
        s_ctx.animation_type = EMOJI_ANIM_NONE;
        return false;
    }
    return behavior_apply_animation_terminal_transition_locked(terminal, display_request);
}

static bool behavior_apply_display_execution_locked(const behavior_display_request_t *command,
                                                    const behavior_display_execution_result_t *result,
                                                    behavior_display_request_t *next_display_request) {
    if (command == NULL || result == NULL || command->owner_epoch != s_ctx.animation_owner_epoch) {
        return false;
    }
    if (!result->animation_attempted) {
        return false;
    }
    if (result->animation_result == ANIMATION_SERVICE_OK && result->animation_ticket != ANIMATION_TICKET_INVALID) {
        s_ctx.animation_ticket = result->animation_ticket;
        s_ctx.animation_type = result->animation_type;
        ESP_LOGI(TAG, "Animation ticket accepted state=%s ticket=%lu owner=%lu anim=%s repeat=%u",
                 s_ctx.current_state_id, (unsigned long)s_ctx.animation_ticket,
                 (unsigned long)s_ctx.animation_owner_epoch, animation_registry_name(s_ctx.animation_type),
                 (unsigned)command->repeat_count);
        return false;
    }

    s_ctx.animation_ticket = ANIMATION_TICKET_INVALID;
    s_ctx.animation_type = result->animation_type;
    ESP_LOGW(TAG, "Animation submit terminal failure state=%s owner=%lu result=%d", s_ctx.current_state_id,
             (unsigned long)s_ctx.animation_owner_epoch, (int)result->animation_result);
    return behavior_apply_animation_terminal_transition_locked(BEHAVIOR_ANIMATION_TERMINAL_FAILED,
                                                               next_display_request);
}

static void behavior_task(void *arg) {
    behavior_display_request_t display_request;
    behavior_state_request_t state_request;
    animation_event_t animation_event;

    (void)arg;

    while (true) {
        bool has_state_request = false;

        behavior_clear_display_request(&display_request);
        while (s_ctx.request_queue != NULL && xQueueReceive(s_ctx.request_queue, &state_request, 0) == pdTRUE) {
            has_state_request = true;
        }

        if (behavior_lock()) {
            uint32_t now_ms = behavior_now_ms();

            if (has_state_request) {
                esp_err_t request_ret = ESP_OK;
                if (state_request.force_animation_refresh) {
                    behavior_force_animation_refresh_locked(&display_request);
                } else {
                    request_ret = behavior_schedule_state_locked(
                        state_request.state_id, state_request.has_text ? state_request.text : NULL,
                        state_request.font_size, state_request.alert_text,
                        state_request.has_anim_override && state_request.anim_id[0] != '\0' ? state_request.anim_id
                                                                                            : NULL,
                        state_request.suppress_anim,
                        state_request.has_sound_override && state_request.sound_id[0] != '\0' ? state_request.sound_id
                                                                                              : NULL,
                        state_request.suppress_sound,
                        state_request.action_id[0] != '\0' ? state_request.action_id : NULL,
                        state_request.resources_one_shot, &display_request);
                    if (request_ret == ESP_OK) {
                        ESP_LOGI(TAG, "Applied queued state request state=%s action=%s", state_request.state_id,
                                 state_request.action_id[0] != '\0' ? state_request.action_id : "<none>");
                    } else {
                        ESP_LOGW(TAG, "Queued state request failed state=%s err=%s", state_request.state_id,
                                 esp_err_to_name(request_ret));
                    }
                }
                now_ms = behavior_now_ms();
            }

            while (s_ctx.animation_event_queue != NULL &&
                   xQueueReceive(s_ctx.animation_event_queue, &animation_event, 0) == pdTRUE) {
                (void)behavior_apply_animation_event_locked(&animation_event, &display_request);
                now_ms = behavior_now_ms();
            }

            if (s_ctx.current_state != NULL) {
                uint32_t elapsed_ms;
                uint32_t action_elapsed_ms;
                uint32_t done_at_ms;

                behavior_dispatch_due_events_locked(now_ms, &display_request);
                elapsed_ms = now_ms - s_ctx.state_started_ms;
                action_elapsed_ms = behavior_action_elapsed_ms_locked(now_ms);

                if (behavior_should_loop_action_locked() && s_ctx.current_action != NULL &&
                    s_ctx.current_action->total_duration_ms > 0 && behavior_all_action_events_dispatched_locked() &&
                    action_elapsed_ms >= s_ctx.current_action->total_duration_ms) {
                    ESP_LOGI(TAG, "Looping action '%s' for state '%s'",
                             s_ctx.current_action_id[0] != '\0' ? s_ctx.current_action_id : "<none>",
                             s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state);
                    s_ctx.action_started_ms = now_ms;
                    s_ctx.next_action_motion_index = 0;
                    (void)behavior_start_action_motion_sequence_locked(s_ctx.current_action);
                    behavior_dispatch_due_events_locked(now_ms, &display_request);
                    action_elapsed_ms = behavior_action_elapsed_ms_locked(now_ms);
                }

                if (s_ctx.current_state->loop) {
                    if (s_ctx.current_state->timeline_end_ms > 0 && behavior_all_state_events_dispatched_locked() &&
                        behavior_all_action_events_dispatched_locked() &&
                        elapsed_ms >= s_ctx.current_state->timeline_end_ms &&
                        (s_ctx.current_action == NULL ||
                         action_elapsed_ms >= s_ctx.current_action->total_duration_ms)) {
                        s_ctx.state_started_ms = now_ms;
                        s_ctx.next_motion_index = 0;
                        s_ctx.next_expression_index = 0;
                        s_ctx.next_sound_index = s_ctx.current_state->sound_count;
                        s_ctx.next_light_index = 0;
                        behavior_dispatch_due_events_locked(now_ms, &display_request);
                    }
                } else {
                    done_at_ms = behavior_non_loop_done_at_ms_locked();
                    if (behavior_all_state_events_dispatched_locked() &&
                        behavior_all_action_events_dispatched_locked() && elapsed_ms >= done_at_ms &&
                        !(s_ctx.wait_for_local_sfx_completion && sfx_service_is_busy())) {
                        if (s_ctx.current_state->hold_until_replaced) {
                            if (!s_ctx.hold_logged) {
                                ESP_LOGI(TAG, "Holding state=%s until next state arrives", s_ctx.current_state_id);
                                s_ctx.hold_logged = true;
                            }
                        } else {
                            esp_err_t fallback_ret =
                                behavior_schedule_state_locked(s_ctx.catalog.default_state, NULL, 0, false, NULL, false,
                                                               NULL, false, NULL, false, &display_request);
                            if (fallback_ret != ESP_OK) {
                                ESP_LOGW(TAG, "Fallback state request failed state=%s err=%s",
                                         s_ctx.catalog.default_state, esp_err_to_name(fallback_ret));
                            }
                        }
                    }
                }
            }

            behavior_update_action_active_snapshot_locked(behavior_now_ms());
            behavior_update_state_busy_snapshot_locked();
            behavior_unlock();
        }

        for (unsigned int apply_count = 0U; display_request.pending && apply_count < 4U; ++apply_count) {
            behavior_display_request_t applied_request = display_request;
            behavior_display_execution_result_t execution_result;

            behavior_clear_display_request(&display_request);
            behavior_apply_display_request(&applied_request, "behavior task", &execution_result);
            if (behavior_lock()) {
                (void)behavior_apply_display_execution_locked(&applied_request, &execution_result, &display_request);
                behavior_unlock();
            }
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BEHAVIOR_TICK_MS));
    }
}

esp_err_t behavior_state_init(void) {
    BaseType_t task_result;
    esp_err_t load_ret;
    animation_service_result_t sink_result;

    if (s_ctx.initialized) {
        return ESP_OK;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGW(TAG, "Animation catalog init failed during behavior init");
    }

    s_ctx.lock = xSemaphoreCreateMutex();
    if (s_ctx.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ctx.request_queue = xQueueCreate(BEHAVIOR_REQUEST_QUEUE_DEPTH, sizeof(behavior_state_request_t));
    if (s_ctx.request_queue == NULL) {
        vSemaphoreDelete(s_ctx.lock);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }

    s_ctx.animation_event_queue =
        xQueueCreateStatic(BEHAVIOR_ANIMATION_EVENT_QUEUE_DEPTH, sizeof(animation_event_t),
                           s_ctx.animation_event_queue_buffer, &s_ctx.animation_event_queue_storage);
    if (s_ctx.animation_event_queue == NULL) {
        vQueueDelete(s_ctx.request_queue);
        vSemaphoreDelete(s_ctx.lock);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }
    s_ctx.animation_observation_queue =
        xQueueCreateStatic(BEHAVIOR_ANIMATION_OBSERVATION_QUEUE_DEPTH, sizeof(behavior_animation_event_t),
                           s_ctx.animation_observation_queue_buffer, &s_ctx.animation_observation_queue_storage);
    if (s_ctx.animation_observation_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create Behavior animation observation queue");
        return ESP_ERR_NO_MEM;
    }

    task_result =
        xTaskCreate(behavior_task, "behavior_state", BEHAVIOR_TASK_STACK, NULL, BEHAVIOR_TASK_PRIORITY, &s_ctx.task);
    if (task_result != pdPASS) {
        vQueueDelete(s_ctx.request_queue);
        vSemaphoreDelete(s_ctx.lock);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }

    behavior_copy_string(s_ctx.catalog.version, sizeof(s_ctx.catalog.version), "1.0");
    behavior_copy_string(s_ctx.catalog.default_state, sizeof(s_ctx.catalog.default_state), "standby");
    behavior_reset_runtime_locked();
    s_ctx.initialized = true;

    sink_result = animation_set_event_sink(behavior_animation_event_sink, NULL);
    if (sink_result != ANIMATION_SERVICE_OK) {
        ESP_LOGW(TAG, "Animation event sink registration deferred/unavailable: result=%d", (int)sink_result);
    }

    load_ret = behavior_state_load();
    if (load_ret != ESP_OK && load_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Behavior state load failed: %s", esp_err_to_name(load_ret));
    }

    return ESP_OK;
}

size_t behavior_state_stack_high_watermark(void) {
    return s_ctx.task != NULL ? (size_t)uxTaskGetStackHighWaterMark(s_ctx.task) : 0U;
}

size_t behavior_state_stack_size(void) {
    return BEHAVIOR_TASK_STACK;
}

esp_err_t behavior_state_load(void) {
    behavior_catalog_t catalog = {0};
    behavior_action_catalog_t action_catalog = {0};
    esp_err_t ret;
    esp_err_t action_ret;
    uint32_t previous_owner_epoch = 0U;

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGW(TAG, "Animation catalog init failed during behavior load");
    }

    ret = behavior_load_catalog_from_file(&catalog);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Behavior catalog reload kept last-known-good data after failure: %s", esp_err_to_name(ret));
        return ret;
    }

    action_ret = behavior_load_actions_from_dir(&action_catalog);
    if (action_ret != ESP_OK && action_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Behavior action reload kept last-known-good bundle after failure: %s",
                 esp_err_to_name(action_ret));
        behavior_free_catalog(&catalog);
        behavior_free_action_catalog(&action_catalog);
        return action_ret;
    }

    if (behavior_lock()) {
        previous_owner_epoch = s_ctx.animation_owner_epoch;
        behavior_free_catalog(&s_ctx.catalog);
        behavior_free_action_catalog(&s_ctx.action_catalog);
        s_ctx.catalog = catalog;
        s_ctx.action_catalog = action_catalog;
        behavior_reset_runtime_locked();
        behavior_unlock();
    } else {
        behavior_free_catalog(&catalog);
        behavior_free_action_catalog(&action_catalog);
        return ESP_FAIL;
    }

    if (previous_owner_epoch != 0U) {
        animation_service_result_t cancel_result = animation_cancel_owner(ANIM_SOURCE_BEHAVIOR, previous_owner_epoch);
        if (cancel_result != ANIMATION_SERVICE_OK && cancel_result != ANIMATION_SERVICE_NOT_FOUND &&
            cancel_result != ANIMATION_SERVICE_INVALID_TRANSITION &&
            cancel_result != ANIMATION_SERVICE_NOT_INITIALIZED) {
            ESP_LOGW(TAG, "Previous behavior animation session cancel failed owner=%lu result=%d",
                     (unsigned long)previous_owner_epoch, (int)cancel_result);
        }
    }

    ESP_LOGI(TAG, "Loaded behavior states v%s: count=%d default=%s actions=%d", s_ctx.catalog.version,
             s_ctx.catalog.state_count, s_ctx.catalog.default_state, s_ctx.action_catalog.action_count);
    return ESP_OK;
}

static esp_err_t behavior_state_set_with_resources_and_action_internal(const char *state_id, const char *text,
                                                                       int font_size, bool alert_text,
                                                                       const char *anim_id, const char *sound_id,
                                                                       const char *action_id, bool resources_one_shot) {
    behavior_state_request_t request;

    if (state_id == NULL || state_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (behavior_state_is_missing_from_catalog(state_id)) {
        esp_err_t reload_ret;

        ESP_LOGI(TAG, "Retrying behavior catalog load for missing state=%s", state_id);
        reload_ret = behavior_state_load();
        if (reload_ret != ESP_OK && reload_ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Behavior catalog reload failed for missing state=%s: %s", state_id,
                     esp_err_to_name(reload_ret));
        }
    }

    ESP_LOGI(
        TAG,
        "Queue state request state=%s action=%s mode=%s text=%s anim_override=%s sound_override=%s font=%d alert=%d",
        state_id, action_id != NULL ? action_id : "<none>", resources_one_shot ? "once" : "inherit",
        text != NULL ? text : "<unchanged>", anim_id != NULL ? anim_id : "<default>",
        sound_id != NULL ? sound_id : "<default>", font_size, alert_text ? 1 : 0);

    behavior_fill_state_request(&request, state_id, text, font_size, alert_text, anim_id, sound_id, action_id,
                                resources_one_shot);
    return behavior_submit_state_request(&request);
}

esp_err_t behavior_state_set(const char *state_id) {
    return behavior_state_set_with_resources(state_id, NULL, 0, NULL, NULL);
}

esp_err_t behavior_state_set_with_text(const char *state_id, const char *text, int font_size) {
    return behavior_state_set_with_text_style(state_id, text, font_size, false);
}

esp_err_t behavior_state_set_with_text_style(const char *state_id, const char *text, int font_size, bool alert_text) {
    return behavior_state_set_with_resources_and_action_internal(state_id, text, font_size, alert_text, NULL, NULL,
                                                                 NULL, false);
}

esp_err_t behavior_state_set_with_resources(const char *state_id, const char *text, int font_size, const char *anim_id,
                                            const char *sound_id) {
    return behavior_state_set_with_resources_and_action(state_id, text, font_size, anim_id, sound_id, NULL);
}

esp_err_t behavior_state_set_with_resources_and_action(const char *state_id, const char *text, int font_size,
                                                       const char *anim_id, const char *sound_id,
                                                       const char *action_id) {
    return behavior_state_set_with_resources_and_action_internal(state_id, text, font_size, false, anim_id, sound_id,
                                                                 action_id, false);
}

esp_err_t behavior_state_set_with_resources_and_action_once(const char *state_id, const char *text, int font_size,
                                                            const char *anim_id, const char *sound_id,
                                                            const char *action_id) {
    return behavior_state_set_with_resources_and_action_internal(state_id, text, font_size, false, anim_id, sound_id,
                                                                 action_id, true);
}

esp_err_t behavior_state_set_text(const char *text, int font_size) {
    return behavior_state_set_text_style(text, font_size, false);
}

esp_err_t behavior_state_set_text_style(const char *text, int font_size, bool alert_text) {
    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (!behavior_lock()) {
        return ESP_FAIL;
    }

    s_ctx.text_override_valid = (text != NULL);
    behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
    s_ctx.text_override_font_size = font_size;
    s_ctx.text_override_alert = alert_text;
    behavior_unlock();

    return behavior_executor_apply_text(text, font_size, alert_text);
}

esp_err_t behavior_state_refresh_animation(void) {
    behavior_state_request_t request;

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }
    memset(&request, 0, sizeof(request));
    request.force_animation_refresh = true;
    return behavior_submit_state_request(&request);
}

esp_err_t behavior_state_cancel(void) {
    char default_state[BEHAVIOR_STATE_ID_LEN] = "standby";

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }
    if (behavior_lock()) {
        behavior_copy_string(default_state, sizeof(default_state), s_ctx.catalog.default_state);
        behavior_unlock();
    }
    sfx_service_stop();
    (void)behavior_executor_cancel_behavior_motion();
    return behavior_state_set(default_state);
}

const char *behavior_state_get_current(void) {
    if (!s_ctx.initialized) {
        return "standby";
    }
    return s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state;
}

bool behavior_state_is_busy(void) {
    if (!s_ctx.initialized) {
        return false;
    }

    return sfx_service_is_busy() || s_state_busy_snapshot;
}

bool behavior_state_poll_animation_event(behavior_animation_event_t *event_out) {
    if (event_out == NULL || s_ctx.animation_observation_queue == NULL) {
        return false;
    }
    return xQueueReceive(s_ctx.animation_observation_queue, event_out, 0) == pdTRUE;
}

bool behavior_state_has_action(const char *action_id) {
    bool has_action = false;

    if (action_id == NULL || action_id[0] == '\0') {
        return false;
    }
    if (behavior_state_init() != ESP_OK) {
        return false;
    }
    if (!behavior_lock()) {
        return false;
    }

    has_action = behavior_find_action_locked(action_id) != NULL;
    behavior_unlock();
    return has_action;
}

bool behavior_state_has_state(const char *state_id) {
    bool found = false;

    if (state_id == NULL || state_id[0] == '\0' || behavior_state_init() != ESP_OK || !behavior_lock()) {
        return false;
    }
    found = behavior_find_state_locked(state_id) != NULL;
    behavior_unlock();
    return found;
}

bool behavior_state_is_action_active(void) {
    if (behavior_state_init() != ESP_OK) {
        return false;
    }

    return behavior_action_active_snapshot_is_active(behavior_now_ms());
}

esp_err_t behavior_state_interrupt_action(const char *source) {
    esp_err_t ret = ESP_OK;

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }
    if (!behavior_lock()) {
        return ESP_FAIL;
    }

    if (!behavior_stop_current_action_locked(source)) {
        ret = ESP_ERR_NOT_FOUND;
    }

    behavior_unlock();
    return ret;
}
