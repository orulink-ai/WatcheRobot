#include "sfx_service.h"

#include "sfx_schedule.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_audio.h"
#include "sdkconfig.h"
#include "sensecap-watcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mem_monitor_snapshot(const char *stage);

#define TAG "SFX_SERVICE"

#define SFX_MANIFEST_PATH "/spiffs/sfx/manifest.json"
#define SFX_DIR "/spiffs/sfx"
#ifdef CONFIG_WATCHER_SFX_TASK_STACK_SIZE
#define SFX_TASK_STACK CONFIG_WATCHER_SFX_TASK_STACK_SIZE
#else
#define SFX_TASK_STACK 4096
#endif
#define SFX_TASK_PRIORITY 7
#define SFX_POLL_INTERVAL_MS 20
#ifdef CONFIG_WATCHER_SFX_STREAM_CHUNK_SIZE
#define SFX_STREAM_CHUNK_SIZE CONFIG_WATCHER_SFX_STREAM_CHUNK_SIZE
#else
#define SFX_STREAM_CHUNK_SIZE 2048
#endif
#define SFX_PREFETCH_LIMIT_BYTES (256 * 1024)
#define SFX_CACHE_TOTAL_LIMIT_BYTES (2 * 1024 * 1024)
#define SFX_MAX_ID_LEN 32
#define SFX_MAX_PATH_LEN 128
#define SFX_MAX_MANIFEST_BYTES 8192
typedef struct {
    char id[SFX_MAX_ID_LEN];
    char path[SFX_MAX_PATH_LEN];
    uint8_t *audio_data;
    size_t audio_size;
    bool loaded_in_psram;
} sfx_manifest_entry_t;

typedef struct {
    sfx_manifest_entry_t *entries;
    int count;
} sfx_manifest_t;

typedef struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    bool initialized;
    bool cloud_audio_busy;
    bool local_busy;
    bool stop_requested;
    uint32_t request_generation;
    sfx_schedule_t schedule;
    sfx_manifest_t manifest;
} sfx_context_t;

static sfx_context_t s_ctx = {0};

