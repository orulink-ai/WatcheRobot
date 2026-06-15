#include "behavior_state_service.h"

#include "anim_storage.h"
#include "behavior_action_parser.h"
#include "behavior_catalog_parser.h"
#include "behavior_executor.h"
#include "behavior_scheduler.h"
#include "behavior_types.h"
#include "display_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
#define BEHAVIOR_ACTIONS_DIR "/spiffs/actions"
#define BEHAVIOR_TASK_STACK 6144
#define BEHAVIOR_TASK_PRIORITY 5
#define BEHAVIOR_TICK_MS 10
#define BEHAVIOR_MAX_FILE_BYTES 16384
#define BEHAVIOR_ACTION_MAX_FILE_BYTES 32768
#define BEHAVIOR_ACTION_PATH_LEN 160
#define BEHAVIOR_DEFAULT_ONESHOT_HOLD_MS 1200U
#define BEHAVIOR_REQUEST_QUEUE_DEPTH 1
#define BEHAVIOR_QUERY_LOCK_TIMEOUT_MS 5U
#define BEHAVIOR_QUERY_TIMEOUT_LOG_INTERVAL_MS 1000U

typedef struct {
    SemaphoreHandle_t lock;
    QueueHandle_t request_queue;
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
    bool hold_logged;
} behavior_context_t;

typedef behavior_display_command_t behavior_display_request_t;

typedef struct {
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
} behavior_state_request_t;

static behavior_context_t s_ctx = {0};

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

static bool behavior_lock_with_timeout(uint32_t timeout_ms) {
    if (s_ctx.lock == NULL) {
        return false;
    }

    return xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

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
                                        const char *action_id) {
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
}

static esp_err_t behavior_submit_state_request(const behavior_state_request_t *request) {
    BaseType_t queue_result;

    if (request == NULL || request->state_id[0] == '\0') {
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

static void behavior_capture_display_request_locked(behavior_display_request_t *request) {
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

    request->pending = true;
    request->has_text = (text != NULL);
    request->has_anim = (anim != NULL);
    request->font_size = font_size;
    request->alert_text = s_ctx.text_override_valid && s_ctx.text_override_alert;
    behavior_copy_string(request->text, sizeof(request->text), text);
    behavior_copy_string(request->anim, sizeof(request->anim), anim);
    behavior_copy_string(request->state_id, sizeof(request->state_id), s_ctx.current_state_id);
}

static void behavior_apply_display_request(const behavior_display_request_t *request, const char *reason) {
    behavior_executor_apply_display(request, reason);
}

static void behavior_log_query_timeout_once(const char *query_name, uint32_t *last_log_ms) {
    uint32_t now_ms;

    if (query_name == NULL || last_log_ms == NULL) {
        return;
    }

    now_ms = behavior_now_ms();
    if (*last_log_ms != 0U && (now_ms - *last_log_ms) < BEHAVIOR_QUERY_TIMEOUT_LOG_INTERVAL_MS) {
        return;
    }

    *last_log_ms = now_ms;
    ESP_LOGW(TAG, "%s timed out waiting for behavior lock; treating behavior as busy", query_name);
}

static uint32_t behavior_now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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

    catalog.actions = (behavior_action_def_t *)calloc((size_t)action_count, sizeof(behavior_action_def_t));
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
    (void)ctx;
    return anim_id != NULL && anim_id[0] != '\0' && display_emoji_from_string(anim_id) != EMOJI_UNKNOWN;
}

static esp_err_t behavior_load_catalog_from_file(behavior_catalog_t *out_catalog) {
    char *json = NULL;
    esp_err_t ret;

    json = behavior_read_text_file(BEHAVIOR_STATES_PATH, BEHAVIOR_MAX_FILE_BYTES);
    if (json == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ret = behavior_catalog_parse_json(json, strlen(json), behavior_validate_anim_id_for_catalog, NULL, out_catalog);
    free(json);
    return ret;
}

static void behavior_reset_runtime_locked(void) {
    s_ctx.current_state = NULL;
    s_ctx.current_action = NULL;
    behavior_copy_string(s_ctx.current_state_id, sizeof(s_ctx.current_state_id), s_ctx.catalog.default_state);
    s_ctx.current_action_id[0] = '\0';
    s_ctx.state_started_ms = behavior_now_ms();
    s_ctx.action_started_ms = s_ctx.state_started_ms;
    s_ctx.next_motion_index = 0;
    s_ctx.next_action_motion_index = 0;
    s_ctx.next_expression_index = 0;
    s_ctx.next_sound_index = 0;
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
    s_ctx.hold_logged = false;
}

static bool behavior_is_valid_anim_id(const char *anim_id) {
    return anim_id != NULL && anim_id[0] != '\0' && display_emoji_from_string(anim_id) != EMOJI_UNKNOWN;
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
    (void)behavior_executor_cancel_behavior_motion();
    return true;
}

static bool behavior_should_loop_action_locked(void) {
    return behavior_scheduler_action_should_loop(s_ctx.current_state, s_ctx.current_action);
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
static void behavior_dispatch_motion_locked(const behavior_motion_event_t *event) {
    if (event == NULL) {
        return;
    }

    (void)behavior_executor_move_motion(event);
}

static void behavior_dispatch_expression_locked(const behavior_expression_event_t *event,
                                                behavior_display_request_t *request) {
    if (event == NULL) {
        return;
    }

    behavior_capture_display_request_locked(request);
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

static esp_err_t behavior_schedule_state_locked(const char *state_id, const char *text, int font_size, bool alert_text,
                                                const char *anim_id, bool suppress_anim, const char *sound_id,
                                                bool suppress_sound, const char *action_id,
                                                behavior_display_request_t *display_request) {
    behavior_state_def_t *state_def = behavior_find_state_locked(state_id);
    behavior_action_def_t *action_def = behavior_find_action_locked(action_id);
    const char *effective_state_id = NULL;
    uint32_t now_ms = behavior_now_ms();

    if (state_def == NULL) {
        if (strcmp(state_id, s_ctx.catalog.default_state) != 0 && strcmp(state_id, "standby") != 0) {
            return ESP_ERR_NOT_FOUND;
        }

        effective_state_id = s_ctx.catalog.default_state;
        if (behavior_is_same_state_action_request_locked(effective_state_id,
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
            behavior_capture_display_request_locked(display_request);
            return ESP_OK;
        }

        if (s_ctx.current_state != NULL || s_ctx.current_action != NULL) {
            (void)behavior_executor_cancel_behavior_motion();
        }

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
        s_ctx.text_override_valid = (text != NULL);
        behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
        s_ctx.text_override_font_size = font_size;
        s_ctx.text_override_alert = alert_text;
        behavior_set_anim_override_locked(anim_id, suppress_anim);
        behavior_set_sound_override_locked(sound_id, suppress_sound);
        s_ctx.suppress_state_sound_events = false;
        s_ctx.wait_for_local_sfx_completion = false;
        s_ctx.hold_logged = false;
        if (behavior_apply_sound_override_locked(sound_id, suppress_sound)) {
            s_ctx.suppress_state_sound_events = true;
            s_ctx.next_sound_index = s_ctx.current_state != NULL ? s_ctx.current_state->sound_count : 0;
        }
        behavior_log_action_start_locked(effective_state_id, action_def);
        behavior_capture_display_request_locked(display_request);
        return ESP_OK;
    }

    effective_state_id = state_def->id;
    if (behavior_is_same_state_action_request_locked(effective_state_id, action_def != NULL ? action_def->id : NULL)) {
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
        return ESP_OK;
    }

    if (s_ctx.current_state != NULL || s_ctx.current_action != NULL) {
        (void)behavior_executor_cancel_behavior_motion();
    }

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
    s_ctx.text_override_valid = (text != NULL);
    behavior_copy_string(s_ctx.text_override, sizeof(s_ctx.text_override), text);
    s_ctx.text_override_font_size = font_size;
    s_ctx.text_override_alert = alert_text;
    behavior_set_anim_override_locked(anim_id, suppress_anim);
    behavior_set_sound_override_locked(sound_id, suppress_sound);
    s_ctx.wait_for_local_sfx_completion = false;
    s_ctx.suppress_state_sound_events = behavior_apply_sound_override_locked(sound_id, suppress_sound);
    s_ctx.hold_logged = false;
    if (s_ctx.suppress_state_sound_events) {
        s_ctx.next_sound_index = s_ctx.current_state->sound_count;
    }
    behavior_log_action_start_locked(effective_state_id, action_def);
    behavior_dispatch_due_events_locked(now_ms, display_request);
    return ESP_OK;
}

static void behavior_task(void *arg) {
    behavior_display_request_t display_request;
    behavior_state_request_t state_request;

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
                esp_err_t request_ret = behavior_schedule_state_locked(
                    state_request.state_id, state_request.has_text ? state_request.text : NULL, state_request.font_size,
                    state_request.alert_text,
                    state_request.has_anim_override && state_request.anim_id[0] != '\0' ? state_request.anim_id : NULL,
                    state_request.suppress_anim,
                    state_request.has_sound_override && state_request.sound_id[0] != '\0' ? state_request.sound_id
                                                                                          : NULL,
                    state_request.suppress_sound, state_request.action_id[0] != '\0' ? state_request.action_id : NULL,
                    &display_request);
                if (request_ret == ESP_OK) {
                    ESP_LOGI(TAG, "Applied queued state request state=%s action=%s", state_request.state_id,
                             state_request.action_id[0] != '\0' ? state_request.action_id : "<none>");
                } else {
                    ESP_LOGW(TAG, "Queued state request failed state=%s err=%s", state_request.state_id,
                             esp_err_to_name(request_ret));
                }
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
                                                               NULL, false, NULL, &display_request);
                            if (fallback_ret != ESP_OK) {
                                ESP_LOGW(TAG, "Fallback state request failed state=%s err=%s",
                                         s_ctx.catalog.default_state, esp_err_to_name(fallback_ret));
                            }
                        }
                    }
                }
            }

            behavior_unlock();
        }

        behavior_apply_display_request(&display_request, "behavior task");
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BEHAVIOR_TICK_MS));
    }
}