static void sfx_copy_string(char *dst, size_t dst_size, const char *src) {
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

static void sfx_manifest_free(sfx_manifest_t *manifest) {
    int i;

    if (manifest == NULL) {
        return;
    }

    for (i = 0; i < manifest->count; ++i) {
        free(manifest->entries[i].audio_data);
        manifest->entries[i].audio_data = NULL;
        manifest->entries[i].audio_size = 0U;
        manifest->entries[i].loaded_in_psram = false;
    }
    free(manifest->entries);
    manifest->entries = NULL;
    manifest->count = 0;
}

static bool sfx_lock(void) {
    if (s_ctx.lock == NULL) {
        return false;
    }

    return xSemaphoreTake(s_ctx.lock, portMAX_DELAY) == pdTRUE;
}

static void sfx_unlock(void) {
    if (s_ctx.lock != NULL) {
        xSemaphoreGive(s_ctx.lock);
    }
}

static int64_t sfx_now_ms(void) {
    return esp_timer_get_time() / 1000LL;
}

static void sfx_bump_generation_locked(void) {
    s_ctx.request_generation++;
    if (s_ctx.request_generation == 0U) {
        s_ctx.request_generation = 1U;
    }
}

static void sfx_clear_schedule_locked(void) {
    sfx_schedule_clear(&s_ctx.schedule);
}

static void sfx_normalize_path(const char *raw_path, char *out_path, size_t out_size) {
    if (out_path == NULL || out_size == 0) {
        return;
    }

    out_path[0] = '\0';
    if (raw_path == NULL || raw_path[0] == '\0') {
        return;
    }

    if (strncmp(raw_path, "/spiffs/", 8) == 0) {
        sfx_copy_string(out_path, out_size, raw_path);
        return;
    }

    if (strncmp(raw_path, "sfx/", 4) == 0) {
        snprintf(out_path, out_size, "/spiffs/%s", raw_path);
        return;
    }

    snprintf(out_path, out_size, "%s/%s", SFX_DIR, raw_path);
}

static char *sfx_read_text_file(const char *path, size_t max_bytes) {
    FILE *file = NULL;
    char *buffer = NULL;
    long file_size = 0;
    size_t read_size = 0;

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

static esp_err_t sfx_manifest_load_locked(void) {
    cJSON *root = NULL;
    cJSON *sounds = NULL;
    cJSON *item = NULL;
    sfx_manifest_t manifest = {0};
    char *json = sfx_read_text_file(SFX_MANIFEST_PATH, SFX_MAX_MANIFEST_BYTES);

    sfx_manifest_free(&s_ctx.manifest);
    if (json == NULL) {
        ESP_LOGI(TAG, "No sfx manifest found at %s, using direct file lookup", SFX_MANIFEST_PATH);
        return ESP_OK;
    }

    root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "Failed to parse %s, using direct file lookup", SFX_MANIFEST_PATH);
        return ESP_FAIL;
    }

    sounds = cJSON_GetObjectItem(root, "sounds");
    if (sounds == NULL || !cJSON_IsObject(sounds)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON_ArrayForEach(item, sounds) {
        if (item->string != NULL) {
            manifest.count++;
        }
    }

    if (manifest.count > 0) {
        manifest.entries = (sfx_manifest_entry_t *)calloc((size_t)manifest.count, sizeof(sfx_manifest_entry_t));
        if (manifest.entries == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
    }

    manifest.count = 0;
    cJSON_ArrayForEach(item, sounds) {
        const char *path_value = NULL;

        if (item->string == NULL) {
            continue;
        }

        if (cJSON_IsString(item)) {
            path_value = item->valuestring;
        } else if (cJSON_IsObject(item)) {
            cJSON *path_item = cJSON_GetObjectItem(item, "path");
            if (path_item != NULL && cJSON_IsString(path_item)) {
                path_value = path_item->valuestring;
            }
        }

        if (path_value == NULL || path_value[0] == '\0') {
            continue;
        }

        sfx_copy_string(manifest.entries[manifest.count].id, sizeof(manifest.entries[manifest.count].id), item->string);
        sfx_normalize_path(path_value, manifest.entries[manifest.count].path,
                           sizeof(manifest.entries[manifest.count].path));
        manifest.count++;
    }

    s_ctx.manifest = manifest;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d sfx manifest entries", s_ctx.manifest.count);
    return ESP_OK;
}

static void sfx_resolve_path_locked(const char *sound_id, char *out_path, size_t out_size) {
    int i;

    for (i = 0; i < s_ctx.manifest.count; ++i) {
        if (strcmp(s_ctx.manifest.entries[i].id, sound_id) == 0) {
            sfx_copy_string(out_path, out_size, s_ctx.manifest.entries[i].path);
            return;
        }
    }

    snprintf(out_path, out_size, "%s/%s.pcm", SFX_DIR, sound_id);
}

static bool sfx_find_cached_audio_locked(const char *sound_id, const uint8_t **audio_data, size_t *audio_size,
                                         bool *loaded_in_psram) {
    int i;

    if (audio_data == NULL || audio_size == NULL || loaded_in_psram == NULL) {
        return false;
    }

    *audio_data = NULL;
    *audio_size = 0U;
    *loaded_in_psram = false;
    for (i = 0; i < s_ctx.manifest.count; ++i) {
        sfx_manifest_entry_t *entry = &s_ctx.manifest.entries[i];

        if (strcmp(entry->id, sound_id) != 0 || entry->audio_data == NULL || entry->audio_size == 0U) {
            continue;
        }
        *audio_data = entry->audio_data;
        *audio_size = entry->audio_size;
        *loaded_in_psram = entry->loaded_in_psram;
        return true;
    }
    return false;
}

static bool sfx_playback_should_abort(uint32_t expected_generation) {
    bool abort = true;

    if (!sfx_lock()) {
        return true;
    }

    abort = s_ctx.stop_requested || s_ctx.cloud_audio_busy || s_ctx.request_generation != expected_generation ||
            sfx_schedule_has_due(&s_ctx.schedule, sfx_now_ms());
    sfx_unlock();
    return abort;
}

static void sfx_set_local_busy(bool busy) {
    if (!sfx_lock()) {
        return;
    }

    s_ctx.local_busy = busy;
    if (!busy) {
        s_ctx.stop_requested = false;
    }
    sfx_unlock();
}

static esp_err_t sfx_load_audio_blob(const char *sound_id, const char *sound_path, uint8_t **audio_data,
                                     size_t *audio_size, bool *loaded_in_psram) {
    FILE *file = NULL;
    long file_size = 0;
    uint8_t *buffer = NULL;
    size_t read_size = 0;
    bool in_psram = true;

    if (audio_data == NULL || audio_size == NULL || loaded_in_psram == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *audio_data = NULL;
    *audio_size = 0U;
    *loaded_in_psram = false;

    file = fopen(sound_path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        return ESP_FAIL;
    }

    if ((size_t)file_size > SFX_PREFETCH_LIMIT_BYTES) {
        fclose(file);
        ESP_LOGW(TAG, "Local sfx '%s' is %u bytes, exceeding prefetch limit %u, keeping streaming fallback", sound_id,
                 (unsigned int)file_size, (unsigned int)SFX_PREFETCH_LIMIT_BYTES);
        return ESP_ERR_NOT_SUPPORTED;
    }

    buffer = (uint8_t *)heap_caps_malloc((size_t)file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        in_psram = false;
        buffer = (uint8_t *)heap_caps_malloc((size_t)file_size, MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(buffer);
        return ESP_FAIL;
    }

    *audio_data = buffer;
    *audio_size = (size_t)file_size;
    *loaded_in_psram = in_psram;
    return ESP_OK;
}

static void sfx_cache_manifest_audio_locked(void) {
    size_t total_cached = 0U;
    int cached_count = 0;
    int i;

    for (i = 0; i < s_ctx.manifest.count; ++i) {
        sfx_manifest_entry_t *entry = &s_ctx.manifest.entries[i];
        uint8_t *audio_data = NULL;
        size_t audio_size = 0U;
        bool loaded_in_psram = false;
        esp_err_t ret;

        if (entry->audio_data != NULL || entry->path[0] == '\0') {
            continue;
        }

        ret = sfx_load_audio_blob(entry->id, entry->path, &audio_data, &audio_size, &loaded_in_psram);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SFX cache skipped '%s': %s", entry->id, esp_err_to_name(ret));
            continue;
        }
        if (total_cached + audio_size > SFX_CACHE_TOTAL_LIMIT_BYTES) {
            ESP_LOGW(TAG, "SFX cache budget reached, skipping '%s' (%u bytes)", entry->id, (unsigned int)audio_size);
            free(audio_data);
            continue;
        }

        entry->audio_data = audio_data;
        entry->audio_size = audio_size;
        entry->loaded_in_psram = loaded_in_psram;
        total_cached += audio_size;
        cached_count++;
    }

    ESP_LOGI(TAG, "Cached %d local sfx entries (%u bytes)", cached_count, (unsigned int)total_cached);
}

static bool sfx_playback_buffered(const char *sound_id, uint32_t generation, const uint8_t *audio_data,
                                  size_t audio_size) {
    uint8_t staging[SFX_STREAM_CHUNK_SIZE];
    size_t offset = 0U;

    while (offset < audio_size) {
        size_t remaining = audio_size - offset;
        size_t chunk_size = remaining > sizeof(staging) ? sizeof(staging) : remaining;
        int written = 0;

        if (sfx_playback_should_abort(generation)) {
            ESP_LOGI(TAG, "Stopping local sfx '%s' due to new audio request", sound_id);
            return false;
        }

        memcpy(staging, audio_data + offset, chunk_size);
        written = hal_audio_write(staging, (int)chunk_size);
        if (written != (int)chunk_size) {
            ESP_LOGW(TAG, "Incomplete buffered sfx playback '%s': %d/%u", sound_id, written, (unsigned int)chunk_size);
            return false;
        }

        offset += chunk_size;
    }

    return true;
}

static bool sfx_playback_streaming(const char *sound_id, uint32_t generation, FILE *file) {
    uint8_t buffer[SFX_STREAM_CHUNK_SIZE];

    while (!feof(file)) {
        size_t read_size = 0U;
        int written = 0;

        if (sfx_playback_should_abort(generation)) {
            ESP_LOGI(TAG, "Stopping local sfx '%s' due to new audio request", sound_id);
            return false;
        }

        read_size = fread(buffer, 1, sizeof(buffer), file);
        if (read_size == 0U) {
            break;
        }

        written = hal_audio_write(buffer, (int)read_size);
        if (written != (int)read_size) {
            ESP_LOGW(TAG, "Incomplete streaming sfx playback '%s': %d/%u", sound_id, written, (unsigned int)read_size);
            return false;
        }
    }

    return true;
}

static bool sfx_is_cloud_audio_busy(void) {
    bool busy = false;

    if (!s_ctx.initialized || !sfx_lock()) {
        return false;
    }

    busy = s_ctx.cloud_audio_busy;
    sfx_unlock();
    return busy;
}

static void sfx_playback_file(const char *sound_id, uint32_t generation) {
    FILE *file = NULL;
    uint8_t *audio_data = NULL;
    const uint8_t *playback_audio_data = NULL;
    size_t audio_size = 0U;
    char sound_path[SFX_MAX_PATH_LEN];
    bool audio_started = false;
    bool loaded_in_psram = false;
    bool use_prefetched_audio = false;
    bool owns_audio_data = false;
    bool handoff_to_cloud_audio = false;
    esp_err_t preload_ret = ESP_OK;

    if (!sfx_lock()) {
        return;
    }
    sfx_resolve_path_locked(sound_id, sound_path, sizeof(sound_path));
    if (sfx_find_cached_audio_locked(sound_id, &playback_audio_data, &audio_size, &loaded_in_psram)) {
        use_prefetched_audio = true;
    }
    sfx_unlock();

    if (!use_prefetched_audio) {
        preload_ret = sfx_load_audio_blob(sound_id, sound_path, &audio_data, &audio_size, &loaded_in_psram);
        if (preload_ret == ESP_OK) {
            playback_audio_data = audio_data;
            use_prefetched_audio = true;
            owns_audio_data = true;
        }
    }
    if (!use_prefetched_audio && (preload_ret == ESP_ERR_NOT_SUPPORTED || preload_ret == ESP_ERR_NO_MEM)) {
        file = fopen(sound_path, "rb");
        if (file == NULL) {
            ESP_LOGI(TAG, "Skip local sfx '%s': file not found (%s)", sound_id, sound_path);
            sfx_set_local_busy(false);
            return;
        }
        ESP_LOGW(TAG, "Playing local sfx '%s' with streaming fallback (reason=%s)", sound_id,
                 preload_ret == ESP_ERR_NO_MEM ? "no_mem" : "too_large");
    } else if (!use_prefetched_audio) {
        ESP_LOGW(TAG, "Failed to preload local sfx '%s' from %s: %s", sound_id, sound_path,
                 esp_err_to_name(preload_ret));
        sfx_set_local_busy(false);
        return;
    }

    if (sfx_playback_should_abort(generation)) {
        ESP_LOGI(TAG, "Skip local sfx '%s': request superseded before playback started", sound_id);
        if (file != NULL) {
            fclose(file);
        }
        free(audio_data);
        sfx_set_local_busy(false);
        return;
    }

    hal_audio_set_playback_mode(true);
    hal_audio_set_sample_rate(24000);
    if (hal_audio_start() != 0) {
        ESP_LOGW(TAG, "Failed to start audio for '%s'", sound_id);
        hal_audio_set_playback_mode(false);
        hal_audio_set_sample_rate(16000);
        if (file != NULL) {
            fclose(file);
        }
        free(audio_data);
        sfx_set_local_busy(false);
        return;
    }

    audio_started = true;
    if (use_prefetched_audio) {
        ESP_LOGI(TAG, "Playing local sfx '%s' from %s via %s prefetch (%u bytes, chunk=%u)", sound_id, sound_path,
                 loaded_in_psram ? "psram" : "heap", (unsigned int)audio_size, (unsigned int)SFX_STREAM_CHUNK_SIZE);
        sfx_playback_buffered(sound_id, generation, playback_audio_data, audio_size);
    } else {
        ESP_LOGI(TAG, "Playing local sfx '%s' from %s via streaming fallback (chunk=%u)", sound_id, sound_path,
                 (unsigned int)SFX_STREAM_CHUNK_SIZE);
        sfx_playback_streaming(sound_id, generation, file);
    }

    if (file != NULL) {
        fclose(file);
    }
    if (owns_audio_data) {
        free(audio_data);
    }
    if (audio_started) {
        handoff_to_cloud_audio = sfx_is_cloud_audio_busy();
        if (handoff_to_cloud_audio) {
            ESP_LOGI(TAG, "Handing off audio path from local sfx '%s' to cloud audio", sound_id);
        } else {
#ifdef CONFIG_ENABLE_WAKE_WORD
            hal_audio_set_playback_mode(false);
#endif
            hal_audio_stop();
        }
    }
    mem_monitor_snapshot("after_sfx_playback");
    sfx_set_local_busy(false);
}

static bool sfx_take_scheduled_request(char *sound_id, size_t sound_id_size, uint32_t *generation) {
    bool has_request = false;
    int64_t now_ms;
    int64_t late_ms;

    if (!sfx_lock()) {
        return false;
    }

    now_ms = sfx_now_ms();
    if (!s_ctx.cloud_audio_busy) {
        has_request = sfx_schedule_take_due(&s_ctx.schedule, now_ms, sound_id, sound_id_size, &late_ms);
    }
    if (has_request) {
        /* Keep the service busy across task handoff so callers do not think playback finished
         * between dequeuing the request and actually starting the speaker stream. */
        s_ctx.local_busy = true;
        sfx_bump_generation_locked();
        *generation = s_ctx.request_generation;
        ESP_LOGI(TAG, "Dequeued local sfx '%s' late=%lldms", sound_id, (long long)late_ms);
    }

    sfx_unlock();
    return has_request;
}

static void sfx_task(void *arg) {
    char sound_id[SFX_MAX_ID_LEN];
    uint32_t generation = 0;

    (void)arg;

    while (true) {
        if (sfx_take_scheduled_request(sound_id, sizeof(sound_id), &generation)) {
            sfx_playback_file(sound_id, generation);
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SFX_POLL_INTERVAL_MS));
    }
}

esp_err_t sfx_service_init(void) {
    BaseType_t task_result;

    if (s_ctx.initialized) {
        return ESP_OK;
    }

    if (bsp_spiffs_init_default() != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed during sfx init");
    }

    s_ctx.lock = xSemaphoreCreateMutex();
    if (s_ctx.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (!sfx_lock()) {
        return ESP_FAIL;
    }
    sfx_manifest_load_locked();
    sfx_cache_manifest_audio_locked();
    sfx_unlock();

    task_result = xTaskCreate(sfx_task, "sfx_task", SFX_TASK_STACK, NULL, SFX_TASK_PRIORITY, &s_ctx.task);
    if (task_result != pdPASS) {
        vSemaphoreDelete(s_ctx.lock);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }

    s_ctx.initialized = true;
    return ESP_OK;
}

esp_err_t sfx_service_reload(void) {
    if (sfx_service_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (bsp_spiffs_init_default() != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed during sfx reload");
    }

    if (!sfx_lock()) {
        return ESP_FAIL;
    }

    sfx_manifest_load_locked();
    sfx_cache_manifest_audio_locked();
    sfx_unlock();
    return ESP_OK;
}

esp_err_t sfx_service_play(const char *sound_id) {
    return sfx_service_play_delayed(sound_id, 0);
}

esp_err_t sfx_service_play_delayed(const char *sound_id, int delay_ms) {
    int safe_delay_ms;
    char replaced_sound_id[SFX_MAX_ID_LEN];

    if (sound_id == NULL || sound_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (sfx_service_init() != ESP_OK) {
        return ESP_FAIL;
    }

    if (!sfx_lock()) {
        return ESP_FAIL;
    }

    if (s_ctx.cloud_audio_busy) {
        sfx_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    safe_delay_ms = delay_ms > 0 ? delay_ms : 0;
    s_ctx.stop_requested = false;
    if (!sfx_schedule_enqueue(&s_ctx.schedule, sound_id, sfx_now_ms() + (int64_t)safe_delay_ms, replaced_sound_id,
                              sizeof(replaced_sound_id))) {
        sfx_unlock();
        return ESP_FAIL;
    }
    if (replaced_sound_id[0] != '\0') {
        ESP_LOGW(TAG, "SFX schedule full, replacing latest request '%s'", replaced_sound_id);
    }
    ESP_LOGI(TAG, "Scheduled local sfx '%s' delay=%dms", sound_id, safe_delay_ms);
    sfx_unlock();
    return ESP_OK;
}

void sfx_service_stop(void) {
    if (!s_ctx.initialized || !sfx_lock()) {
        return;
    }

    s_ctx.stop_requested = true;
    sfx_clear_schedule_locked();
    sfx_bump_generation_locked();
    sfx_unlock();
}

bool sfx_service_is_busy(void) {
    bool busy = false;

    if (!s_ctx.initialized || !sfx_lock()) {
        return false;
    }

    busy = s_ctx.local_busy || sfx_schedule_has_active(&s_ctx.schedule);
    sfx_unlock();
    return busy;
}

void sfx_service_set_cloud_audio_busy(bool busy) {
    if (sfx_service_init() != ESP_OK) {
        return;
    }

    if (!sfx_lock()) {
        return;
    }

    s_ctx.cloud_audio_busy = busy;
    if (busy) {
        s_ctx.stop_requested = true;
        sfx_clear_schedule_locked();
        sfx_bump_generation_locked();
    }
    sfx_unlock();
}

bool sfx_service_is_cloud_audio_busy(void) {
    bool busy = false;

    if (!s_ctx.initialized || !sfx_lock()) {
        return false;
    }

    busy = s_ctx.cloud_audio_busy;
    sfx_unlock();
    return busy;
}