esp_err_t behavior_state_init(void) {
    BaseType_t task_result;
    esp_err_t load_ret;

    if (s_ctx.initialized) {
        return ESP_OK;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGW(TAG, "Animation catalog init failed during behavior init");
    }
    if (sfx_service_init() != ESP_OK) {
        ESP_LOGW(TAG, "SFX service init failed during behavior init");
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

    load_ret = behavior_state_load();
    if (load_ret != ESP_OK && load_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Behavior state load failed: %s", esp_err_to_name(load_ret));
    }

    return ESP_OK;
}

esp_err_t behavior_state_load(void) {
    behavior_catalog_t catalog = {0};
    behavior_action_catalog_t action_catalog = {0};
    esp_err_t ret;
    esp_err_t action_ret;

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (anim_catalog_init() != 0) {
        ESP_LOGW(TAG, "Animation catalog init failed during behavior load");
    }
    sfx_service_reload();

    ret = behavior_load_catalog_from_file(&catalog);
    if (ret != ESP_OK) {
        if (behavior_lock()) {
            behavior_free_catalog(&s_ctx.catalog);
            behavior_free_action_catalog(&s_ctx.action_catalog);
            behavior_copy_string(s_ctx.catalog.version, sizeof(s_ctx.catalog.version), "1.0");
            behavior_copy_string(s_ctx.catalog.default_state, sizeof(s_ctx.catalog.default_state), "standby");
            behavior_reset_runtime_locked();
            behavior_unlock();
        }
        return ret;
    }

    action_ret = behavior_load_actions_from_dir(&action_catalog);
    if (action_ret != ESP_OK && action_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Behavior action load failed: %s", esp_err_to_name(action_ret));
        behavior_free_action_catalog(&action_catalog);
        memset(&action_catalog, 0, sizeof(action_catalog));
    }

    if (behavior_lock()) {
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

    ESP_LOGI(TAG, "Loaded behavior states v%s: count=%d default=%s actions=%d", s_ctx.catalog.version,
             s_ctx.catalog.state_count, s_ctx.catalog.default_state, s_ctx.action_catalog.action_count);
    return ESP_OK;
}

static esp_err_t behavior_state_set_with_resources_and_action_internal(const char *state_id, const char *text,
                                                                       int font_size, bool alert_text,
                                                                       const char *anim_id, const char *sound_id,
                                                                       const char *action_id) {
    behavior_state_request_t request;

    if (state_id == NULL || state_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (behavior_state_init() != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Queue state request state=%s action=%s text=%s anim_override=%s sound_override=%s font=%d alert=%d",
             state_id, action_id != NULL ? action_id : "<none>", text != NULL ? text : "<unchanged>",
             anim_id != NULL ? anim_id : "<default>", sound_id != NULL ? sound_id : "<default>", font_size,
             alert_text ? 1 : 0);

    behavior_fill_state_request(&request, state_id, text, font_size, alert_text, anim_id, sound_id, action_id);
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
                                                                 NULL);
}

esp_err_t behavior_state_set_with_resources(const char *state_id, const char *text, int font_size, const char *anim_id,
                                            const char *sound_id) {
    return behavior_state_set_with_resources_and_action(state_id, text, font_size, anim_id, sound_id, NULL);
}

esp_err_t behavior_state_set_with_resources_and_action(const char *state_id, const char *text, int font_size,
                                                       const char *anim_id, const char *sound_id,
                                                       const char *action_id) {
    return behavior_state_set_with_resources_and_action_internal(state_id, text, font_size, false, anim_id, sound_id,
                                                                 action_id);
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

const char *behavior_state_get_current(void) {
    if (!s_ctx.initialized) {
        return "standby";
    }
    return s_ctx.current_state_id[0] != '\0' ? s_ctx.current_state_id : s_ctx.catalog.default_state;
}

bool behavior_state_is_busy(void) {
    bool busy = false;
    static uint32_t s_last_busy_timeout_log_ms = 0;

    if (!s_ctx.initialized) {
        return false;
    }
    if (!behavior_lock_with_timeout(BEHAVIOR_QUERY_LOCK_TIMEOUT_MS)) {
        behavior_log_query_timeout_once("behavior_state_is_busy", &s_last_busy_timeout_log_ms);
        return true;
    }

    busy = sfx_service_is_busy() ||
           (s_ctx.current_state_id[0] != '\0' && strcmp(s_ctx.current_state_id, s_ctx.catalog.default_state) != 0);
    behavior_unlock();
    return busy;
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

bool behavior_state_is_action_active(void) {
    bool active = false;
    uint32_t elapsed_ms = 0;
    static uint32_t s_last_action_timeout_log_ms = 0;

    if (behavior_state_init() != ESP_OK) {
        return false;
    }
    if (!behavior_lock_with_timeout(BEHAVIOR_QUERY_LOCK_TIMEOUT_MS)) {
        behavior_log_query_timeout_once("behavior_state_is_action_active", &s_last_action_timeout_log_ms);
        return true;
    }

    if (s_ctx.current_action != NULL && s_ctx.current_action->total_duration_ms > 0) {
        elapsed_ms = behavior_now_ms() - s_ctx.action_started_ms;
        active = elapsed_ms < s_ctx.current_action->total_duration_ms;
    }

    behavior_unlock();
    return active;
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
