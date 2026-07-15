#include "app_center.h"

#include "app_center_exit_policy.h"
#include "app_center_firmware_sources.h"
#include "boot_anim.h"
#include "cJSON.h"
#include "factory_home_ui/ui.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_attr.h"
#include "esp_flash.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "ota_service.h"
#include "sensecap-watcher.h"
#include "voice_service.h"
#include "watcher_app_runtime.h"
#include "wifi_manager.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef CONFIG_APP_CENTER_REMOTE_LIST_URL
#define APP_CENTER_REMOTE_LIST_URL CONFIG_APP_CENTER_REMOTE_LIST_URL
#else
#define APP_CENTER_REMOTE_LIST_URL ""
#endif

#ifdef CONFIG_APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES
#define APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES 1
#else
#define APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES 0
#endif

#ifdef CONFIG_APP_CENTER_TRUSTED_SIGNATURE_ISSUERS
#define APP_CENTER_TRUSTED_SIGNATURE_ISSUERS CONFIG_APP_CENTER_TRUSTED_SIGNATURE_ISSUERS
#else
#define APP_CENTER_TRUSTED_SIGNATURE_ISSUERS ""
#endif

#ifdef CONFIG_APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256
#define APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256 CONFIG_APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256
#else
#define APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256 ""
#endif

#define TAG "APP_CENTER"
#define APP_CENTER_WAIT_DELAY_MS 50
#define APP_CENTER_CLOSE_WAIT_MS 1500
#define APP_CENTER_VOICE_BUTTON_SUPPRESS_MS 750
#define APP_CENTER_MAX_ENTRIES 8
#define APP_CENTER_TEXT_MAX 160
#define APP_CENTER_URL_MAX 256
#define APP_CENTER_HTTP_TIMEOUT_MS 8000
#define APP_CENTER_REMOTE_JSON_MAX_BYTES 8192
#define APP_CENTER_FIRMWARE_MIRROR_MAX 3
#define APP_CENTER_REMOTE_RETRY_FIRST_MS 2000
#define APP_CENTER_REMOTE_RETRY_SECOND_MS 5000
#define APP_CENTER_REMOTE_RETRY_MAX_MS 10000
#define APP_CENTER_CARD_Y 118
#define APP_CENTER_CARD_W 206
#define APP_CENTER_CARD_H 96
#define APP_CENTER_CARD_GAP 16
#define APP_CENTER_CARD_TRAVEL 112
#define APP_CENTER_ANIM_MS 190
#define APP_CENTER_EXIT_AUTO_HIDE_MS 5000
#define APP_CENTER_STYLE_GREEN 0x95F500
#define APP_CENTER_STYLE_AMBER 0xE1B94B
#define APP_CENTER_STYLE_RED 0xD54941
#define APP_CENTER_STYLE_WHITE 0xFFFFFF
#define APP_CENTER_STYLE_PANEL 0x30343A
#define APP_CENTER_STYLE_MUTED 0xB4C8CA
#define APP_CENTER_STYLE_DIM 0x7EA0A4
#define APP_CENTER_LIST_TITLE_Y 36
#define APP_CENTER_LIST_STATUS_Y 68
#define APP_CENTER_LIST_STATUS_W 236
#define APP_CENTER_CARD_TEXT_X 16
#define APP_CENTER_CARD_TEXT_W 174
#define APP_CENTER_CARD_NAME_Y 17
#define APP_CENTER_CARD_STATE_Y 55
#define APP_CENTER_DETAIL_TEXT_X 24
#define APP_CENTER_DETAIL_TEXT_W 188
#define APP_CENTER_DETAIL_NAME_Y 16
#define APP_CENTER_DETAIL_STATE_Y 48
#define APP_CENTER_DETAIL_BODY_Y 76
#define APP_CENTER_DETAIL_BODY_H 42
#define APP_CENTER_DETAIL_META_Y 120
#define APP_CENTER_STATUS_PANEL_W 400
#define APP_CENTER_STATUS_PANEL_H 300
#define APP_CENTER_STATUS_PANEL_Y -50
#define APP_CENTER_STATUS_TEXT_W 350
#define APP_CENTER_STATUS_BUTTON_W 184
#define APP_CENTER_STATUS_BUTTON_H 42
#define APP_CENTER_STATUS_BUTTON_Y 116
#define APP_CENTER_STATUS_PROGRESS_W 184
#define APP_CENTER_STATUS_PROGRESS_H 12
#define APP_CENTER_STATUS_PROGRESS_Y 64
#define APP_CENTER_STATUS_PROGRESS_LABEL_Y 88
#define APP_CENTER_STATUS_ICON_Y -100
#define APP_CENTER_STATUS_ICON_ZOOM 140
#define APP_CENTER_STATUS_TITLE_Y -17
#define APP_CENTER_STATUS_ACCENT_Y 16
#define APP_CENTER_STATUS_BODY_Y 44
#define APP_CENTER_LEGACY_ESPNOW_PERMISSION "esp-now"
#define APP_CENTER_LEGACY_ESPNOW_NAME_KEY "espnowremote"
#define APP_CENTER_NVS_NAMESPACE "app_center"
#define APP_CENTER_INSTALLED_KEY_PREFIX "app"
#define APP_CENTER_SOURCE_KEY_PREFIX "src"
#define APP_CENTER_NVS_KEY_MAX 15
#define APP_CENTER_PACKAGE_DIR "/spiffs/app_center"
#define APP_CENTER_PACKAGE_PATH_MAX 96
#define APP_CENTER_PACKAGE_READ_CHUNK 1024
#define APP_CENTER_PACKAGE_MANIFEST_MAX_BYTES 4096
#define APP_CENTER_PACKAGE_MAX_BYTES (64 * 1024)
#define APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES (0x400000U)
#define APP_CENTER_SPIFFS_LABEL "storage"
#define APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES (8 * 1024)
#define APP_CENTER_MANAGER_SNAPSHOT_TASK_STACK 8192
#define APP_CENTER_MANAGER_SNAPSHOT_TASK_PRIORITY 4

typedef enum {
    APP_CENTER_ENTRY_PACKAGE = 0,
    APP_CENTER_ENTRY_FIRMWARE_APP,
} app_center_entry_type_t;

typedef enum {
    APP_CENTER_PAGE_LIST = 0,
    APP_CENTER_PAGE_DETAIL,
    APP_CENTER_PAGE_INSTALL,
} app_center_page_t;

typedef enum {
    APP_CENTER_DETAIL_ACTION_PRIMARY = 0,
    APP_CENTER_DETAIL_ACTION_SECONDARY,
} app_center_detail_action_t;

typedef struct {
    char id[48];
    char name[64];
    char description[APP_CENTER_TEXT_MAX];
    char icon_url[APP_CENTER_URL_MAX];
    char package_url[APP_CENTER_URL_MAX];
    char firmware_url[APP_CENTER_URL_MAX];
    char firmware_mirrors[APP_CENTER_FIRMWARE_MIRROR_MAX][APP_CENTER_URL_MAX];
    char firmware_sha256[65];
    char image_version[32];
    char signature_algorithm[32];
    char min_launcher_version[32];
    char version[32];
    size_t firmware_size_bytes;
    size_t compat_ota_slot_size;
    int compat_flash_size_mb;
    int firmware_mirror_count;
    app_center_entry_type_t type;
    bool installed;
} app_center_entry_t;

typedef struct {
    char *buffer;
    int length;
    int capacity;
} app_center_http_buffer_t;

typedef struct {
    int index;
    uint32_t generation;
} app_center_download_task_arg_t;

/* Catalog metadata is large, cold data and never participates in DMA/ISR
 * paths. Keep it out of scarce internal DRAM even in App.Center-enabled
 * builds. Product builds disable App.Center entirely. */
EXT_RAM_BSS_ATTR static app_center_entry_t s_entries[APP_CENTER_MAX_ENTRIES];
static app_center_deps_t s_deps = {0};
static int s_entry_count = 0;
static int s_selected_index = 0;
static int s_action_index = 0;
static app_center_detail_action_t s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
static volatile app_center_selection_t s_selection = APP_CENTER_SELECTION_NONE;
static volatile bool s_render_requested = false;
static volatile bool s_pending_download_open = false;
static volatile bool s_remote_fetch_started = false;
static volatile bool s_remote_fetch_running = false;
static volatile bool s_download_running = false;
static volatile bool s_download_finished = false;
static volatile bool s_download_cancel_requested = false;
static volatile bool s_return_launcher_requested = false;
static volatile bool s_app_center_active = false;
static volatile uint32_t s_app_center_generation = 0;
static volatile int s_download_progress = 0;
static volatile esp_err_t s_download_result = ESP_OK;
static char s_remote_status[96] = "Remote list not loaded";
static char s_download_status[96] = "Ready to download";
static char s_launch_app_name[64] = "Downloaded App";
static char s_launch_app_description[APP_CENTER_TEXT_MAX] = "";
static char s_launch_app_package_path[APP_CENTER_PACKAGE_PATH_MAX] = "";
static app_center_page_t s_page = APP_CENTER_PAGE_LIST;
static FILE *s_ws_package_file = NULL;
static char s_ws_package_command_id[48] = "";
static char s_ws_package_id[48] = "";
static char s_ws_package_name[64] = "";
static char s_ws_package_version[32] = "";
static char s_ws_package_description[APP_CENTER_TEXT_MAX] = "";
static char s_ws_package_temp_path[APP_CENTER_PACKAGE_PATH_MAX] = "";
static size_t s_ws_package_expected_size = 0;
static size_t s_ws_package_received_size = 0;
static int s_ws_package_last_reported_percent = -1;
static lv_obj_t *s_screen = NULL;
static lv_group_t *s_group = NULL;
static lv_obj_t *s_status_panel = NULL;
static lv_obj_t *s_status_icon = NULL;
static lv_obj_t *s_status_title_label = NULL;
static lv_obj_t *s_status_body_label = NULL;
static lv_obj_t *s_install_status_label = NULL;
static lv_obj_t *s_install_progress_bar = NULL;
static lv_obj_t *s_install_progress_label = NULL;
static lv_obj_t *s_list_title_label = NULL;
static lv_obj_t *s_list_status_label = NULL;
static lv_obj_t *s_list_card_name_label = NULL;
static lv_obj_t *s_list_card_state_label = NULL;
static lv_obj_t *s_detail_panel = NULL;
static lv_obj_t *s_detail_name_label = NULL;
static lv_obj_t *s_detail_state_label = NULL;
static lv_obj_t *s_detail_body_label = NULL;
static lv_obj_t *s_detail_meta_label = NULL;
static lv_obj_t *s_detail_primary_button = NULL;
static lv_obj_t *s_detail_secondary_button = NULL;
static lv_obj_t *s_download_cancel_button = NULL;
static lv_obj_t *s_focus_proxies[APP_CENTER_MAX_ENTRIES] = {0};
static lv_obj_t *s_entry_cards[APP_CENTER_MAX_ENTRIES] = {0};
static lv_obj_t *s_exit_button = NULL;
static TaskHandle_t s_remote_fetch_task = NULL;
static TaskHandle_t s_download_task = NULL;
static TaskHandle_t s_manager_snapshot_task = NULL;
static SemaphoreHandle_t s_http_client_lock = NULL;
static SemaphoreHandle_t s_manager_snapshot_lock = NULL;
static esp_http_client_handle_t s_remote_fetch_client = NULL;
static esp_http_client_handle_t s_download_client = NULL;
EXT_RAM_BSS_ATTR static app_center_manager_snapshot_t s_manager_snapshot_cache = {0};
static TickType_t s_remote_retry_after_tick = 0;
static TickType_t s_button_ignore_until_tick = 0;
static TickType_t s_exit_hide_at_tick = 0;
static uint8_t s_remote_fetch_failure_count = 0;
static bool s_manager_snapshot_cache_valid = false;
static bool s_manager_snapshot_refresh_pending = false;
static bool s_manager_snapshot_refresh_in_flight = false;
static bool s_list_animating = false;
static int s_previous_index = -1;
static int s_transition_dir = 0;
static int s_uninstall_confirm_index = -1;
static volatile bool s_wifi_gate_active = false;

static void app_center_update_list_selection_locked(void);
static void app_center_focus_obj_locked(lv_obj_t *obj);
static void app_center_select_relative(int direction);
static void app_center_request_download(int index);
static void app_center_cancel_http_client(esp_http_client_handle_t *slot, const char *label);
static bool app_center_wait_background_idle(uint32_t timeout_ms);
static void app_center_ensure_package_entries_loaded(void);
static void app_center_invalidate_manager_snapshot(void);
static int app_center_find_entry_by_id(const char *id);
static int app_center_upsert_package_entry(const char *id, const char *name, const char *version,
                                           const char *description, bool installed);
static int app_center_upsert_firmware_entry(const app_center_entry_t *source, bool installed);
static const char *app_center_json_string(cJSON *object, const char *key);
static cJSON *json_get_object(cJSON *object, const char *key);
static size_t json_get_size_t(cJSON *object, const char *key_a, const char *key_b, const char *key_c);

static void app_center_copy_deps(const app_center_deps_t *deps) {
    if (deps == NULL) {
        memset(&s_deps, 0, sizeof(s_deps));
        return;
    }
    s_deps = *deps;
}

void app_center_configure(const app_center_deps_t *deps) {
    app_center_copy_deps(deps);
}

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (src != NULL) {
        snprintf(dst, dst_size, "%s", src);
    }
}

static uint32_t app_center_next_generation(void) {
    s_app_center_generation++;
    if (s_app_center_generation == 0) {
        s_app_center_generation = 1;
    }
    return s_app_center_generation;
}

static void app_center_ensure_http_client_lock(void) {
    if (s_http_client_lock == NULL) {
        s_http_client_lock = xSemaphoreCreateMutex();
        if (s_http_client_lock == NULL) {
            ESP_LOGW(TAG, "Failed to create App.Center HTTP client lock");
        }
    }
}

static void app_center_set_http_client(esp_http_client_handle_t *slot, esp_http_client_handle_t client) {
    if (slot == NULL) {
        return;
    }

    app_center_ensure_http_client_lock();
    if (s_http_client_lock != NULL) {
        xSemaphoreTake(s_http_client_lock, portMAX_DELAY);
    }
    *slot = client;
    if (s_http_client_lock != NULL) {
        xSemaphoreGive(s_http_client_lock);
    }
}

static void app_center_clear_http_client(esp_http_client_handle_t *slot, esp_http_client_handle_t client) {
    if (slot == NULL) {
        return;
    }

    app_center_ensure_http_client_lock();
    if (s_http_client_lock != NULL) {
        xSemaphoreTake(s_http_client_lock, portMAX_DELAY);
    }
    if (*slot == client) {
        *slot = NULL;
    }
    if (s_http_client_lock != NULL) {
        xSemaphoreGive(s_http_client_lock);
    }
}

static void app_center_cancel_http_client(esp_http_client_handle_t *slot, const char *label) {
    if (slot == NULL) {
        return;
    }

    app_center_ensure_http_client_lock();
    if (s_http_client_lock != NULL) {
        xSemaphoreTake(s_http_client_lock, portMAX_DELAY);
    }
    esp_http_client_handle_t client = *slot;
    if (client != NULL) {
        esp_err_t ret = esp_http_client_cancel_request(client);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to cancel App.Center HTTP client (%s): %s", label != NULL ? label : "unknown",
                     esp_err_to_name(ret));
        }
    }
    if (s_http_client_lock != NULL) {
        xSemaphoreGive(s_http_client_lock);
    }
}

static bool app_center_background_work_idle(void) {
    return !s_remote_fetch_running && !s_download_running && s_remote_fetch_task == NULL && s_download_task == NULL;
}

static bool app_center_wait_background_idle(uint32_t timeout_ms) {
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (!app_center_background_work_idle()) {
        if (xTaskGetTickCount() >= deadline) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_CENTER_WAIT_DELAY_MS));
    }
    return true;
}

static bool app_center_generation_is_active(uint32_t generation) {
    return s_app_center_active && generation == s_app_center_generation;
}

static void app_center_request_render_for_generation(uint32_t generation) {
    if (app_center_generation_is_active(generation)) {
        s_render_requested = true;
    }
}

static bool app_center_set_remote_status(const char *status) {
    if (status == NULL || strcmp(s_remote_status, status) == 0) {
        return false;
    }

    safe_copy(s_remote_status, sizeof(s_remote_status), status);
    s_render_requested = true;
    return true;
}

static void app_center_ensure_manager_snapshot_lock(void) {
    if (s_manager_snapshot_lock == NULL) {
        s_manager_snapshot_lock = xSemaphoreCreateMutex();
        if (s_manager_snapshot_lock == NULL) {
            ESP_LOGW(TAG, "Failed to create App.Center manager snapshot lock");
        }
    }
}

static bool app_center_take_manager_snapshot_lock(void) {
    app_center_ensure_manager_snapshot_lock();
    return s_manager_snapshot_lock != NULL && xSemaphoreTake(s_manager_snapshot_lock, portMAX_DELAY) == pdTRUE;
}

static void app_center_give_manager_snapshot_lock(void) {
    if (s_manager_snapshot_lock != NULL) {
        xSemaphoreGive(s_manager_snapshot_lock);
    }
}

static void app_center_store_manager_snapshot_cache(const app_center_manager_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    if (!app_center_take_manager_snapshot_lock()) {
        return;
    }
    s_manager_snapshot_cache = *snapshot;
    s_manager_snapshot_cache_valid = true;
    app_center_give_manager_snapshot_lock();
}

esp_err_t app_center_get_cached_manager_snapshot(app_center_manager_snapshot_t *out_snapshot) {
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));

    if (app_center_take_manager_snapshot_lock()) {
        if (s_manager_snapshot_cache_valid) {
            *out_snapshot = s_manager_snapshot_cache;
            ret = ESP_OK;
        }
        app_center_give_manager_snapshot_lock();
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    if (ret != ESP_OK) {
        app_center_request_manager_snapshot_refresh();
    }
    return ret;
}

static void app_center_manager_snapshot_refresh_task(void *arg) {
    (void)arg;
    while (true) {
        app_center_manager_snapshot_t snapshot = {0};
        esp_err_t ret;
        bool run_again = false;

        if (app_center_take_manager_snapshot_lock()) {
            s_manager_snapshot_refresh_pending = false;
            app_center_give_manager_snapshot_lock();
        }

        ret = app_center_get_manager_snapshot(&snapshot);
        if (ret == ESP_OK) {
            app_center_store_manager_snapshot_cache(&snapshot);
        } else {
            ESP_LOGW(TAG, "App.Center manager snapshot refresh failed: %s", esp_err_to_name(ret));
        }

        if (app_center_take_manager_snapshot_lock()) {
            run_again = s_manager_snapshot_refresh_pending;
            if (!run_again) {
                s_manager_snapshot_refresh_in_flight = false;
                s_manager_snapshot_task = NULL;
            }
            app_center_give_manager_snapshot_lock();
        }
        if (!run_again) {
            break;
        }
    }
    vTaskDelete(NULL);
}

void app_center_request_manager_snapshot_refresh(void) {
    bool create_task = false;

    if (!app_center_take_manager_snapshot_lock()) {
        return;
    }
    s_manager_snapshot_refresh_pending = true;
    if (!s_manager_snapshot_refresh_in_flight) {
        s_manager_snapshot_refresh_in_flight = true;
        create_task = true;
    }
    app_center_give_manager_snapshot_lock();

    if (create_task) {
        TaskHandle_t task = NULL;
        BaseType_t created =
            xTaskCreate(app_center_manager_snapshot_refresh_task, "app_mgr_snapshot",
                        APP_CENTER_MANAGER_SNAPSHOT_TASK_STACK, NULL, APP_CENTER_MANAGER_SNAPSHOT_TASK_PRIORITY, &task);
        if (created == pdPASS) {
            if (app_center_take_manager_snapshot_lock()) {
                if (s_manager_snapshot_refresh_in_flight) {
                    s_manager_snapshot_task = task;
                }
                app_center_give_manager_snapshot_lock();
            }
            return;
        }

        if (app_center_take_manager_snapshot_lock()) {
            s_manager_snapshot_refresh_pending = false;
            s_manager_snapshot_refresh_in_flight = false;
            s_manager_snapshot_task = NULL;
            app_center_give_manager_snapshot_lock();
        }
        ESP_LOGW(TAG, "Failed to create App.Center manager snapshot refresh task");
    }
}

static void app_center_invalidate_manager_snapshot(void) {
    if (app_center_take_manager_snapshot_lock()) {
        s_manager_snapshot_cache_valid = false;
        app_center_give_manager_snapshot_lock();
    }
    app_center_request_manager_snapshot_refresh();
}

static uint32_t app_center_hash_id(const char *id) {
    uint32_t hash = 2166136261u;

    if (id == NULL || id[0] == '\0') {
        id = "unknown";
    }
    while (*id != '\0') {
        hash ^= (uint8_t)(*id++);
        hash *= 16777619u;
    }
    return hash;
}

static void app_center_make_installed_key(const char *id, char *out_key, size_t out_size) {
    if (out_key == NULL || out_size == 0) {
        return;
    }
    snprintf(out_key, out_size, "%s%08lx", APP_CENTER_INSTALLED_KEY_PREFIX, (unsigned long)app_center_hash_id(id));
}

static void app_center_make_source_key(const char *id, char *out_key, size_t out_size) {
    if (out_key == NULL || out_size == 0) {
        return;
    }
    snprintf(out_key, out_size, "%s%08lx", APP_CENTER_SOURCE_KEY_PREFIX, (unsigned long)app_center_hash_id(id));
}

static void app_center_make_package_path(const char *id, char *out_path, size_t out_size, bool temp) {
    uint32_t hash = app_center_hash_id(id);

    if (out_path == NULL || out_size == 0) {
        return;
    }
    snprintf(out_path, out_size, "%s/%08lx.pkg%s", APP_CENTER_PACKAGE_DIR, (unsigned long)hash, temp ? ".tmp" : "");
}

static void app_center_make_metadata_path(const char *id, char *out_path, size_t out_size) {
    uint32_t hash = app_center_hash_id(id);

    if (out_path == NULL || out_size == 0) {
        return;
    }
    snprintf(out_path, out_size, "%s/%08lx.meta", APP_CENTER_PACKAGE_DIR, (unsigned long)hash);
}

static bool app_center_path_has_suffix(const char *path, const char *suffix) {
    size_t path_len;
    size_t suffix_len;

    if (path == NULL || suffix == NULL) {
        return false;
    }
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool app_center_legacy_espnow_id_matches(const char *id) {
    static const char *const legacy_ids[] = {
        "espnow-remote",
        "esp-now-remote",
        "remote.app",
    };

    if (id == NULL || id[0] == '\0') {
        return false;
    }
    for (size_t index = 0; index < sizeof(legacy_ids) / sizeof(legacy_ids[0]); ++index) {
        if (strcmp(id, legacy_ids[index]) == 0) {
            return true;
        }
    }
    return false;
}

static char app_center_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static bool app_center_ascii_is_alnum(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool app_center_legacy_espnow_name_matches(const char *name) {
    char normalized[sizeof(APP_CENTER_LEGACY_ESPNOW_NAME_KEY)] = {0};
    size_t out_len = 0;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (const char *cursor = name; *cursor != '\0'; ++cursor) {
        if (!app_center_ascii_is_alnum(*cursor)) {
            continue;
        }
        if (out_len + 1 >= sizeof(normalized)) {
            return false;
        }
        normalized[out_len++] = app_center_ascii_lower(*cursor);
    }
    return strcmp(normalized, APP_CENTER_LEGACY_ESPNOW_NAME_KEY) == 0;
}

static bool app_center_legacy_espnow_requested(const char *id, const char *name) {
    return app_center_legacy_espnow_id_matches(id) || app_center_legacy_espnow_name_matches(name);
}

static esp_err_t app_center_sha256_file_hex(const char *path, char *out_hex, size_t out_hex_size) {
    FILE *file = NULL;
    uint8_t *buffer = NULL;
    unsigned char digest[32] = {0};
    mbedtls_sha256_context ctx;
    esp_err_t ret = ESP_OK;

    if (path == NULL || out_hex == NULL || out_hex_size < 65) {
        return ESP_ERR_INVALID_ARG;
    }

    out_hex[0] = '\0';
    file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    buffer = (uint8_t *)malloc(APP_CENTER_PACKAGE_READ_CHUNK);
    if (buffer == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    while (true) {
        size_t read_len = fread(buffer, 1, APP_CENTER_PACKAGE_READ_CHUNK, file);
        if (read_len > 0 && mbedtls_sha256_update(&ctx, buffer, read_len) != 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (read_len < APP_CENTER_PACKAGE_READ_CHUNK) {
            if (ferror(file)) {
                ret = ESP_FAIL;
            }
            break;
        }
    }
    if (ret == ESP_OK && mbedtls_sha256_finish(&ctx, digest) != 0) {
        ret = ESP_FAIL;
    }
    if (ret == ESP_OK) {
        for (size_t index = 0; index < sizeof(digest); ++index) {
            snprintf(out_hex + index * 2, out_hex_size - index * 2, "%02x", digest[index]);
        }
        out_hex[64] = '\0';
    }

cleanup:
    mbedtls_sha256_free(&ctx);
    free(buffer);
    fclose(file);
    return ret;
}

static bool app_center_url_is_absolute(const char *url) {
    return url != NULL &&
           (strncmp(url, "http://", strlen("http://")) == 0 || strncmp(url, "https://", strlen("https://")) == 0);
}

static void app_center_resolve_package_url(const char *url, char *out_url, size_t out_size) {
    const char *last_slash = NULL;
    size_t base_len = 0;

    if (out_url == NULL || out_size == 0) {
        return;
    }
    out_url[0] = '\0';
    if (url == NULL || url[0] == '\0') {
        return;
    }
    if (app_center_url_is_absolute(url) || APP_CENTER_REMOTE_LIST_URL[0] == '\0') {
        safe_copy(out_url, out_size, url);
        return;
    }

    last_slash = strrchr(APP_CENTER_REMOTE_LIST_URL, '/');
    if (last_slash == NULL || last_slash < APP_CENTER_REMOTE_LIST_URL + strlen("http://")) {
        safe_copy(out_url, out_size, url);
        return;
    }

    base_len = (size_t)(last_slash - APP_CENTER_REMOTE_LIST_URL + 1);
    if (base_len >= out_size) {
        return;
    }
    memcpy(out_url, APP_CENTER_REMOTE_LIST_URL, base_len);
    out_url[base_len] = '\0';
    snprintf(out_url + base_len, out_size - base_len, "%s", url);
}

static bool app_center_firmware_url_already_added(const app_center_entry_t *entry, const char *url) {
    if (entry == NULL || url == NULL || url[0] == '\0') {
        return true;
    }
    if (entry->firmware_url[0] != '\0' && strcmp(entry->firmware_url, url) == 0) {
        return true;
    }
    for (int index = 0; index < entry->firmware_mirror_count; ++index) {
        if (strcmp(entry->firmware_mirrors[index], url) == 0) {
            return true;
        }
    }
    return false;
}

static bool app_center_add_firmware_mirror_url(app_center_entry_t *entry, const char *url) {
    char resolved_url[APP_CENTER_URL_MAX] = {0};

    if (entry == NULL || url == NULL || url[0] == '\0' ||
        entry->firmware_mirror_count >= APP_CENTER_FIRMWARE_MIRROR_MAX) {
        return false;
    }

    app_center_resolve_package_url(url, resolved_url, sizeof(resolved_url));
    if (resolved_url[0] == '\0' || app_center_firmware_url_already_added(entry, resolved_url)) {
        return false;
    }

    safe_copy(entry->firmware_mirrors[entry->firmware_mirror_count],
              sizeof(entry->firmware_mirrors[entry->firmware_mirror_count]), resolved_url);
    entry->firmware_mirror_count++;
    return true;
}

static void app_center_load_firmware_mirrors(cJSON *firmware, app_center_entry_t *entry) {
    cJSON *mirrors = NULL;
    cJSON *mirror = NULL;

    if (firmware == NULL || entry == NULL) {
        return;
    }

    mirrors = cJSON_GetObjectItemCaseSensitive(firmware, "mirrors");
    if (!cJSON_IsArray(mirrors)) {
        return;
    }

    cJSON_ArrayForEach(mirror, mirrors) {
        if (cJSON_IsString(mirror) && mirror->valuestring != NULL) {
            (void)app_center_add_firmware_mirror_url(entry, mirror->valuestring);
        }
    }
}

static void app_center_add_firmware_mirrors_json(cJSON *firmware, const app_center_entry_t *entry) {
    cJSON *mirrors = NULL;

    if (firmware == NULL || entry == NULL || entry->firmware_mirror_count <= 0) {
        return;
    }

    mirrors = cJSON_CreateArray();
    if (mirrors == NULL) {
        return;
    }

    for (int index = 0; index < entry->firmware_mirror_count; ++index) {
        if (entry->firmware_mirrors[index][0] != '\0') {
            cJSON *mirror = cJSON_CreateString(entry->firmware_mirrors[index]);
            if (mirror != NULL) {
                cJSON_AddItemToArray(mirrors, mirror);
            }
        }
    }

    if (cJSON_GetArraySize(mirrors) > 0) {
        cJSON_AddItemToObject(firmware, "mirrors", mirrors);
    } else {
        cJSON_Delete(mirrors);
    }
}

static bool app_center_package_exists(const char *id) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    struct stat st = {0};

    app_center_make_package_path(id, path, sizeof(path), false);
    return stat(path, &st) == 0 && st.st_size > 0;
}

static size_t app_center_file_size_or_zero(const char *path) {
    struct stat st = {0};

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        return 0;
    }
    return (size_t)st.st_size;
}

static esp_err_t app_center_package_storage_info(size_t *out_total_bytes, size_t *out_used_bytes,
                                                 size_t *out_free_bytes) {
    size_t total = 0;
    size_t used = 0;
    esp_err_t ret;

    if (out_total_bytes == NULL || out_used_bytes == NULL || out_free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_total_bytes = 0;
    *out_used_bytes = 0;
    *out_free_bytes = 0;
    ret = esp_spiffs_info(APP_CENTER_SPIFFS_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "App package storage info unavailable: %s", esp_err_to_name(ret));
        return ret;
    }
    if (used > total) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_total_bytes = total;
    *out_used_bytes = used;
    *out_free_bytes = total - used;
    return ESP_OK;
}

static esp_err_t app_center_package_storage_free(size_t *out_free_bytes) {
    size_t total = 0;
    size_t used = 0;

    return app_center_package_storage_info(&total, &used, out_free_bytes);
}

static esp_err_t app_center_package_storage_check_fit(const char *app_id, size_t required_bytes, const char *context) {
    char final_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    size_t free_bytes = 0;
    size_t reclaim_bytes = 0;
    size_t available_bytes = 0;
    esp_err_t ret;

    if (required_bytes == 0U) {
        return ESP_OK;
    }
    ret = app_center_package_storage_free(&free_bytes);
    if (ret != ESP_OK) {
        return ret;
    }
    if (app_id != NULL && app_id[0] != '\0') {
        app_center_make_package_path(app_id, final_path, sizeof(final_path), false);
        reclaim_bytes = app_center_file_size_or_zero(final_path);
    }
    available_bytes = free_bytes + reclaim_bytes;
    if (available_bytes <= APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES ||
        required_bytes > available_bytes - APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES) {
        ESP_LOGW(TAG, "Not enough App.Center storage for %s: required=%u free=%u reclaim=%u reserve=%u app=%s",
                 context != NULL ? context : "package", (unsigned int)required_bytes, (unsigned int)free_bytes,
                 (unsigned int)reclaim_bytes, (unsigned int)APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES,
                 app_id != NULL && app_id[0] != '\0' ? app_id : "(unknown)");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t app_center_package_storage_check_next_chunk(size_t chunk_bytes, const char *context) {
    size_t free_bytes = 0;
    esp_err_t ret;

    if (chunk_bytes == 0U) {
        return ESP_OK;
    }
    ret = app_center_package_storage_free(&free_bytes);
    if (ret != ESP_OK) {
        return ret;
    }
    if (free_bytes <= APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES ||
        chunk_bytes > free_bytes - APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES) {
        ESP_LOGW(TAG, "Not enough App.Center storage for %s chunk: chunk=%u free=%u reserve=%u",
                 context != NULL ? context : "package", (unsigned int)chunk_bytes, (unsigned int)free_bytes,
                 (unsigned int)APP_CENTER_PACKAGE_STORAGE_RESERVE_BYTES);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t app_center_prepare_package_storage(void) {
    esp_err_t ret = bsp_spiffs_init_default();

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "App package storage init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (mkdir(APP_CENTER_PACKAGE_DIR, 0775) != 0) {
        struct stat st = {0};
        if (stat(APP_CENTER_PACKAGE_DIR, &st) != 0) {
            ESP_LOGW(TAG, "App package dir may be virtual or unavailable: %s", APP_CENTER_PACKAGE_DIR);
        }
    }

    return ESP_OK;
}

static const char *app_center_canonical_package_id(const char *id, const char *name) {
    if (app_center_legacy_espnow_requested(id, name)) {
        return "";
    }
    return id != NULL ? id : "";
}

static bool app_center_catalog_item_is_local_app(const char *id, const char *name) {
    static const char *const local_ids[] = {
        "launcher",      "basic",     "ble.app",   "client.app",    "phone.control.app",
        "phone-control", "voice.app", "agent.app", "provision.app", "app.center",
    };
    static const char *const local_names[] = {
        "Launcher",      "Basic",     "BLE App",    "Bluetooth App", "Client App",
        "Phone Control", "Voice App", "Agent",      "Agent App",     "Provision App",
        "App.Center",    "蓝牙 App",  "客户端 App", "语音 App",      "配网 App",
    };

    for (size_t index = 0; index < sizeof(local_ids) / sizeof(local_ids[0]); ++index) {
        if (id != NULL && strcmp(id, local_ids[index]) == 0) {
            return true;
        }
    }
    for (size_t index = 0; index < sizeof(local_names) / sizeof(local_names[0]); ++index) {
        if (name != NULL && strcmp(name, local_names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static void app_center_prepare_downloaded_app_launch(const app_center_entry_t *entry) {
    if (entry == NULL) {
        safe_copy(s_launch_app_name, sizeof(s_launch_app_name), "Downloaded App");
        safe_copy(s_launch_app_description, sizeof(s_launch_app_description), "");
        safe_copy(s_launch_app_package_path, sizeof(s_launch_app_package_path), "");
        return;
    }

    safe_copy(s_launch_app_name, sizeof(s_launch_app_name), entry->name[0] != '\0' ? entry->name : "Downloaded App");
    safe_copy(s_launch_app_description, sizeof(s_launch_app_description), entry->description);
    app_center_make_package_path(entry->id, s_launch_app_package_path, sizeof(s_launch_app_package_path), false);
}

bool app_center_get_downloaded_app_launch_info(char *name, size_t name_size, char *description, size_t description_size,
                                               char *package_path, size_t package_path_size) {
    safe_copy(name, name_size, s_launch_app_name);
    safe_copy(description, description_size, s_launch_app_description);
    safe_copy(package_path, package_path_size, s_launch_app_package_path);
    return s_launch_app_name[0] != '\0';
}

static bool app_center_app_installed_load(const char *id) {
    nvs_handle_t handle;
    uint8_t installed = 0;
    char key[APP_CENTER_NVS_KEY_MAX + 1] = {0};

    app_center_make_installed_key(id, key, sizeof(key));

    if (nvs_open(APP_CENTER_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    (void)nvs_get_u8(handle, key, &installed);
    nvs_close(handle);
    return installed != 0;
}

static bool app_center_entry_installed_load(const char *id) {
    return app_center_app_installed_load(id);
}

static void app_center_app_installed_save(const char *id, bool installed) {
    nvs_handle_t handle;
    char key[APP_CENTER_NVS_KEY_MAX + 1] = {0};

    app_center_make_installed_key(id, key, sizeof(key));

    if (nvs_open(APP_CENTER_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open App.Center NVS for installed state");
        return;
    }
    if (nvs_set_u8(handle, key, installed ? 1 : 0) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static void app_center_app_state_clear(const char *id) {
    nvs_handle_t handle;
    char installed_key[APP_CENTER_NVS_KEY_MAX + 1] = {0};
    char source_key[APP_CENTER_NVS_KEY_MAX + 1] = {0};
    bool changed = false;

    if (id == NULL || id[0] == '\0') {
        return;
    }
    app_center_make_installed_key(id, installed_key, sizeof(installed_key));
    app_center_make_source_key(id, source_key, sizeof(source_key));

    if (nvs_open(APP_CENTER_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open App.Center NVS for state clear");
        return;
    }
    if (nvs_erase_key(handle, installed_key) == ESP_OK) {
        changed = true;
    }
    if (nvs_erase_key(handle, source_key) == ESP_OK) {
        changed = true;
    }
    if (changed) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static void app_center_remove_package_artifacts(const char *id) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char temp_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char meta_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};

    if (id == NULL || id[0] == '\0') {
        return;
    }
    app_center_app_state_clear(id);
    app_center_make_package_path(id, path, sizeof(path), false);
    app_center_make_package_path(id, temp_path, sizeof(temp_path), true);
    app_center_make_metadata_path(id, meta_path, sizeof(meta_path));
    (void)remove(path);
    (void)remove(temp_path);
    (void)remove(meta_path);
}

static void app_center_cleanup_legacy_espnow_remote_state(void) {
    static const char *const legacy_ids[] = {
        "espnow-remote",
        "esp-now-remote",
        "remote.app",
    };

    for (size_t index = 0; index < sizeof(legacy_ids) / sizeof(legacy_ids[0]); ++index) {
        app_center_remove_package_artifacts(legacy_ids[index]);
    }
}

static int app_center_preferred_firmware_source_load(const char *id) {
    nvs_handle_t handle;
    uint8_t source_index = 0xFF;
    char key[APP_CENTER_NVS_KEY_MAX + 1] = {0};

    app_center_make_source_key(id, key, sizeof(key));

    if (nvs_open(APP_CENTER_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    (void)nvs_get_u8(handle, key, &source_index);
    nvs_close(handle);
    return source_index < (uint8_t)(1 + APP_CENTER_FIRMWARE_MIRROR_MAX) ? (int)source_index : -1;
}

static void app_center_preferred_firmware_source_save(const char *id, int source_index) {
    nvs_handle_t handle;
    char key[APP_CENTER_NVS_KEY_MAX + 1] = {0};

    if (source_index < 0 || source_index > APP_CENTER_FIRMWARE_MIRROR_MAX) {
        return;
    }
    app_center_make_source_key(id, key, sizeof(key));

    if (nvs_open(APP_CENTER_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open App.Center NVS for preferred source");
        return;
    }
    if (nvs_set_u8(handle, key, (uint8_t)source_index) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static const char *app_center_json_string(cJSON *object, const char *key) {
    if (object == NULL || key == NULL) {
        return "";
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
}

static bool app_center_string_list_contains(const char *list, const char *value) {
    size_t value_len;
    const char *cursor;

    if (list == NULL || value == NULL || value[0] == '\0') {
        return false;
    }

    value_len = strlen(value);
    cursor = list;
    while (*cursor != '\0') {
        const char *start;
        const char *end;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            ++cursor;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            ++cursor;
        }
        end = cursor;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            --end;
        }
        if ((size_t)(end - start) == value_len && strncmp(start, value, value_len) == 0) {
            return true;
        }
        if (*cursor == ',') {
            ++cursor;
        }
    }

    return false;
}

static esp_err_t app_center_sha256_bytes(const uint8_t *data, size_t data_len, uint8_t out_digest[32]) {
    mbedtls_sha256_context ctx;
    esp_err_t ret = ESP_OK;

    if ((data == NULL && data_len > 0U) || out_digest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (data_len > 0U && mbedtls_sha256_update(&ctx, data, data_len) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (mbedtls_sha256_finish(&ctx, out_digest) != 0) {
        ret = ESP_FAIL;
    }

cleanup:
    mbedtls_sha256_free(&ctx);
    return ret;
}

static void app_center_digest_to_hex(const uint8_t digest[32], char *out_hex, size_t out_hex_size) {
    if (digest == NULL || out_hex == NULL || out_hex_size < 65) {
        return;
    }
    for (size_t index = 0; index < 32; ++index) {
        snprintf(out_hex + index * 2U, out_hex_size - index * 2U, "%02x", digest[index]);
    }
    out_hex[64] = '\0';
}

static esp_err_t app_center_verify_manifest_signature(cJSON *root, cJSON *signature, const char *digest_hex,
                                                      const char *issuer, const char *path) {
    const char *public_key_pem = NULL;
    const char *signature_b64 = NULL;
    cJSON *unsigned_root = NULL;
    char *unsigned_json = NULL;
    uint8_t digest[32] = {0};
    char actual_digest_hex[65] = {0};
    uint8_t public_key_digest[32] = {0};
    char public_key_digest_hex[65] = {0};
    uint8_t *signature_bytes = NULL;
    size_t signature_len = 0;
    size_t signature_capacity = 0;
    mbedtls_pk_context pk;
    esp_err_t ret = ESP_OK;

    if (root == NULL || signature == NULL || digest_hex == NULL || digest_hex[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    public_key_pem = app_center_json_string(signature, "publicKeyPem");
    if (public_key_pem[0] == '\0') {
        public_key_pem = app_center_json_string(signature, "public_key_pem");
    }
    signature_b64 = app_center_json_string(signature, "signature");
    if (signature_b64[0] == '\0') {
        signature_b64 = app_center_json_string(signature, "value");
    }
    if (public_key_pem[0] == '\0' || signature_b64[0] == '\0') {
        ESP_LOGW(TAG, "Package production signature missing publicKeyPem or signature: %s", path);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (issuer == NULL || issuer[0] == '\0') {
        ESP_LOGW(TAG, "Package production signature missing issuer: %s", path);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256[0] == '\0') {
        ESP_LOGW(TAG, "No trusted App.Center signing public key hash configured");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (APP_CENTER_TRUSTED_SIGNATURE_ISSUERS[0] != '\0' &&
        !app_center_string_list_contains(APP_CENTER_TRUSTED_SIGNATURE_ISSUERS, issuer)) {
        ESP_LOGW(TAG, "Package signature issuer is not trusted: issuer=%s path=%s",
                 issuer != NULL && issuer[0] != '\0' ? issuer : "(missing)", path);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ret = app_center_sha256_bytes((const uint8_t *)public_key_pem, strlen(public_key_pem), public_key_digest);
    if (ret != ESP_OK) {
        return ret;
    }
    app_center_digest_to_hex(public_key_digest, public_key_digest_hex, sizeof(public_key_digest_hex));
    if (!app_center_string_list_contains(APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256, public_key_digest_hex)) {
        ESP_LOGW(TAG, "Package signing key is not trusted: key_sha256=%s path=%s", public_key_digest_hex, path);
        return ESP_ERR_NOT_SUPPORTED;
    }

    unsigned_root = cJSON_Duplicate(root, true);
    if (unsigned_root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(unsigned_root, "signature");
    unsigned_json = cJSON_PrintUnformatted(unsigned_root);
    cJSON_Delete(unsigned_root);
    unsigned_root = NULL;
    if (unsigned_json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ret = app_center_sha256_bytes((const uint8_t *)unsigned_json, strlen(unsigned_json), digest);
    cJSON_free(unsigned_json);
    unsigned_json = NULL;
    if (ret != ESP_OK) {
        return ret;
    }
    app_center_digest_to_hex(digest, actual_digest_hex, sizeof(actual_digest_hex));
    if (strcasecmp(actual_digest_hex, digest_hex) != 0) {
        ESP_LOGW(TAG, "Package signature digest mismatch: actual=%s expected=%s path=%s", actual_digest_hex, digest_hex,
                 path);
        return ESP_ERR_INVALID_CRC;
    }

    signature_capacity = ((strlen(signature_b64) + 3U) / 4U) * 3U + 1U;
    signature_bytes = (uint8_t *)malloc(signature_capacity);
    if (signature_bytes == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (mbedtls_base64_decode(signature_bytes, signature_capacity, &signature_len, (const unsigned char *)signature_b64,
                              strlen(signature_b64)) != 0) {
        ESP_LOGW(TAG, "Package signature base64 decode failed: %s", path);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    mbedtls_pk_init(&pk);
    if (mbedtls_pk_parse_public_key(&pk, (const unsigned char *)public_key_pem, strlen(public_key_pem) + 1U) != 0) {
        ESP_LOGW(TAG, "Package signing public key parse failed: %s", path);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup_pk;
    }
    if (mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature_bytes, signature_len) != 0) {
        ESP_LOGW(TAG, "Package ECDSA signature verification failed: %s", path);
        ret = ESP_ERR_INVALID_CRC;
    }

cleanup_pk:
    mbedtls_pk_free(&pk);
cleanup:
    free(signature_bytes);
    return ret;
}

static void app_center_save_package_metadata(const char *id, const char *name, const char *version,
                                             const char *description) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char *json = NULL;
    FILE *file = NULL;
    cJSON *root = NULL;

    if (id == NULL || id[0] == '\0') {
        return;
    }

    app_center_make_metadata_path(id, path, sizeof(path));
    root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "name", name != NULL && name[0] != '\0' ? name : id);
    cJSON_AddStringToObject(root, "version", version != NULL ? version : "");
    cJSON_AddStringToObject(root, "description", description != NULL ? description : "");

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return;
    }

    file = fopen(path, "wb");
    if (file != NULL) {
        (void)fwrite(json, 1, strlen(json), file);
        fclose(file);
    }
    cJSON_free(json);
}

static void app_center_save_entry_metadata(const app_center_entry_t *entry) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char *json = NULL;
    FILE *file = NULL;
    cJSON *root = NULL;
    cJSON *firmware = NULL;

    if (entry == NULL || entry->id[0] == '\0') {
        return;
    }

    app_center_make_metadata_path(entry->id, path, sizeof(path));
    root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "id", entry->id);
    cJSON_AddStringToObject(root, "name", entry->name[0] != '\0' ? entry->name : entry->id);
    cJSON_AddStringToObject(root, "version", entry->version);
    cJSON_AddStringToObject(root, "description", entry->description);
    cJSON_AddStringToObject(root, "type",
                            entry->type == APP_CENTER_ENTRY_FIRMWARE_APP ? "firmware-app" : "manifest-app");

    if (entry->type == APP_CENTER_ENTRY_FIRMWARE_APP) {
        firmware = cJSON_CreateObject();
        if (firmware != NULL) {
            cJSON_AddStringToObject(firmware, "url", entry->firmware_url);
            cJSON_AddStringToObject(firmware, "sha256", entry->firmware_sha256);
            cJSON_AddNumberToObject(firmware, "sizeBytes", (double)entry->firmware_size_bytes);
            cJSON_AddStringToObject(firmware, "imageVersion", entry->image_version);
            app_center_add_firmware_mirrors_json(firmware, entry);
            cJSON_AddItemToObject(root, "firmware", firmware);
            firmware = NULL;
        }
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return;
    }

    file = fopen(path, "wb");
    if (file != NULL) {
        (void)fwrite(json, 1, strlen(json), file);
        fclose(file);
    }
    cJSON_free(json);
}

static cJSON *app_center_load_package_metadata_json(const char *id) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    FILE *file = NULL;
    char *buffer = NULL;
    long length;
    cJSON *root = NULL;

    if (id == NULL || id[0] == '\0') {
        return NULL;
    }

    app_center_make_metadata_path(id, path, sizeof(path));
    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        goto cleanup;
    }
    length = ftell(file);
    if (length <= 0 || length > APP_CENTER_PACKAGE_MANIFEST_MAX_BYTES) {
        goto cleanup;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        goto cleanup;
    }
    buffer = (char *)calloc((size_t)length + 1, 1);
    if (buffer == NULL) {
        goto cleanup;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        goto cleanup;
    }
    root = cJSON_ParseWithLength(buffer, (size_t)length);
    if (root == NULL || !cJSON_IsObject(root)) {
        goto cleanup;
    }

    free(buffer);
    fclose(file);
    return root;

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    free(buffer);
    if (file != NULL) {
        fclose(file);
    }
    return NULL;
}

static bool app_center_read_package_metadata_string(const char *id, const char *key, char *out, size_t out_size) {
    cJSON *root = NULL;
    const char *value = NULL;
    bool found = false;

    if (id == NULL || id[0] == '\0' || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    root = app_center_load_package_metadata_json(id);
    if (root == NULL) {
        return false;
    }

    value = app_center_json_string(root, key);
    if (value[0] != '\0') {
        safe_copy(out, out_size, value);
        found = true;
    }

    cJSON_Delete(root);
    return found;
}

static bool app_center_read_package_firmware_metadata_string(const char *id, const char *key, char *out,
                                                             size_t out_size) {
    cJSON *root = NULL;
    cJSON *firmware = NULL;
    const char *value = NULL;
    bool found = false;

    if (id == NULL || id[0] == '\0' || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    root = app_center_load_package_metadata_json(id);
    if (root == NULL) {
        return false;
    }

    firmware = cJSON_GetObjectItemCaseSensitive(root, "firmware");
    if (firmware != NULL && cJSON_IsObject(firmware)) {
        value = app_center_json_string(firmware, key);
        if (value[0] != '\0') {
            safe_copy(out, out_size, value);
            found = true;
        }
    }

    cJSON_Delete(root);
    return found;
}

static bool app_center_firmware_install_matches_catalog(const app_center_entry_t *entry) {
    char installed_type[24] = {0};
    char installed_version[32] = {0};
    char installed_sha256[65] = {0};
    char installed_image_version[32] = {0};

    if (entry == NULL || entry->id[0] == '\0' || !app_center_app_installed_load(entry->id)) {
        return false;
    }

    if (!app_center_read_package_metadata_string(entry->id, "type", installed_type, sizeof(installed_type)) ||
        strcmp(installed_type, "firmware-app") != 0) {
        ESP_LOGI(TAG, "Firmware app install metadata missing or not firmware-app: id=%s", entry->id);
        app_center_app_state_clear(entry->id);
        return false;
    }

    if (entry->version[0] != '\0') {
        if (!app_center_read_package_metadata_string(entry->id, "version", installed_version,
                                                     sizeof(installed_version)) ||
            strcmp(installed_version, entry->version) != 0) {
            ESP_LOGI(TAG, "Firmware app catalog version changed: id=%s installed=%s catalog=%s", entry->id,
                     installed_version[0] != '\0' ? installed_version : "(none)", entry->version);
            app_center_app_state_clear(entry->id);
            return false;
        }
    }

    if (entry->firmware_sha256[0] != '\0') {
        if (!app_center_read_package_firmware_metadata_string(entry->id, "sha256", installed_sha256,
                                                              sizeof(installed_sha256)) ||
            strcmp(installed_sha256, entry->firmware_sha256) != 0) {
            ESP_LOGI(TAG, "Firmware app catalog SHA changed: id=%s", entry->id);
            app_center_app_state_clear(entry->id);
            return false;
        }
    }

    if (entry->image_version[0] != '\0') {
        if (!app_center_read_package_firmware_metadata_string(entry->id, "imageVersion", installed_image_version,
                                                              sizeof(installed_image_version)) ||
            strcmp(installed_image_version, entry->image_version) != 0) {
            ESP_LOGI(TAG, "Firmware app image version changed: id=%s installed=%s catalog=%s", entry->id,
                     installed_image_version[0] != '\0' ? installed_image_version : "(none)", entry->image_version);
            app_center_app_state_clear(entry->id);
            return false;
        }
    }

    return true;
}

static bool app_center_parse_version_part(const char **cursor, long *out_value) {
    long value = 0;
    const char *p = cursor != NULL ? *cursor : NULL;

    if (p == NULL || out_value == NULL || *p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    *out_value = value;
    *cursor = p;
    return true;
}

static int app_center_compare_numeric_versions(const char *left, const char *right, bool *comparable) {
    const char *a = left;
    const char *b = right;

    if (comparable != NULL) {
        *comparable = false;
    }
    if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0') {
        return 0;
    }

    while (*a != '\0' || *b != '\0') {
        long av = 0;
        long bv = 0;
        if (!app_center_parse_version_part(&a, &av) || !app_center_parse_version_part(&b, &bv)) {
            return 0;
        }
        if (comparable != NULL) {
            *comparable = true;
        }
        if (av != bv) {
            return av > bv ? 1 : -1;
        }
        if (*a == '\0' && *b == '\0') {
            return 0;
        }
        if (*a == '.' || *a == '-' || *a == '_') {
            ++a;
        } else if (*a != '\0') {
            return 0;
        }
        if (*b == '.' || *b == '-' || *b == '_') {
            ++b;
        } else if (*b != '\0') {
            return 0;
        }
    }
    return 0;
}

static esp_err_t app_center_reject_downgrade_if_needed(const char *id, const char *new_version) {
    char installed_version[32] = {0};
    bool comparable = false;
    int cmp;

    if (id == NULL || id[0] == '\0' || new_version == NULL || new_version[0] == '\0' ||
        !app_center_app_installed_load(id) || !app_center_package_exists(id)) {
        return ESP_OK;
    }
    if (!app_center_read_package_metadata_string(id, "version", installed_version, sizeof(installed_version)) ||
        installed_version[0] == '\0') {
        return ESP_OK;
    }
    cmp = app_center_compare_numeric_versions(new_version, installed_version, &comparable);
    if (comparable && cmp < 0) {
        ESP_LOGW(TAG, "Rejecting app package downgrade: id=%s installed=%s incoming=%s", id, installed_version,
                 new_version);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static void app_center_load_package_metadata_file(const char *path) {
    FILE *file = NULL;
    char *buffer = NULL;
    long length;
    cJSON *root = NULL;
    const char *id;
    const char *name;
    bool remove_current_path = false;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        goto cleanup;
    }
    length = ftell(file);
    if (length <= 0 || length > APP_CENTER_PACKAGE_MANIFEST_MAX_BYTES) {
        goto cleanup;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        goto cleanup;
    }
    buffer = (char *)calloc((size_t)length + 1, 1);
    if (buffer == NULL) {
        goto cleanup;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        goto cleanup;
    }
    root = cJSON_ParseWithLength(buffer, (size_t)length);
    if (root == NULL || !cJSON_IsObject(root)) {
        goto cleanup;
    }

    id = app_center_json_string(root, "id");
    name = app_center_json_string(root, "name");
    if (app_center_legacy_espnow_requested(id, name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        remove_current_path = true;
        goto cleanup;
    }
    if (id[0] != '\0' && app_center_entry_installed_load(id)) {
        const char *type = app_center_json_string(root, "type");
        if (strcmp(type, "firmware-app") == 0) {
            cJSON *firmware = json_get_object(root, "firmware");
            app_center_entry_t entry = {0};
            safe_copy(entry.id, sizeof(entry.id), id);
            safe_copy(entry.name, sizeof(entry.name), name);
            safe_copy(entry.version, sizeof(entry.version), app_center_json_string(root, "version"));
            safe_copy(entry.description, sizeof(entry.description), app_center_json_string(root, "description"));
            entry.type = APP_CENTER_ENTRY_FIRMWARE_APP;
            safe_copy(entry.firmware_url, sizeof(entry.firmware_url), app_center_json_string(firmware, "url"));
            safe_copy(entry.package_url, sizeof(entry.package_url), entry.firmware_url);
            safe_copy(entry.firmware_sha256, sizeof(entry.firmware_sha256), app_center_json_string(firmware, "sha256"));
            entry.firmware_size_bytes = json_get_size_t(firmware, "sizeBytes", "size_bytes", "size");
            safe_copy(entry.image_version, sizeof(entry.image_version),
                      app_center_json_string(firmware, "imageVersion"));
            app_center_load_firmware_mirrors(firmware, &entry);
            (void)app_center_upsert_firmware_entry(&entry, true);
        } else if (app_center_package_exists(id)) {
            (void)app_center_upsert_package_entry(id, app_center_json_string(root, "name"),
                                                  app_center_json_string(root, "version"),
                                                  app_center_json_string(root, "description"), true);
        }
    }

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    free(buffer);
    if (file != NULL) {
        fclose(file);
    }
    if (remove_current_path) {
        (void)remove(path);
    }
}

static void app_center_load_persisted_package_metadata(void) {
    DIR *dir = opendir(APP_CENTER_PACKAGE_DIR);

    if (dir == NULL) {
        return;
    }

    while (true) {
        struct dirent *entry = readdir(dir);
        char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};

        if (entry == NULL) {
            break;
        }
        if (!app_center_path_has_suffix(entry->d_name, ".meta")) {
            continue;
        }
        size_t dir_len = strlen(APP_CENTER_PACKAGE_DIR);
        size_t name_len = strlen(entry->d_name);
        if (dir_len + 1 + name_len + 1 > sizeof(path)) {
            ESP_LOGW(TAG, "Skip App.Center metadata with too long file name: %s", entry->d_name);
            continue;
        }
        memcpy(path, APP_CENTER_PACKAGE_DIR, dir_len);
        path[dir_len] = '/';
        memcpy(path + dir_len + 1, entry->d_name, name_len + 1);
        app_center_load_package_metadata_file(path);
    }
    closedir(dir);
}

static app_center_manager_app_kind_t app_center_entry_manager_kind(const app_center_entry_t *entry) {
    return entry != NULL && entry->type == APP_CENTER_ENTRY_FIRMWARE_APP ? APP_CENTER_MANAGER_APP_FIRMWARE
                                                                         : APP_CENTER_MANAGER_APP_MANIFEST;
}

static esp_err_t app_center_uninstall_entry_by_id(const char *app_id, const char *name, const char *version,
                                                  const char *command_id, bool update_ui) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char temp_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char meta_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    const char *display_name = NULL;
    app_center_manager_uninstall_policy_t policy;
    app_center_manager_app_kind_t kind = APP_CENTER_MANAGER_APP_MANIFEST;
    int index;

    if (app_id == NULL || app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_center_legacy_espnow_requested(app_id, name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        if (command_id != NULL && command_id[0] != '\0') {
            (void)ws_send_app_package_status(command_id, app_id, name != NULL && name[0] != '\0' ? name : app_id,
                                             version != NULL ? version : "", "uninstalled", "removed");
        }
        if (update_ui) {
            snprintf(s_remote_status, sizeof(s_remote_status), "%s removed",
                     name != NULL && name[0] != '\0' ? name : app_id);
            s_page = APP_CENTER_PAGE_LIST;
            s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
            s_uninstall_confirm_index = -1;
            s_render_requested = true;
        }
        app_center_invalidate_manager_snapshot();
        return ESP_OK;
    }

    app_center_ensure_package_entries_loaded();
    index = app_center_find_entry_by_id(app_id);
    if (index >= 0) {
        kind = app_center_entry_manager_kind(&s_entries[index]);
        display_name = s_entries[index].name[0] != '\0' ? s_entries[index].name : app_id;
    } else {
        display_name = name != NULL && name[0] != '\0' ? name : app_id;
    }
    policy = app_center_manager_uninstall_policy(kind);

    ESP_LOGI(TAG, "App.Center uninstall app: id=%s name=%s type=%s installed=%d erase_ota=%d", app_id, display_name,
             kind == APP_CENTER_MANAGER_APP_FIRMWARE ? "firmware-app" : "manifest-app",
             index >= 0 && s_entries[index].installed ? 1 : 0, policy.erase_ota_slot ? 1 : 0);

    if (policy.clear_installed_state) {
        app_center_app_state_clear(app_id);
    }
    app_center_make_package_path(app_id, path, sizeof(path), false);
    app_center_make_package_path(app_id, temp_path, sizeof(temp_path), true);
    app_center_make_metadata_path(app_id, meta_path, sizeof(meta_path));
    if (policy.remove_package_files) {
        (void)remove(path);
        (void)remove(temp_path);
    }
    if (policy.remove_metadata_file) {
        (void)remove(meta_path);
    }
    if (index >= 0) {
        s_entries[index].installed = false;
    }
    if (command_id != NULL && command_id[0] != '\0') {
        const char *display_version = index >= 0 ? s_entries[index].version : (version != NULL ? version : "");
        (void)ws_send_app_package_status(command_id, app_id, display_name, display_version, "uninstalled",
                                         policy.removed_message);
    }
    if (update_ui) {
        snprintf(s_remote_status, sizeof(s_remote_status), "%s %s", display_name, policy.removed_message);
        s_page = APP_CENTER_PAGE_LIST;
        s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
        s_uninstall_confirm_index = -1;
        s_render_requested = true;
    }
    app_center_invalidate_manager_snapshot();
    return ESP_OK;
}

static esp_err_t app_center_uninstall_entry(int index) {
    if (index < 0 || index >= s_entry_count) {
        ESP_LOGW(TAG, "App.Center uninstall ignored: invalid index=%d count=%d", index, s_entry_count);
        return ESP_ERR_INVALID_ARG;
    }
    return app_center_uninstall_entry_by_id(s_entries[index].id, s_entries[index].name, s_entries[index].version, NULL,
                                            true);
}

static esp_err_t app_center_open_firmware_entry(const app_center_entry_t *entry) {
    const esp_partition_t *running = NULL;
    const esp_partition_t *next = NULL;
    esp_err_t ret;

    if (entry == NULL || entry->type != APP_CENTER_ENTRY_FIRMWARE_APP || !entry->installed) {
        return ESP_ERR_INVALID_ARG;
    }

    running = esp_ota_get_running_partition();
    next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        ESP_LOGE(TAG, "Firmware app open failed: no inactive OTA partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (running == next) {
        ESP_LOGW(TAG, "Firmware app open skipped: next OTA partition is running slot %s", running->label);
        return ESP_ERR_INVALID_STATE;
    }

    ret = esp_ota_set_boot_partition(next);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Firmware app open failed: set boot partition %s: %s", next->label, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGW(TAG, "Firmware app opening: id=%s running=%s next=%s", entry->id,
             running != NULL ? running->label : "(unknown)", next->label);
    snprintf(s_remote_status, sizeof(s_remote_status), "Opening %s", entry->name);
    s_render_requested = true;
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
    return ESP_OK;
}

static void app_center_open_entry(int index) {
    if (index < 0 || index >= s_entry_count) {
        ESP_LOGW(TAG, "App.Center open ignored: invalid index=%d count=%d", index, s_entry_count);
        return;
    }

    s_uninstall_confirm_index = -1;
    ESP_LOGI(TAG, "App.Center open entry: index=%d id=%s name=%s installed=%d", index, s_entries[index].id,
             s_entries[index].name, s_entries[index].installed ? 1 : 0);
    if (s_entries[index].type == APP_CENTER_ENTRY_FIRMWARE_APP) {
        esp_err_t ret = app_center_open_firmware_entry(&s_entries[index]);
        if (ret != ESP_OK) {
            snprintf(s_remote_status, sizeof(s_remote_status), "%s failed to open", s_entries[index].name);
            s_page = APP_CENTER_PAGE_LIST;
            s_render_requested = true;
        }
        return;
    }

    if (!app_center_package_exists(s_entries[index].id)) {
        s_entries[index].installed = false;
        app_center_app_installed_save(s_entries[index].id, false);
        snprintf(s_remote_status, sizeof(s_remote_status), "%s package missing", s_entries[index].name);
        s_page = APP_CENTER_PAGE_LIST;
        s_render_requested = true;
        return;
    }

    app_center_prepare_downloaded_app_launch(&s_entries[index]);
    if (watcher_app_open("downloaded.app") != ESP_OK) {
        snprintf(s_remote_status, sizeof(s_remote_status), "%s failed to open", s_entries[index].name);
        s_page = APP_CENTER_PAGE_LIST;
        s_render_requested = true;
    }
}

static bool app_center_wifi_gate_needed(void) {
    return wifi_is_connected() != 1;
}

static uint32_t app_center_remote_retry_delay_ms(void) {
    if (s_remote_fetch_failure_count == 0) {
        return APP_CENTER_REMOTE_RETRY_FIRST_MS;
    }
    if (s_remote_fetch_failure_count == 1) {
        return APP_CENTER_REMOTE_RETRY_SECOND_MS;
    }
    return APP_CENTER_REMOTE_RETRY_MAX_MS;
}

static void app_center_schedule_remote_retry(void) {
    uint32_t retry_ms = app_center_remote_retry_delay_ms();

    if (s_remote_fetch_failure_count < UINT8_MAX) {
        s_remote_fetch_failure_count++;
    }
    s_remote_retry_after_tick = xTaskGetTickCount() + pdMS_TO_TICKS(retry_ms);
}

static void app_center_ensure_wifi_resume(void) {
    if (s_download_running || s_page == APP_CENTER_PAGE_INSTALL) {
        return;
    }
    if (wifi_has_credentials() == 1 && wifi_is_connected() != 1 && wifi_is_connect_requested() != 1) {
        (void)wifi_resume_background();
    }
}

static void app_center_ignore_button_clicks(uint32_t duration_ms) {
    s_button_ignore_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
}

static bool app_center_button_click_ignored(void) {
    TickType_t now = xTaskGetTickCount();
    return s_button_ignore_until_tick != 0 && now < s_button_ignore_until_tick;
}

static app_center_exit_page_t app_center_exit_page_from_current(void) {
    switch (s_page) {
    case APP_CENTER_PAGE_INSTALL:
        return APP_CENTER_EXIT_PAGE_INSTALL;
    case APP_CENTER_PAGE_DETAIL:
        return APP_CENTER_EXIT_PAGE_DETAIL;
    case APP_CENTER_PAGE_LIST:
    default:
        return APP_CENTER_EXIT_PAGE_LIST;
    }
}

static const char *app_center_page_name(app_center_page_t page) {
    switch (page) {
    case APP_CENTER_PAGE_LIST:
        return "list";
    case APP_CENTER_PAGE_DETAIL:
        return "detail";
    case APP_CENTER_PAGE_INSTALL:
        return "install";
    default:
        return "unknown";
    }
}

static void app_center_request_download_cancel(const char *reason) {
    bool first_cancel_request = !s_download_cancel_requested;
    const char *safe_reason = reason != NULL && reason[0] != '\0' ? reason : "download cancel";

    s_download_cancel_requested = true;
    safe_copy(s_download_status, sizeof(s_download_status), "Canceling...");
    if (first_cancel_request) {
        app_center_cancel_http_client(&s_download_client, safe_reason);
        (void)ota_service_cancel_active_request(safe_reason);
    }
    s_render_requested = true;
}

static bool app_center_exit_is_visible_locked(void) {
    return s_exit_button != NULL && !lv_obj_has_flag(s_exit_button, LV_OBJ_FLAG_HIDDEN);
}

static void app_center_request_return_to_launcher(const char *reason) {
    const char *safe_reason = reason != NULL && reason[0] != '\0' ? reason : "unknown";

    if (s_download_running) {
        app_center_request_download_cancel(safe_reason);
        ESP_LOGI(TAG, "App.Center return requested during download: %s", safe_reason);
    } else {
        ESP_LOGI(TAG, "App.Center return requested: %s", safe_reason);
    }
    s_return_launcher_requested = true;
    s_render_requested = true;
}

static void app_center_hide_exit_locked(void) {
    if (s_exit_button != NULL) {
        lv_group_remove_obj(s_exit_button);
        lv_obj_add_flag(s_exit_button, LV_OBJ_FLAG_HIDDEN);
    }
    s_exit_hide_at_tick = 0;
}

static void app_center_exit_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && app_center_exit_is_visible_locked()) {
        ESP_LOGI(TAG, "App.Center exit clicked");
        app_center_hide_exit_locked();
        app_center_request_return_to_launcher("exit button");
    }
}

static void app_center_show_exit_locked(void) {
    lv_obj_t *parent = lv_layer_top();

    if (parent == NULL) {
        parent = s_screen;
    }
    if (parent == NULL) {
        return;
    }

    if (s_exit_button == NULL) {
        s_exit_button = lv_btn_create(parent);
        lv_obj_set_size(s_exit_button, 60, 60);
        lv_obj_align(s_exit_button, LV_ALIGN_BOTTOM_MID, 0, -30);
        lv_obj_set_style_radius(s_exit_button, 50, 0);
        lv_obj_set_style_bg_color(s_exit_button, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(s_exit_button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_img_src(s_exit_button, &ui_img_button_cancel_png, 0);
        lv_obj_set_style_border_width(s_exit_button, 0, 0);
        lv_obj_add_flag(s_exit_button, LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(s_exit_button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_exit_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s_exit_button, 16);
        lv_obj_add_event_cb(s_exit_button, app_center_exit_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_move_foreground(s_exit_button);
    lv_obj_clear_flag(s_exit_button, LV_OBJ_FLAG_HIDDEN);
    if (s_group == NULL || lv_obj_get_group(s_exit_button) != s_group) {
        app_center_focus_obj_locked(s_exit_button);
    } else if (s_group != NULL) {
        lv_group_focus_obj(s_exit_button);
    }
    s_exit_hide_at_tick = xTaskGetTickCount() + pdMS_TO_TICKS(APP_CENTER_EXIT_AUTO_HIDE_MS);
    ESP_LOGW(TAG, "App.Center exit show");
}

static const char *json_get_string(cJSON *object, const char *key_a, const char *key_b, const char *key_c) {
    cJSON *item = NULL;

    if (object == NULL) {
        return NULL;
    }

    if (key_a != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(object, key_a);
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            return item->valuestring;
        }
    }
    if (key_b != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(object, key_b);
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            return item->valuestring;
        }
    }
    if (key_c != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(object, key_c);
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            return item->valuestring;
        }
    }

    return NULL;
}

static cJSON *json_get_object(cJSON *object, const char *key) {
    cJSON *item = object != NULL ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsObject(item) ? item : NULL;
}

static size_t json_get_size_t(cJSON *object, const char *key_a, const char *key_b, const char *key_c) {
    const char *keys[] = {key_a, key_b, key_c};

    if (object == NULL) {
        return 0;
    }
    for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        cJSON *item;
        if (keys[index] == NULL) {
            continue;
        }
        item = cJSON_GetObjectItemCaseSensitive(object, keys[index]);
        if (cJSON_IsNumber(item) && item->valuedouble > 0) {
            return (size_t)item->valuedouble;
        }
    }
    return 0;
}

static int json_get_int(cJSON *object, const char *key_a, const char *key_b, const char *key_c) {
    return (int)json_get_size_t(object, key_a, key_b, key_c);
}

static bool app_center_url_is_https(const char *url) {
    return url != NULL && strncmp(url, "https://", strlen("https://")) == 0;
}

static bool app_center_is_sha256_hex(const char *value) {
    if (value == NULL || strlen(value) != 64U) {
        return false;
    }
    for (size_t index = 0; index < 64U; ++index) {
        char ch = value[index];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool app_center_device_flash_mb(int *out_flash_mb) {
    uint32_t flash_bytes = 0;
    esp_err_t ret;

    if (out_flash_mb == NULL) {
        return false;
    }
    *out_flash_mb = 0;
    ret = esp_flash_get_size(NULL, &flash_bytes);
    if (ret != ESP_OK || flash_bytes == 0) {
        ESP_LOGW(TAG, "Failed to read flash size: %s", esp_err_to_name(ret));
        return false;
    }
    *out_flash_mb = (int)(flash_bytes / (1024U * 1024U));
    return true;
}

static bool app_center_catalog_compat_supported(cJSON *compat, app_center_entry_t *entry, bool required) {
    const char *product;
    const char *chip;
    const char *min_launcher;
    int required_flash_mb;
    size_t ota_slot_size;
    int device_flash_mb = 0;

    if (compat == NULL) {
        return !required;
    }

    product = json_get_string(compat, "product", "device", "target");
    if (product != NULL && product[0] != '\0' && strcmp(product, "WatcheRobot") != 0) {
        ESP_LOGW(TAG, "Catalog app rejected: unsupported product=%s", product);
        return false;
    }

    chip = json_get_string(compat, "chip", "targetChip", "idfTarget");
    if (chip != NULL && chip[0] != '\0' && strcmp(chip, "esp32s3") != 0 && strcmp(chip, "esp32-s3") != 0) {
        ESP_LOGW(TAG, "Catalog app rejected: unsupported chip=%s", chip);
        return false;
    }

    required_flash_mb = json_get_int(compat, "flashSizeMb", "flash_size_mb", "flashMb");
    if (entry != NULL) {
        entry->compat_flash_size_mb = required_flash_mb;
    }
    if (required_flash_mb > 0 && app_center_device_flash_mb(&device_flash_mb) && device_flash_mb < required_flash_mb) {
        ESP_LOGW(TAG, "Catalog app rejected: flash=%dMB required=%dMB", device_flash_mb, required_flash_mb);
        return false;
    }

    ota_slot_size = json_get_size_t(compat, "otaSlotSize", "ota_slot_size", "otaSlotBytes");
    if (entry != NULL) {
        entry->compat_ota_slot_size = ota_slot_size;
    }
    if (ota_slot_size > APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES) {
        ESP_LOGW(TAG, "Catalog app rejected: otaSlotSize=%u max=%u", (unsigned int)ota_slot_size,
                 (unsigned int)APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES);
        return false;
    }

    min_launcher = json_get_string(compat, "minLauncherVersion", "min_launcher_version", "minFwVersion");
    if (entry != NULL) {
        safe_copy(entry->min_launcher_version, sizeof(entry->min_launcher_version), min_launcher);
    }
    if (min_launcher != NULL && min_launcher[0] != '\0') {
        bool comparable = false;
        int cmp = app_center_compare_numeric_versions(ota_service_get_fw_version(), min_launcher, &comparable);
        if (comparable && cmp < 0) {
            ESP_LOGW(TAG, "Catalog app rejected: launcher=%s min=%s", ota_service_get_fw_version(), min_launcher);
            return false;
        }
    }

    return true;
}

static esp_err_t app_center_validate_firmware_entry_for_install(const app_center_entry_t *entry) {
    size_t max_bytes;

    if (entry == NULL || entry->type != APP_CENTER_ENTRY_FIRMWARE_APP) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_center_url_is_https(entry->firmware_url)) {
        ESP_LOGW(TAG, "Firmware app URL must be HTTPS: id=%s url=%s", entry->id, entry->firmware_url);
        return ESP_ERR_INVALID_ARG;
    }
    for (int index = 0; index < entry->firmware_mirror_count; ++index) {
        if (!app_center_url_is_https(entry->firmware_mirrors[index])) {
            ESP_LOGW(TAG, "Firmware app mirror URL must be HTTPS: id=%s url=%s", entry->id,
                     entry->firmware_mirrors[index]);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!app_center_is_sha256_hex(entry->firmware_sha256)) {
        ESP_LOGW(TAG, "Firmware app SHA-256 missing or invalid: id=%s", entry->id);
        return ESP_ERR_INVALID_CRC;
    }
    max_bytes = entry->compat_ota_slot_size > 0U ? entry->compat_ota_slot_size : APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES;
    if (max_bytes > APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES) {
        max_bytes = APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES;
    }
    if (entry->firmware_size_bytes == 0U || entry->firmware_size_bytes > max_bytes) {
        ESP_LOGW(TAG, "Firmware app size invalid: id=%s size=%u max=%u", entry->id,
                 (unsigned int)entry->firmware_size_bytes, (unsigned int)max_bytes);
        return ESP_ERR_INVALID_SIZE;
    }
    if (entry->signature_algorithm[0] != '\0' && strcmp(entry->signature_algorithm, "unsigned-dev") != 0) {
        ESP_LOGW(TAG, "Firmware app signature algorithm unsupported in V1: id=%s algorithm=%s", entry->id,
                 entry->signature_algorithm);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES) {
        ESP_LOGW(TAG, "Unsigned developer firmware app is disabled: id=%s", entry->id);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static void app_center_reset_entries(void) {
    memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0;
    app_center_cleanup_legacy_espnow_remote_state();
    app_center_load_persisted_package_metadata();
    s_selected_index = 0;
    s_action_index = 0;
    s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
}

static bool app_center_add_remote_entry(cJSON *item) {
    app_center_entry_t entry = {0};
    app_center_entry_t *existing_remote = NULL;
    cJSON *compat = NULL;
    cJSON *firmware = NULL;
    cJSON *signature = NULL;
    const char *id = NULL;
    const char *name = NULL;
    const char *url = NULL;
    const char *type = NULL;
    bool firmware_app = false;

    if (item == NULL || !cJSON_IsObject(item)) {
        return false;
    }

    name = json_get_string(item, "name", "appName", "title");
    id = json_get_string(item, "id", "appId", "key");
    type = json_get_string(item, "type", "appType", "kind");
    firmware_app = type != NULL && strcmp(type, "firmware-app") == 0;
    if (app_center_legacy_espnow_requested(id, name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        ESP_LOGW(TAG, "Catalog app rejected: legacy remote app id=%s name=%s", id != NULL ? id : "(none)",
                 name != NULL ? name : "(none)");
        return false;
    }
    if (app_center_catalog_item_is_local_app(id, name)) {
        return false;
    }

    if (firmware_app) {
        firmware = json_get_object(item, "firmware");
        url = json_get_string(firmware, "url", "downloadUrl", "firmwareUrl");
        if (url == NULL || url[0] == '\0') {
            url = json_get_string(item, "url", "downloadUrl", "firmwareUrl");
        }
    } else {
        url = json_get_string(item, "packageUrl", "downloadUrl", "appUrl");
        if (url == NULL || url[0] == '\0') {
            url = json_get_string(item, "url", NULL, NULL);
        }
    }
    if (id == NULL || id[0] == '\0' || name == NULL || name[0] == '\0' || url == NULL || url[0] == '\0') {
        return false;
    }
    safe_copy(entry.name, sizeof(entry.name), name);
    safe_copy(entry.id, sizeof(entry.id), app_center_canonical_package_id(id, entry.name));
    if (entry.id[0] == '\0') {
        return false;
    }
    safe_copy(entry.description, sizeof(entry.description),
              json_get_string(item, "description", "desc", "appDescription"));
    safe_copy(entry.icon_url, sizeof(entry.icon_url), json_get_string(item, "iconUrl", "appIcon", "icon"));
    safe_copy(entry.version, sizeof(entry.version), json_get_string(item, "version", "fwVersion", "firmwareVersion"));
    compat = json_get_object(item, "compat");
    if (!app_center_catalog_compat_supported(compat, &entry, firmware_app)) {
        return false;
    }

    if (firmware_app) {
        entry.type = APP_CENTER_ENTRY_FIRMWARE_APP;
        app_center_resolve_package_url(url, entry.firmware_url, sizeof(entry.firmware_url));
        safe_copy(entry.package_url, sizeof(entry.package_url), entry.firmware_url);
        safe_copy(entry.firmware_sha256, sizeof(entry.firmware_sha256),
                  json_get_string(firmware, "sha256", "sha256Hex", "digest"));
        entry.firmware_size_bytes = json_get_size_t(firmware, "sizeBytes", "size_bytes", "size");
        safe_copy(entry.image_version, sizeof(entry.image_version),
                  json_get_string(firmware, "imageVersion", "image_version", "fwVersion"));
        app_center_load_firmware_mirrors(firmware, &entry);
        if (entry.image_version[0] != '\0' && entry.version[0] == '\0') {
            safe_copy(entry.version, sizeof(entry.version), entry.image_version);
        }
        signature = json_get_object(firmware, "signature");
        safe_copy(entry.signature_algorithm, sizeof(entry.signature_algorithm),
                  json_get_string(signature, "algorithm", "alg", NULL));
        if (app_center_validate_firmware_entry_for_install(&entry) != ESP_OK) {
            return false;
        }
    } else {
        entry.type = APP_CENTER_ENTRY_PACKAGE;
        app_center_resolve_package_url(url, entry.package_url, sizeof(entry.package_url));
    }
    entry.installed =
        firmware_app ? app_center_firmware_install_matches_catalog(&entry) : app_center_entry_installed_load(entry.id);

    for (int index = 0; index < s_entry_count; ++index) {
        if (strcmp(s_entries[index].id, entry.id) == 0 || strcmp(s_entries[index].name, entry.name) == 0) {
            existing_remote = &s_entries[index];
            break;
        }
    }

    if (existing_remote != NULL) {
        *existing_remote = entry;
        ESP_LOGI(TAG, "Remote app accepted: id=%s name=%s type=%s installed=%d updated=1", entry.id, entry.name,
                 entry.type == APP_CENTER_ENTRY_FIRMWARE_APP ? "firmware-app" : "manifest-app",
                 entry.installed ? 1 : 0);
        return true;
    }

    if (s_entry_count >= APP_CENTER_MAX_ENTRIES) {
        return false;
    }

    s_entries[s_entry_count] = entry;
    ++s_entry_count;
    ESP_LOGI(TAG, "Remote app accepted: id=%s name=%s type=%s installed=%d updated=0", entry.id, entry.name,
             entry.type == APP_CENTER_ENTRY_FIRMWARE_APP ? "firmware-app" : "manifest-app", entry.installed ? 1 : 0);
    return true;
}

static esp_err_t app_center_parse_remote_list(const char *json, int json_len) {
    cJSON *root = NULL;
    cJSON *array = NULL;
    cJSON *item = NULL;
    int accepted = 0;

    if (json == NULL || json_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_ParseWithLength(json, (size_t)json_len);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (cJSON_IsArray(root)) {
        array = root;
    } else if (cJSON_IsObject(root)) {
        array = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (!cJSON_IsArray(array)) {
            array = cJSON_GetObjectItemCaseSensitive(root, "apps");
        }
        if (!cJSON_IsArray(array)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    cJSON_ArrayForEach(item, array) {
        if (app_center_add_remote_entry(item)) {
            ++accepted;
        }
    }

    if (accepted > 0) {
        ESP_LOGI(TAG, "Remote app list parsed: accepted=%d total_count=%d json_bytes=%d", accepted, s_entry_count,
                 json_len);
    } else {
        ESP_LOGW(TAG, "Remote app list parsed with no accepted apps: total_count=%d json_bytes=%d", s_entry_count,
                 json_len);
    }

    cJSON_Delete(root);
    return accepted > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t app_center_http_event_handler(esp_http_client_event_t *event) {
    app_center_http_buffer_t *ctx = (app_center_http_buffer_t *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA && ctx != NULL && event->data != NULL && event->data_len > 0) {
        int copy_len = event->data_len;
        if (ctx->length + copy_len >= ctx->capacity) {
            copy_len = ctx->capacity - ctx->length - 1;
        }
        if (copy_len > 0) {
            memcpy(ctx->buffer + ctx->length, event->data, (size_t)copy_len);
            ctx->length += copy_len;
            ctx->buffer[ctx->length] = '\0';
        }
    }

    return ESP_OK;
}

static void remote_fetch_task(void *arg) {
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    app_center_http_buffer_t buffer = {0};
    esp_err_t ret = ESP_OK;
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    int http_status = -1;
    int64_t content_length = -1;

    s_remote_fetch_running = true;
    if (app_center_generation_is_active(generation)) {
        (void)app_center_set_remote_status("Fetching remote apps...");
    }
    app_center_request_render_for_generation(generation);
    ESP_LOGI(TAG, "Remote app list fetch starting: %s", APP_CENTER_REMOTE_LIST_URL);

    if (APP_CENTER_REMOTE_LIST_URL[0] == '\0') {
        if (app_center_generation_is_active(generation)) {
            (void)app_center_set_remote_status("Remote URL not configured");
        }
        ESP_LOGW(TAG, "Remote app list URL is not configured");
        if (s_remote_fetch_task == current_task) {
            s_remote_fetch_running = false;
            s_remote_fetch_task = NULL;
        }
        app_center_request_render_for_generation(generation);
        vTaskDelete(NULL);
        return;
    }

    buffer.capacity = APP_CENTER_REMOTE_JSON_MAX_BYTES + 1;
    buffer.buffer = (char *)calloc((size_t)buffer.capacity, 1);
    if (buffer.buffer == NULL) {
        if (app_center_generation_is_active(generation)) {
            (void)app_center_set_remote_status("Remote list memory error");
        }
        ESP_LOGW(TAG, "Remote app list memory allocation failed: capacity=%d", buffer.capacity);
        if (s_remote_fetch_task == current_task) {
            s_remote_fetch_running = false;
            s_remote_fetch_task = NULL;
        }
        app_center_request_render_for_generation(generation);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = APP_CENTER_REMOTE_LIST_URL,
        .timeout_ms = APP_CENTER_HTTP_TIMEOUT_MS,
        .event_handler = app_center_http_event_handler,
        .user_data = &buffer,
        .keep_alive_enable = true,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGW(TAG, "Remote app list HTTP client init failed");
    } else {
        app_center_set_http_client(&s_remote_fetch_client, client);
        ret = esp_http_client_perform(client);
        http_status = esp_http_client_get_status_code(client);
        content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Remote app list HTTP result: ret=%s status=%d received=%d content_length=%lld",
                 esp_err_to_name(ret), http_status, buffer.length, (long long)content_length);
        if (ret == ESP_OK && http_status != 200) {
            ret = ESP_FAIL;
        }
        app_center_clear_http_client(&s_remote_fetch_client, client);
        esp_http_client_cleanup(client);
    }

    if (ret == ESP_OK && app_center_generation_is_active(generation)) {
        ret = app_center_parse_remote_list(buffer.buffer, buffer.length);
    } else if (!app_center_generation_is_active(generation)) {
        ret = ESP_ERR_INVALID_STATE;
    }

    if (ret == ESP_OK && app_center_generation_is_active(generation)) {
        (void)app_center_set_remote_status("Remote app catalog loaded");
        s_remote_fetch_failure_count = 0;
        ESP_LOGI(TAG, "Remote app list loaded: total_count=%d", s_entry_count);
    } else if (app_center_generation_is_active(generation)) {
        snprintf(s_remote_status, sizeof(s_remote_status), "Remote apps unavailable: %s", esp_err_to_name(ret));
        s_render_requested = true;
        ESP_LOGW(TAG, "Remote app list fetch failed: %s", esp_err_to_name(ret));
        s_remote_fetch_started = false;
        app_center_schedule_remote_retry();
    } else {
        ESP_LOGI(TAG, "Remote app list fetch ignored for stale App.Center generation");
    }

    free(buffer.buffer);
    if (s_remote_fetch_task == current_task) {
        s_remote_fetch_running = false;
        s_remote_fetch_task = NULL;
    }
    app_center_request_render_for_generation(generation);
    vTaskDelete(NULL);
}

static void app_center_start_remote_fetch_once(void) {
    if (!s_app_center_active) {
        return;
    }

    if (s_remote_fetch_started) {
        return;
    }

    if (wifi_has_credentials() != 1) {
        if (app_center_set_remote_status("Remote apps need Wi-Fi setup")) {
            ESP_LOGW(TAG, "Remote app list fetch waiting: Wi-Fi credentials missing");
        }
        return;
    }

    if (wifi_is_connected() != 1) {
        app_center_ensure_wifi_resume();
        if (app_center_set_remote_status("Waiting for Wi-Fi to load apps")) {
            ESP_LOGW(TAG, "Remote app list fetch waiting: Wi-Fi not connected");
        }
        return;
    }

    if (APP_CENTER_REMOTE_LIST_URL[0] == '\0') {
        if (app_center_set_remote_status("Remote URL not configured")) {
            ESP_LOGW(TAG, "Remote app list fetch skipped: URL not configured");
        }
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_remote_retry_after_tick != 0 && now < s_remote_retry_after_tick) {
        uint32_t retry_ms = (uint32_t)pdTICKS_TO_MS(s_remote_retry_after_tick - now);
        snprintf(s_remote_status, sizeof(s_remote_status), "Retrying remote apps in %lus",
                 (unsigned long)((retry_ms + 999U) / 1000U));
        s_render_requested = true;
        if (retry_ms % 1000U < APP_CENTER_WAIT_DELAY_MS) {
            ESP_LOGW(TAG, "Remote app list fetch throttled: retry_in_ms=%lu", (unsigned long)retry_ms);
        }
        return;
    }
    s_remote_retry_after_tick = 0;

    s_remote_fetch_started = true;
    uint32_t generation = s_app_center_generation;
    ESP_LOGI(TAG, "Remote app list task starting: generation=%lu url=%s", (unsigned long)generation,
             APP_CENTER_REMOTE_LIST_URL);
    if (xTaskCreate(remote_fetch_task, "app_remote_list", 6144, (void *)(uintptr_t)generation, 4,
                    &s_remote_fetch_task) != pdPASS) {
        s_remote_fetch_task = NULL;
        s_remote_fetch_running = false;
        s_remote_fetch_started = false;
        app_center_schedule_remote_retry();
        (void)app_center_set_remote_status("Remote list task failed");
        ESP_LOGW(TAG, "Remote app list task start failed");
    } else {
        ESP_LOGI(TAG, "Remote app list task started");
    }
}

static void app_center_clear_group(void) {
    if (s_group != NULL) {
        lv_group_del(s_group);
        s_group = NULL;
    }
}

static void app_center_forget_screen_locked(void) {
    app_center_clear_group();
    if (s_exit_button != NULL && lv_obj_is_valid(s_exit_button)) {
        lv_obj_del(s_exit_button);
    }
    s_screen = NULL;
    s_status_panel = NULL;
    s_status_icon = NULL;
    s_status_title_label = NULL;
    s_status_body_label = NULL;
    s_install_status_label = NULL;
    s_install_progress_bar = NULL;
    s_install_progress_label = NULL;
    s_list_title_label = NULL;
    s_list_status_label = NULL;
    s_list_card_name_label = NULL;
    s_list_card_state_label = NULL;
    s_detail_panel = NULL;
    s_detail_name_label = NULL;
    s_detail_state_label = NULL;
    s_detail_body_label = NULL;
    s_detail_meta_label = NULL;
    s_detail_primary_button = NULL;
    s_detail_secondary_button = NULL;
    s_download_cancel_button = NULL;
    s_exit_button = NULL;
    memset(s_entry_cards, 0, sizeof(s_entry_cards));
    memset(s_focus_proxies, 0, sizeof(s_focus_proxies));
    s_exit_hide_at_tick = 0;
}

static void app_center_open_wifi_gate_base(void) {
    lvgl_port_lock(0);
    app_center_forget_screen_locked();
    lvgl_port_unlock();

    if (s_deps.open_wifi_gate_base_ui != NULL) {
        s_deps.open_wifi_gate_base_ui();
    }
}

static void app_center_clear_wifi_gate(void) {
    if (s_deps.clear_wifi_gate != NULL) {
        s_deps.clear_wifi_gate();
    }
    s_wifi_gate_active = false;
}

static bool app_center_show_wifi_gate_if_needed(void) {
    if (!app_center_wifi_gate_needed()) {
        if (s_wifi_gate_active) {
            app_center_clear_wifi_gate();
        }
        return false;
    }

    if (!s_wifi_gate_active) {
        app_center_open_wifi_gate_base();
        s_wifi_gate_active = true;
    }
    if (s_deps.show_wifi_gate != NULL) {
        (void)s_deps.show_wifi_gate("App.Center");
    }
    return true;
}

static bool app_center_handle_wifi_gate_action(const char *reason) {
    return s_wifi_gate_active && s_deps.handle_wifi_gate_action != NULL && s_deps.handle_wifi_gate_action(reason);
}

static void app_center_focus_obj_locked(lv_obj_t *obj) {
    lv_indev_t *indev = NULL;

    if (obj == NULL) {
        return;
    }

    if (s_group == NULL) {
        s_group = lv_group_create();
        if (s_group == NULL) {
            ESP_LOGW(TAG, "Failed to create App.Center input group");
            return;
        }
        lv_group_set_wrap(s_group, true);
    }

    lv_group_add_obj(s_group, obj);
    if (lv_group_get_focused(s_group) == NULL) {
        lv_group_focus_obj(obj);
    }

    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (indev->driver != NULL && indev->driver->type == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(indev, s_group);
        }
    }
}

static void app_center_select_index(int index, int direction) {
    if (index < 0 || index >= s_entry_count || index == s_selected_index || s_list_animating) {
        ESP_LOGD(TAG, "App.Center select ignored: index=%d selected=%d count=%d animating=%d", index, s_selected_index,
                 s_entry_count, s_list_animating ? 1 : 0);
        return;
    }

    s_previous_index = s_selected_index;
    s_selected_index = index;
    s_transition_dir = direction != 0 ? direction : (index > s_previous_index ? 1 : -1);
    ESP_LOGI(TAG, "App.Center selected index=%d id=%s direction=%d previous=%d", s_selected_index,
             s_entries[s_selected_index].id, direction, s_previous_index);
    if (s_page == APP_CENTER_PAGE_LIST && s_group != NULL && s_entry_cards[index] != NULL) {
        app_center_update_list_selection_locked();
        lv_group_focus_obj(s_entry_cards[index]);
    } else {
        s_render_requested = true;
    }
}

static void app_center_select_relative(int direction) {
    int next;
    if (s_entry_count <= 1 || s_list_animating) {
        return;
    }

    next = (s_selected_index + direction) % s_entry_count;
    if (next < 0) {
        next += s_entry_count;
    }
    if (next == s_selected_index) {
        return;
    }
    app_center_select_index(next, direction);
}

static void app_center_request_download(int index) {
    const char *url = NULL;

    if (index < 0 || index >= s_entry_count) {
        ESP_LOGW(TAG, "App.Center download ignored: index=%d count=%d", index, s_entry_count);
        return;
    }
    url = s_entries[index].type == APP_CENTER_ENTRY_FIRMWARE_APP ? s_entries[index].firmware_url
                                                                 : s_entries[index].package_url;
    if (url == NULL || url[0] == '\0') {
        ESP_LOGW(TAG, "App.Center download missing url: index=%d id=%s", index, s_entries[index].id);
        safe_copy(s_remote_status, sizeof(s_remote_status), "Configure App.Center source first");
        safe_copy(s_download_status, sizeof(s_download_status),
                  s_entries[index].type == APP_CENTER_ENTRY_FIRMWARE_APP ? "Firmware URL missing"
                                                                         : "Package URL missing");
        s_page = APP_CENTER_PAGE_DETAIL;
        s_render_requested = true;
        return;
    }

    ESP_LOGI(TAG, "App.Center download requested: index=%d id=%s name=%s type=%d url=%s", index, s_entries[index].id,
             s_entries[index].name, (int)s_entries[index].type, url);
    s_uninstall_confirm_index = -1;
    s_selected_index = index;
    s_page = APP_CENTER_PAGE_INSTALL;
    s_download_progress = 0;
    s_download_finished = false;
    s_download_result = ESP_OK;
    safe_copy(s_download_status, sizeof(s_download_status), "Starting download...");
    s_pending_download_open = true;
    s_render_requested = true;
}

static void list_entry_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if (index < 0 || index >= s_entry_count) {
        return;
    }

    if (code == LV_EVENT_FOCUSED) {
        if (index != s_selected_index) {
            s_selected_index = index;
            app_center_update_list_selection_locked();
        }
        return;
    }

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    s_selected_index = index;
    s_action_index = index;
    s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
    s_uninstall_confirm_index = -1;
    s_page = APP_CENTER_PAGE_DETAIL;
    s_render_requested = true;
    ESP_LOGI(TAG, "App.Center list clicked: index=%d id=%s installed=%d", index, s_entries[index].id,
             s_entries[index].installed ? 1 : 0);
}

static void app_center_screen_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP && app_center_exit_policy_gesture_reveals_exit(app_center_exit_page_from_current())) {
        app_center_show_exit_locked();
    } else if (s_page == APP_CENTER_PAGE_LIST && dir == LV_DIR_BOTTOM) {
        app_center_select_relative(-1);
    }
}

static void list_key_event_cb(lv_event_t *event) {
    uint32_t key;
    if (lv_event_get_code(event) != LV_EVENT_KEY || s_page != APP_CENTER_PAGE_LIST) {
        return;
    }

    key = *((uint32_t *)lv_event_get_param(event));
    if (key == LV_KEY_NEXT || key == LV_KEY_RIGHT || key == LV_KEY_DOWN) {
        app_center_select_relative(1);
    } else if (key == LV_KEY_PREV || key == LV_KEY_LEFT || key == LV_KEY_UP) {
        app_center_select_relative(-1);
    } else if (key == LV_KEY_ENTER) {
        s_action_index = s_selected_index;
        s_page = APP_CENTER_PAGE_DETAIL;
        s_uninstall_confirm_index = -1;
        s_render_requested = true;
        ESP_LOGI(TAG, "App.Center enter detail by encoder: index=%d id=%s installed=%d", s_action_index,
                 s_entries[s_action_index].id, s_entries[s_action_index].installed ? 1 : 0);
    }
}

static void detail_back_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED) {
        s_detail_action = APP_CENTER_DETAIL_ACTION_SECONDARY;
        return;
    }
    if (code == LV_EVENT_CLICKED) {
        app_center_ignore_button_clicks(350);
        s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
        ESP_LOGI(TAG, "App.Center secondary clicked: index=%d installed=%d", s_action_index,
                 (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) ? 1
                                                                                                                : 0);
        if (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) {
            if (s_uninstall_confirm_index == s_action_index) {
                (void)app_center_uninstall_entry(s_action_index);
            } else {
                s_uninstall_confirm_index = s_action_index;
                app_center_set_remote_status("Tap Confirm remove to delete");
                s_render_requested = true;
            }
        } else {
            s_uninstall_confirm_index = -1;
            s_page = APP_CENTER_PAGE_LIST;
            s_render_requested = true;
        }
    }
}

static void detail_primary_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED) {
        s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    app_center_ignore_button_clicks(350);
    ESP_LOGI(TAG, "App.Center primary clicked: index=%d installed=%d", s_action_index,
             (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) ? 1 : 0);
    if (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) {
        if (s_uninstall_confirm_index == s_action_index) {
            s_uninstall_confirm_index = -1;
            app_center_set_remote_status("Remove canceled");
            s_render_requested = true;
            return;
        }
        app_center_open_entry(s_action_index);
    } else {
        s_uninstall_confirm_index = -1;
        app_center_request_download(s_action_index);
    }
}

static void download_cancel_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_download_running) {
        app_center_request_download_cancel("install cancel button");
        return;
    }

    s_page = APP_CENTER_PAGE_LIST;
    s_render_requested = true;
}

static void app_center_add_button_label(lv_obj_t *button, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

static lv_obj_t *app_center_create_button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
                                          const char *text) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2D3748), 0);
    app_center_add_button_label(button, text);
    return button;
}

static bool app_center_text_has_prefix(const char *text, const char *prefix) {
    if (text == NULL || prefix == NULL) {
        return false;
    }
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int app_center_clamp_progress(int progress) {
    if (progress < 0) {
        return 0;
    }
    if (progress > 100) {
        return 100;
    }
    return progress;
}

static uint32_t app_center_download_status_color(void) {
    if (strstr(s_download_status, "failed") != NULL || strstr(s_download_status, "mismatch") != NULL ||
        strstr(s_download_status, "invalid") != NULL) {
        return APP_CENTER_STYLE_RED;
    }
    if (strstr(s_download_status, "Cancel") != NULL || strstr(s_download_status, "cancel") != NULL) {
        return APP_CENTER_STYLE_AMBER;
    }
    return APP_CENTER_STYLE_GREEN;
}

static const char *app_center_remote_state_accent(void) {
    if (s_remote_fetch_running || strcmp(s_remote_status, "Remote list not loaded") == 0 ||
        strcmp(s_remote_status, "Wi-Fi ready. Loading apps...") == 0 ||
        strcmp(s_remote_status, "Fetching remote apps...") == 0) {
        return "Loading apps";
    }
    if (app_center_text_has_prefix(s_remote_status, "Retrying remote apps")) {
        return "Retrying apps";
    }
    if (app_center_text_has_prefix(s_remote_status, "Remote apps unavailable")) {
        return "Catalog unavailable";
    }
    if (strcmp(s_remote_status, "Remote apps need Wi-Fi setup") == 0) {
        return "Wi-Fi needed";
    }
    if (strcmp(s_remote_status, "Waiting for Wi-Fi to load apps") == 0) {
        return "Waiting for Wi-Fi";
    }
    if (strcmp(s_remote_status, "Remote URL not configured") == 0 ||
        strcmp(s_remote_status, "Configure App.Center source first") == 0) {
        return "Catalog not configured";
    }
    if (strcmp(s_remote_status, "Remote list memory error") == 0 ||
        strcmp(s_remote_status, "Remote list task failed") == 0) {
        return "Catalog unavailable";
    }
    if (s_remote_status[0] != '\0' && strcmp(s_remote_status, "Remote app catalog loaded") != 0) {
        return s_remote_status;
    }
    return "No apps available";
}

static const char *app_center_remote_state_body(void) {
    if (s_remote_fetch_running || strcmp(s_remote_status, "Remote list not loaded") == 0 ||
        strcmp(s_remote_status, "Wi-Fi ready. Loading apps...") == 0 ||
        strcmp(s_remote_status, "Fetching remote apps...") == 0) {
        return "Fetching catalog over Wi-Fi.";
    }
    if (app_center_text_has_prefix(s_remote_status, "Retrying remote apps")) {
        return "Keep Wi-Fi connected while retrying.";
    }
    if (app_center_text_has_prefix(s_remote_status, "Remote apps unavailable")) {
        return s_remote_status;
    }
    if (strcmp(s_remote_status, "Remote apps need Wi-Fi setup") == 0) {
        return "Set up Wi-Fi before loading apps.";
    }
    if (strcmp(s_remote_status, "Waiting for Wi-Fi to load apps") == 0) {
        return "Connect Wi-Fi to load App.Center.";
    }
    if (strcmp(s_remote_status, "Remote URL not configured") == 0 ||
        strcmp(s_remote_status, "Configure App.Center source first") == 0) {
        return "Configure the app catalog URL.";
    }
    if (strcmp(s_remote_status, "Remote list memory error") == 0 ||
        strcmp(s_remote_status, "Remote list task failed") == 0) {
        return s_remote_status;
    }
    return "No downloadable apps were found.";
}

static uint32_t app_center_remote_state_accent_color(void) {
    if (app_center_text_has_prefix(s_remote_status, "Retrying remote apps") ||
        app_center_text_has_prefix(s_remote_status, "Remote apps unavailable") ||
        strcmp(s_remote_status, "Remote apps need Wi-Fi setup") == 0 ||
        strcmp(s_remote_status, "Waiting for Wi-Fi to load apps") == 0 ||
        strcmp(s_remote_status, "Remote URL not configured") == 0 ||
        strcmp(s_remote_status, "Configure App.Center source first") == 0 ||
        strcmp(s_remote_status, "Remote list memory error") == 0 ||
        strcmp(s_remote_status, "Remote list task failed") == 0) {
        return APP_CENTER_STYLE_AMBER;
    }
    return APP_CENTER_STYLE_GREEN;
}

static const char *app_center_entry_download_url(const app_center_entry_t *entry) {
    if (entry == NULL) {
        return "";
    }
    return entry->type == APP_CENTER_ENTRY_FIRMWARE_APP ? entry->firmware_url : entry->package_url;
}

static bool app_center_entry_source_missing(const app_center_entry_t *entry) {
    const char *url = app_center_entry_download_url(entry);
    return entry != NULL && !entry->installed && (url == NULL || url[0] == '\0');
}

static const char *app_center_entry_state_text(const app_center_entry_t *entry) {
    if (entry == NULL) {
        return "Unavailable";
    }
    if (entry->installed) {
        return "Installed";
    }
    if (app_center_entry_source_missing(entry)) {
        return "Source needed";
    }
    return "Ready to download";
}

static uint32_t app_center_entry_state_color(const app_center_entry_t *entry) {
    if (app_center_entry_source_missing(entry)) {
        return APP_CENTER_STYLE_AMBER;
    }
    return APP_CENTER_STYLE_GREEN;
}

static const char *app_center_list_status_text(void) {
    if (strcmp(s_remote_status, "Remote app catalog loaded") == 0) {
        return "Catalog loaded";
    }
    return app_center_remote_state_accent();
}

static lv_obj_t *app_center_create_status_panel_locked(void) {
    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_size(panel, APP_CENTER_STATUS_PANEL_W, APP_CENTER_STATUS_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, APP_CENTER_STATUS_PANEL_Y);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

static lv_obj_t *app_center_create_status_label_locked(lv_obj_t *parent, const char *text, lv_coord_t y,
                                                       const lv_font_t *font, uint32_t color,
                                                       lv_label_long_mode_t long_mode) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_width(label, APP_CENTER_STATUS_TEXT_W);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
    return label;
}

static lv_obj_t *app_center_create_status_button_locked(const char *text, lv_coord_t y, lv_event_cb_t event_cb) {
    lv_obj_t *button = lv_btn_create(s_screen);
    lv_obj_set_size(button, APP_CENTER_STATUS_BUTTON_W, APP_CENTER_STATUS_BUTTON_H);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, y);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 23, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_CENTER_STYLE_PANEL), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(APP_CENTER_STYLE_GREEN), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    if (event_cb != NULL) {
        lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(APP_CENTER_STYLE_WHITE), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

static void app_center_update_progress_text_locked(void) {
    if (s_install_progress_label != NULL) {
        char progress_text[16];
        snprintf(progress_text, sizeof(progress_text), "%d%%", app_center_clamp_progress(s_download_progress));
        lv_label_set_text(s_install_progress_label, progress_text);
    }
}

static lv_obj_t *app_center_render_status_page_locked(const char *title, const char *accent, uint32_t accent_color,
                                                      const char *body, bool show_progress, int progress,
                                                      const char *button_text, lv_event_cb_t button_cb) {
    lv_obj_t *button = NULL;
    lv_obj_t *panel = app_center_create_status_panel_locked();
    s_status_panel = panel;

    lv_obj_t *icon = lv_img_create(panel);
    s_status_icon = icon;
    lv_img_set_src(icon, &ui_img_task_template_png);
    lv_img_set_zoom(icon, APP_CENTER_STATUS_ICON_ZOOM);
    lv_obj_set_style_img_recolor(icon, lv_color_hex(APP_CENTER_STYLE_GREEN), 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_70, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, APP_CENTER_STATUS_ICON_Y);

    s_status_title_label = app_center_create_status_label_locked(
        panel, title, APP_CENTER_STATUS_TITLE_Y, &lv_font_montserrat_24, APP_CENTER_STYLE_WHITE, LV_LABEL_LONG_DOT);
    s_install_status_label = app_center_create_status_label_locked(
        panel, accent, APP_CENTER_STATUS_ACCENT_Y, &lv_font_montserrat_20, accent_color, LV_LABEL_LONG_DOT);

    if (body != NULL && body[0] != '\0') {
        s_status_body_label = app_center_create_status_label_locked(
            panel, body, APP_CENTER_STATUS_BODY_Y, &lv_font_montserrat_20, APP_CENTER_STYLE_WHITE, LV_LABEL_LONG_WRAP);
    }

    if (show_progress) {
        s_install_progress_bar = lv_bar_create(s_screen);
        lv_obj_set_size(s_install_progress_bar, APP_CENTER_STATUS_PROGRESS_W, APP_CENTER_STATUS_PROGRESS_H);
        lv_obj_align(s_install_progress_bar, LV_ALIGN_CENTER, 0, APP_CENTER_STATUS_PROGRESS_Y);
        lv_obj_set_style_radius(s_install_progress_bar, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_install_progress_bar, lv_color_hex(APP_CENTER_STYLE_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_install_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_install_progress_bar, 6, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_install_progress_bar, lv_color_hex(APP_CENTER_STYLE_GREEN), LV_PART_INDICATOR);
        lv_bar_set_range(s_install_progress_bar, 0, 100);
        lv_bar_set_value(s_install_progress_bar, app_center_clamp_progress(progress), LV_ANIM_OFF);

        s_install_progress_label =
            app_center_create_status_label_locked(s_screen, "", APP_CENTER_STATUS_PROGRESS_LABEL_Y,
                                                  &lv_font_montserrat_20, APP_CENTER_STYLE_GREEN, LV_LABEL_LONG_DOT);
        app_center_update_progress_text_locked();
    }

    if (button_text != NULL && button_text[0] != '\0') {
        button = app_center_create_status_button_locked(button_text, APP_CENTER_STATUS_BUTTON_Y, button_cb);
    }

    return button;
}

static void app_center_render_remote_status_locked(void) {
    (void)app_center_render_status_page_locked("App.Center", app_center_remote_state_accent(),
                                               app_center_remote_state_accent_color(), app_center_remote_state_body(),
                                               false, 0, NULL, NULL);
}

static void app_center_prepare_screen_locked(void) {
    lv_obj_t *old_screen = NULL;

    if (s_screen != NULL) {
        lv_obj_clean(s_screen);
        app_center_clear_group();
        return;
    }

    old_screen = boot_anim_get_screen();
    if (old_screen == NULL) {
        old_screen = lv_disp_get_scr_act(NULL);
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x080A0D), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_screen, app_center_screen_event_cb, LV_EVENT_GESTURE, NULL);

    lv_disp_load_scr(s_screen);
    if (old_screen != NULL && old_screen != s_screen) {
        lv_obj_del(old_screen);
    }
}

static void card_y_anim_cb(void *obj, int32_t value) {
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void card_delete_ready_cb(lv_anim_t *anim) {
    lv_obj_t *obj = (lv_obj_t *)anim->var;
    if (obj != NULL) {
        lv_obj_del_async(obj);
    }
}

static void card_enter_ready_cb(lv_anim_t *anim) {
    (void)anim;
    s_list_animating = false;
}

static void app_center_start_card_anim(lv_obj_t *obj, int from_y, int to_y, lv_anim_ready_cb_t ready_cb) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, from_y, to_y);
    lv_anim_set_time(&anim, APP_CENTER_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, card_y_anim_cb);
    if (ready_cb != NULL) {
        lv_anim_set_ready_cb(&anim, ready_cb);
    }
    lv_anim_start(&anim);
}

static void app_center_render_header_locked(const char *subtitle) {
    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "App.Center");
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(title, 180, 30);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_CENTER_STYLE_WHITE), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *sub = lv_label_create(s_screen);
    lv_label_set_text(sub, subtitle != NULL ? subtitle : "");
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_obj_set_size(sub, APP_CENTER_LIST_STATUS_W, 24);
    lv_obj_set_style_text_color(sub, lv_color_hex(APP_CENTER_STYLE_GREEN), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

static void app_center_style_entry_card_locked(lv_obj_t *button, int index) {
    const app_center_entry_t *entry = &s_entries[index];
    bool selected = index == s_selected_index;
    uint32_t color = entry->installed ? 0x1C4C3A : 0x12396B;
    uint32_t selected_color = entry->installed ? 0x267A58 : 0x175AA6;
    uint32_t border = entry->installed ? 0x67D48A : 0x5AA2FF;
    uint32_t selected_border = entry->installed ? 0x75FFC2 : 0x7FDBFF;

    if (button == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(button, lv_color_hex(selected ? selected_color : color), 0);
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(selected ? selected_border : border), 0);
    lv_obj_set_style_border_width(button, selected ? 4 : 1, 0);
    lv_obj_set_style_shadow_width(button, selected ? 22 : 6, 0);
    lv_obj_set_style_shadow_opa(button, selected ? LV_OPA_50 : LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(selected ? selected_border : border), 0);
}

static void app_center_update_list_selection_locked(void) {
    for (int index = 0; index < s_entry_count; ++index) {
        app_center_style_entry_card_locked(s_entry_cards[index], index);
    }
}

static lv_obj_t *app_center_create_entry_card_locked(int index, int y) {
    const app_center_entry_t *entry = &s_entries[index];
    uint32_t color = entry->installed ? 0x1C4C3A : 0x12396B;
    lv_obj_t *button = app_center_create_button(s_screen, 0, 0, APP_CENTER_CARD_W, APP_CENTER_CARD_H, color, "");

    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(button, list_entry_event_cb, LV_EVENT_FOCUSED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(button, list_entry_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    lv_obj_add_event_cb(button, list_key_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_set_style_radius(button, 28, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    app_center_style_entry_card_locked(button, index);
    app_center_focus_obj_locked(button);
    s_entry_cards[index] = button;

    lv_obj_t *name = lv_label_create(button);
    s_list_card_name_label = name;
    lv_label_set_text(name, entry->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, APP_CENTER_CARD_TEXT_W, 30);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_CENTER_STYLE_WHITE), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, APP_CENTER_CARD_TEXT_X, APP_CENTER_CARD_NAME_Y);

    lv_obj_t *state = lv_label_create(button);
    s_list_card_state_label = state;
    lv_label_set_text(state, app_center_entry_state_text(entry));
    lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
    lv_obj_set_size(state, APP_CENTER_CARD_TEXT_W, 24);
    lv_obj_set_style_text_color(state, lv_color_hex(app_center_entry_state_color(entry)), 0);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(state, LV_ALIGN_TOP_LEFT, APP_CENTER_CARD_TEXT_X, APP_CENTER_CARD_STATE_Y);

    return button;
}

static void app_center_create_focus_proxies_locked(void) {
    memset(s_focus_proxies, 0, sizeof(s_focus_proxies));
    if (s_selected_index >= 0 && s_selected_index < s_entry_count && s_entry_cards[s_selected_index] != NULL &&
        s_group != NULL) {
        lv_group_focus_obj(s_entry_cards[s_selected_index]);
    }
}

static void app_center_render_list_locked(void) {
    app_center_prepare_screen_locked();
    memset(s_entry_cards, 0, sizeof(s_entry_cards));

    if (s_entry_count <= 0) {
        app_center_render_remote_status_locked();
        return;
    }

    lv_obj_t *title = lv_label_create(s_screen);
    s_list_title_label = title;
    lv_label_set_text(title, "App.Center");
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(title, 180, 30);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_CENTER_STYLE_WHITE), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, APP_CENTER_LIST_TITLE_Y);

    lv_obj_t *remote = lv_label_create(s_screen);
    s_list_status_label = remote;
    lv_label_set_text(remote, app_center_list_status_text());
    lv_label_set_long_mode(remote, LV_LABEL_LONG_DOT);
    lv_obj_set_size(remote, APP_CENTER_LIST_STATUS_W, 24);
    lv_obj_set_style_text_color(remote, lv_color_hex(app_center_remote_state_accent_color()), 0);
    lv_obj_set_style_text_font(remote, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(remote, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(remote, LV_ALIGN_TOP_MID, 0, APP_CENTER_LIST_STATUS_Y);

    if (s_transition_dir != 0 && s_previous_index >= 0 && s_previous_index < s_entry_count) {
        int dir = s_transition_dir;
        lv_obj_t *old_card = app_center_create_entry_card_locked(s_previous_index, APP_CENTER_CARD_Y);
        lv_obj_t *new_card =
            app_center_create_entry_card_locked(s_selected_index, APP_CENTER_CARD_Y + dir * APP_CENTER_CARD_TRAVEL);
        s_list_animating = true;
        app_center_start_card_anim(old_card, APP_CENTER_CARD_Y, APP_CENTER_CARD_Y - dir * APP_CENTER_CARD_TRAVEL,
                                   card_delete_ready_cb);
        app_center_start_card_anim(new_card, APP_CENTER_CARD_Y + dir * APP_CENTER_CARD_TRAVEL, APP_CENTER_CARD_Y,
                                   card_enter_ready_cb);
        s_transition_dir = 0;
        s_previous_index = -1;
    } else {
        app_center_create_entry_card_locked(s_selected_index, APP_CENTER_CARD_Y);
    }

    app_center_create_focus_proxies_locked();
}

static void app_center_render_detail_locked(void) {
    if (s_action_index < 0 || s_action_index >= s_entry_count) {
        s_action_index = s_selected_index;
    }

    const app_center_entry_t *entry = &s_entries[s_action_index];
    bool confirm_remove = entry->installed && s_uninstall_confirm_index == s_action_index;
    bool source_missing = app_center_entry_source_missing(entry);
    const char *primary = entry->installed ? "Open" : (source_missing ? "Need URL" : "Download");
    const char *secondary = entry->installed ? (confirm_remove ? "Confirm remove" : "Uninstall") : "Cancel";

    app_center_prepare_screen_locked();
    app_center_render_header_locked("App details");
    s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;

    lv_obj_t *panel = lv_obj_create(s_screen);
    s_detail_panel = panel;
    lv_obj_set_size(panel, 236, 194);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 14);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111923), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(entry->installed ? 0x75FFC2 : 0x7FDBFF), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    lv_obj_t *name = lv_label_create(panel);
    s_detail_name_label = name;
    lv_label_set_text(name, entry->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, APP_CENTER_DETAIL_TEXT_W, 30);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_CENTER_STYLE_WHITE), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, APP_CENTER_DETAIL_TEXT_X, APP_CENTER_DETAIL_NAME_Y);

    lv_obj_t *state = lv_label_create(panel);
    s_detail_state_label = state;
    lv_label_set_text(state, app_center_entry_state_text(entry));
    lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
    lv_obj_set_size(state, APP_CENTER_DETAIL_TEXT_W, 24);
    lv_obj_set_style_text_color(state, lv_color_hex(app_center_entry_state_color(entry)), 0);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(state, LV_ALIGN_TOP_LEFT, APP_CENTER_DETAIL_TEXT_X, APP_CENTER_DETAIL_STATE_Y);

    lv_obj_t *detail = lv_label_create(panel);
    s_detail_body_label = detail;
    lv_label_set_text(detail, entry->description[0] != '\0' ? entry->description : "No description");
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(detail, APP_CENTER_DETAIL_TEXT_W, APP_CENTER_DETAIL_BODY_H);
    lv_obj_set_style_text_color(detail, lv_color_hex(APP_CENTER_STYLE_MUTED), 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, APP_CENTER_DETAIL_TEXT_X, APP_CENTER_DETAIL_BODY_Y);

    lv_obj_t *version = lv_label_create(panel);
    s_detail_meta_label = version;
    if (source_missing) {
        lv_label_set_text(version, "Configure apps.json source");
    } else {
        lv_label_set_text_fmt(version, "Version %s", entry->version[0] != '\0' ? entry->version : "-");
    }
    lv_label_set_long_mode(version, LV_LABEL_LONG_DOT);
    lv_obj_set_size(version, APP_CENTER_DETAIL_TEXT_W, 18);
    lv_obj_set_style_text_color(version, lv_color_hex(APP_CENTER_STYLE_DIM), 0);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(version, LV_ALIGN_TOP_LEFT, APP_CENTER_DETAIL_TEXT_X, APP_CENTER_DETAIL_META_Y);

    lv_obj_t *primary_btn = app_center_create_button(panel, 0, 0, confirm_remove ? 76 : 92, 40,
                                                     source_missing ? 0x39414C : 0x2D7FF9, primary);
    lv_obj_align(primary_btn, LV_ALIGN_BOTTOM_MID, confirm_remove ? -62 : -50, -14);
    lv_obj_add_event_cb(primary_btn, detail_primary_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(primary_btn, detail_primary_event_cb, LV_EVENT_CLICKED, NULL);
    s_detail_primary_button = primary_btn;

    lv_obj_t *back_btn = app_center_create_button(panel, 0, 0, confirm_remove ? 122 : 92, 40,
                                                  entry->installed ? 0x8A2E3A : 0x2A3038, secondary);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, confirm_remove ? 42 : 50, -14);
    lv_obj_add_event_cb(back_btn, detail_back_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(back_btn, detail_back_event_cb, LV_EVENT_CLICKED, NULL);
    s_detail_secondary_button = back_btn;
    app_center_focus_obj_locked(primary_btn);
}

static void app_center_render_install_locked(void) {
    if (s_action_index < 0 || s_action_index >= s_entry_count) {
        s_action_index = s_selected_index;
    }

    const app_center_entry_t *entry = &s_entries[s_action_index];

    app_center_prepare_screen_locked();
    s_download_cancel_button = app_center_render_status_page_locked(
        entry->name, s_download_status, app_center_download_status_color(), "Keep device powered on.", true,
        s_download_progress, s_download_running ? "Cancel" : "Back", download_cancel_event_cb);
    app_center_focus_obj_locked(s_download_cancel_button);
}

static void app_center_render_locked(void) {
    if (s_selected_index < 0 || s_selected_index >= s_entry_count) {
        s_selected_index = 0;
    }
    if (s_action_index < 0 || s_action_index >= s_entry_count) {
        s_action_index = s_selected_index;
    }

    s_install_status_label = NULL;
    s_install_progress_bar = NULL;
    s_install_progress_label = NULL;
    s_status_panel = NULL;
    s_status_icon = NULL;
    s_status_title_label = NULL;
    s_status_body_label = NULL;
    s_list_title_label = NULL;
    s_list_status_label = NULL;
    s_list_card_name_label = NULL;
    s_list_card_state_label = NULL;
    s_detail_panel = NULL;
    s_detail_name_label = NULL;
    s_detail_state_label = NULL;
    s_detail_body_label = NULL;
    s_detail_meta_label = NULL;
    s_detail_primary_button = NULL;
    s_detail_secondary_button = NULL;
    s_download_cancel_button = NULL;

    if (s_page == APP_CENTER_PAGE_DETAIL) {
        app_center_render_detail_locked();
    } else if (s_page == APP_CENTER_PAGE_INSTALL) {
        app_center_render_install_locked();
    } else {
        app_center_render_list_locked();
    }
}

static void app_center_update_install_locked(void) {
    if (s_install_progress_bar != NULL) {
        lv_bar_set_value(s_install_progress_bar, s_download_progress, LV_ANIM_OFF);
    }
    if (s_install_status_label != NULL) {
        lv_label_set_text(s_install_status_label, s_download_status);
        lv_obj_set_style_text_color(s_install_status_label, lv_color_hex(app_center_download_status_color()), 0);
    }
    app_center_update_progress_text_locked();
}

static esp_err_t app_center_validate_package_manifest(const char *path, const char *expected_package_id) {
    FILE *file = NULL;
    char *buffer = NULL;
    long length = 0;
    cJSON *root = NULL;
    cJSON *compat = NULL;
    const char *manifest_id = NULL;
    const char *canonical_manifest_id = NULL;
    const char *name = NULL;
    const char *runtime_app_id = NULL;
    const char *resource_mode = NULL;
    const char *compat_product = NULL;
    cJSON *permissions = NULL;
    cJSON *signature = NULL;
    esp_err_t ret = ESP_OK;

    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Package manifest open failed: %s", path);
        return ESP_FAIL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    length = ftell(file);
    if (length <= 0 || length > APP_CENTER_PACKAGE_MANIFEST_MAX_BYTES) {
        ESP_LOGW(TAG, "Package manifest size invalid: %ld path=%s", length, path);
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    buffer = (char *)calloc((size_t)length + 1, 1);
    if (buffer == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        ret = ESP_FAIL;
        goto cleanup;
    }

    root = cJSON_ParseWithLength(buffer, (size_t)length);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Package manifest is not a JSON object: %s", path);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    name = json_get_string(root, "name", "title", "appName");
    if (name == NULL || name[0] == '\0') {
        ESP_LOGW(TAG, "Package manifest missing name/title/appName: %s", path);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    manifest_id = app_center_json_string(root, "id");
    if (manifest_id[0] == '\0') {
        manifest_id = app_center_json_string(root, "appId");
    }
    if (manifest_id[0] == '\0') {
        manifest_id = app_center_json_string(root, "app_id");
    }
    runtime_app_id = app_center_json_string(root, "runtimeAppId");
    if (runtime_app_id[0] == '\0') {
        runtime_app_id = app_center_json_string(root, "runtime_app_id");
    }
    resource_mode = app_center_json_string(root, "resourceMode");
    if (resource_mode[0] == '\0') {
        resource_mode = app_center_json_string(root, "resource_mode");
    }
    if (app_center_legacy_espnow_requested(manifest_id, name) || app_center_legacy_espnow_id_matches(runtime_app_id) ||
        strcmp(resource_mode, "espnow") == 0 || strcmp(resource_mode, "esp-now") == 0) {
        ESP_LOGW(TAG, "Package manifest legacy remote app is not supported: id=%s name=%s path=%s",
                 manifest_id[0] != '\0' ? manifest_id : "(none)", name, path);
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }
    if (expected_package_id != NULL && expected_package_id[0] != '\0') {
        if (manifest_id[0] == '\0') {
            ESP_LOGW(TAG, "Package manifest missing id/appId/app_id: %s", path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        canonical_manifest_id = app_center_canonical_package_id(manifest_id, name);
        if (strcmp(canonical_manifest_id, expected_package_id) != 0) {
            ESP_LOGW(TAG, "Package id mismatch: manifest=%s canonical=%s expected=%s path=%s", manifest_id,
                     canonical_manifest_id, expected_package_id, path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
    }

    compat = cJSON_GetObjectItemCaseSensitive(root, "compat");
    if (compat != NULL) {
        if (!cJSON_IsObject(compat)) {
            ESP_LOGW(TAG, "Package manifest compat must be an object: %s", path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        compat_product = app_center_json_string(compat, "product");
        if (compat_product[0] == '\0') {
            compat_product = app_center_json_string(compat, "device");
        }
        if (compat_product[0] == '\0') {
            compat_product = app_center_json_string(compat, "target");
        }
        if (compat_product[0] != '\0' && strcmp(compat_product, "WatcheRobot") != 0) {
            ESP_LOGW(TAG, "Package manifest product is not supported: product=%s path=%s", compat_product, path);
            ret = ESP_ERR_NOT_SUPPORTED;
            goto cleanup;
        }
    }

    permissions = cJSON_GetObjectItemCaseSensitive(root, "permissions");
    if (permissions != NULL) {
        if (!cJSON_IsArray(permissions)) {
            ESP_LOGW(TAG, "Package manifest permissions must be an array: %s", path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        cJSON *permission = NULL;
        cJSON_ArrayForEach(permission, permissions) {
            const char *value = NULL;
            if (!cJSON_IsString(permission) || permission->valuestring == NULL) {
                ESP_LOGW(TAG, "Package manifest permission item must be a string: %s", path);
                ret = ESP_ERR_INVALID_RESPONSE;
                goto cleanup;
            }
            value = permission->valuestring;
            if (strcmp(value, APP_CENTER_LEGACY_ESPNOW_PERMISSION) == 0) {
                ESP_LOGW(TAG, "Package manifest legacy remote permission is not supported: %s", path);
                ret = ESP_ERR_NOT_SUPPORTED;
                goto cleanup;
            }
            if (strcmp(value, "servo") != 0 && strcmp(value, "display") != 0 && strcmp(value, "sound") != 0 &&
                strcmp(value, "storage") != 0) {
                ESP_LOGW(TAG, "Package manifest permission not allowed: %s path=%s", value, path);
                ret = ESP_ERR_NOT_SUPPORTED;
                goto cleanup;
            }
        }
    }

    signature = cJSON_GetObjectItemCaseSensitive(root, "signature");
    if (signature != NULL) {
        const char *algorithm = NULL;
        const char *digest = NULL;
        const char *issuer = NULL;
        if (!cJSON_IsObject(signature)) {
            ESP_LOGW(TAG, "Package manifest signature must be an object: %s", path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        algorithm = app_center_json_string(signature, "algorithm");
        if (algorithm[0] == '\0') {
            algorithm = app_center_json_string(signature, "alg");
        }
        digest = app_center_json_string(signature, "digest");
        if (digest[0] == '\0') {
            digest = app_center_json_string(signature, "hash");
        }
        issuer = app_center_json_string(signature, "issuer");
        if (issuer[0] == '\0') {
            issuer = app_center_json_string(signature, "keyId");
        }
        if (issuer[0] == '\0') {
            issuer = app_center_json_string(signature, "key_id");
        }
        if (algorithm[0] == '\0' || digest[0] == '\0') {
            ESP_LOGW(TAG, "Package manifest signature missing algorithm or digest: %s", path);
            ret = ESP_ERR_INVALID_RESPONSE;
            goto cleanup;
        }
        if (strcmp(algorithm, "unsigned-dev") == 0) {
            if (!APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES) {
                ESP_LOGW(TAG, "Unsigned developer package is disabled: %s", path);
                ret = ESP_ERR_NOT_SUPPORTED;
                goto cleanup;
            }
        } else if (strcmp(algorithm, "ecdsa-p256-sha256") == 0) {
            ret = app_center_verify_manifest_signature(root, signature, digest, issuer, path);
            if (ret != ESP_OK) {
                goto cleanup;
            }
        } else {
            ESP_LOGW(TAG, "Package signature algorithm is not supported: algorithm=%s path=%s", algorithm, path);
            ret = ESP_ERR_NOT_SUPPORTED;
            goto cleanup;
        }
    } else if (!APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES) {
        ESP_LOGW(TAG, "Unsigned package is disabled: %s", path);
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    free(buffer);
    if (file != NULL) {
        fclose(file);
    }
    return ret;
}

static esp_err_t app_center_download_package_file(const app_center_entry_t *entry) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char temp_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    uint8_t *buffer = NULL;
    FILE *file = NULL;
    esp_http_client_handle_t client = NULL;
    esp_err_t ret = ESP_OK;
    int64_t content_length = 0;
    int64_t downloaded = 0;

    if (entry == NULL || entry->package_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ret = app_center_prepare_package_storage();
    if (ret != ESP_OK) {
        return ret;
    }

    app_center_make_package_path(entry->id, path, sizeof(path), false);
    app_center_make_package_path(entry->id, temp_path, sizeof(temp_path), true);
    (void)remove(temp_path);

    buffer = (uint8_t *)malloc(APP_CENTER_PACKAGE_READ_CHUNK);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = entry->package_url,
        .timeout_ms = APP_CENTER_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    client = esp_http_client_init(&config);
    if (client == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    app_center_set_http_client(&s_download_client, client);

    safe_copy(s_download_status, sizeof(s_download_status), "Connecting...");
    s_render_requested = true;
    ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Package download HTTP status=%d url=%s", status_code, entry->package_url);
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (content_length > APP_CENTER_PACKAGE_MAX_BYTES) {
        ESP_LOGW(TAG, "Package download too large: %lld max=%u url=%s", (long long)content_length,
                 (unsigned int)APP_CENTER_PACKAGE_MAX_BYTES, entry->package_url);
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    if (content_length > 0) {
        ret = app_center_package_storage_check_fit(entry->id, (size_t)content_length, "HTTP download");
        if (ret != ESP_OK) {
            goto cleanup;
        }
    }

    file = fopen(temp_path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Failed to open package temp file: %s", temp_path);
        ret = ESP_FAIL;
        goto cleanup;
    }

    while (!s_download_cancel_requested) {
        int read_len = esp_http_client_read(client, (char *)buffer, APP_CENTER_PACKAGE_READ_CHUNK);
        if (read_len < 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (read_len == 0) {
            break;
        }
        if (downloaded + read_len > APP_CENTER_PACKAGE_MAX_BYTES) {
            ret = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        ret = app_center_package_storage_check_next_chunk((size_t)read_len, "HTTP download");
        if (ret != ESP_OK) {
            goto cleanup;
        }
        if (fwrite(buffer, 1, (size_t)read_len, file) != (size_t)read_len) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        downloaded += read_len;
        if (content_length > 0) {
            int percent = (int)((downloaded * 100) / content_length);
            if (percent > 100) {
                percent = 100;
            }
            s_download_progress = percent;
            snprintf(s_download_status, sizeof(s_download_status), "Downloading... %d%%", percent);
        } else {
            snprintf(s_download_status, sizeof(s_download_status), "Downloading... %ld KB", (long)(downloaded / 1024));
        }
        s_render_requested = true;
    }

    if (s_download_cancel_requested) {
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    if (downloaded <= 0) {
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    safe_copy(s_download_status, sizeof(s_download_status), "Installing...");
    s_download_progress = 100;
    s_render_requested = true;

cleanup:
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
    if (client != NULL) {
        app_center_clear_http_client(&s_download_client, client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(buffer);

    if (ret == ESP_OK) {
        const char *canonical_id = app_center_canonical_package_id(entry->id, entry->name);
        ret = app_center_validate_package_manifest(temp_path, canonical_id);
        if (ret == ESP_OK) {
            ret = app_center_reject_downgrade_if_needed(canonical_id, entry->version);
        }
    }
    if (ret == ESP_OK) {
        (void)remove(path);
        if (rename(temp_path, path) != 0) {
            ESP_LOGW(TAG, "Failed to finalize package file: %s", path);
            ret = ESP_FAIL;
        }
    }
    if (ret != ESP_OK) {
        (void)remove(temp_path);
    }
    return ret;
}

typedef struct {
    const app_center_entry_t *entry;
    const char *command_id;
    const char *source_label;
    int source_attempt;
    int source_count;
} app_center_firmware_install_context_t;

static const char *app_center_firmware_source_label(const char *url) {
    if (url == NULL) {
        return "Source";
    }
    if (strstr(url, "github.com/") != NULL && strstr(url, "/releases/") != NULL) {
        return "GitHub";
    }
    if (strstr(url, "cdn.jsdelivr.net") != NULL) {
        return "jsDelivr";
    }
    if (strstr(url, "raw.githubusercontent.com") != NULL) {
        return "GitHub Raw";
    }
    return "Mirror";
}

static const char *app_center_firmware_ws_state_for_status(const char *status) {
    if (status == NULL) {
        return "installing";
    }
    if (strstr(status, "Connecting") != NULL || strstr(status, "Writing") != NULL) {
        return "downloading";
    }
    if (strstr(status, "Finalizing") != NULL) {
        return "installing";
    }
    if (strstr(status, "Ready to reboot") != NULL || strstr(status, "Rebooting") != NULL) {
        return "rebooting";
    }
    return "installing";
}

static void app_center_report_firmware_status(const app_center_firmware_install_context_t *ctx, const char *state,
                                              const char *message) {
    if (ctx == NULL || ctx->entry == NULL || ctx->command_id == NULL || ctx->command_id[0] == '\0') {
        return;
    }
    (void)ws_send_app_package_status(ctx->command_id, ctx->entry->id, ctx->entry->name, ctx->entry->version, state,
                                     message != NULL ? message : state);
}

static void app_center_firmware_install_progress_cb(int percent, void *user_ctx) {
    app_center_firmware_install_context_t *ctx = (app_center_firmware_install_context_t *)user_ctx;
    char message[64] = {0};

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    s_download_progress = percent;
    snprintf(message, sizeof(message), "Downloading firmware... %d%%", percent);
    safe_copy(s_download_status, sizeof(s_download_status), message);
    app_center_report_firmware_status(ctx, "downloading", message);
    s_render_requested = true;
}

static void app_center_firmware_install_status_cb(const char *status, void *user_ctx) {
    app_center_firmware_install_context_t *ctx = (app_center_firmware_install_context_t *)user_ctx;
    char message[96] = {0};
    const char *display_status = status;

    if (status != NULL && status[0] != '\0') {
        if (ctx != NULL && ctx->source_label != NULL && ctx->source_count > 0) {
            if (strstr(status, "Connecting OTA") != NULL) {
                snprintf(message, sizeof(message), "%s %d/%d: connecting", ctx->source_label, ctx->source_attempt,
                         ctx->source_count);
                display_status = message;
            } else if (strstr(status, "Writing OTA") != NULL) {
                snprintf(message, sizeof(message), "%s %d/%d: downloading", ctx->source_label, ctx->source_attempt,
                         ctx->source_count);
                display_status = message;
            } else if (strstr(status, "Finalizing") != NULL) {
                safe_copy(message, sizeof(message), "Verifying firmware...");
                display_status = message;
            }
        }
        safe_copy(s_download_status, sizeof(s_download_status), display_status);
        app_center_report_firmware_status(ctx, app_center_firmware_ws_state_for_status(status), display_status);
        s_render_requested = true;
    }
}

static bool app_center_download_status_is_actionable_failure(void) {
    return strstr(s_download_status, "mismatch") != NULL || strstr(s_download_status, "invalid") != NULL ||
           strstr(s_download_status, "unreadable") != NULL || strstr(s_download_status, "unchanged") != NULL ||
           strstr(s_download_status, "No OTA data") != NULL;
}

static bool app_center_firmware_install_cancel_requested(void *user_ctx) {
    (void)user_ctx;
    return s_download_cancel_requested;
}

static void app_center_set_final_download_failure_status(esp_err_t ret, app_center_entry_type_t type) {
    if (type == APP_CENTER_ENTRY_FIRMWARE_APP && app_center_download_status_is_actionable_failure()) {
        safe_copy(s_remote_status, sizeof(s_remote_status), "Install failed");
        return;
    }

    snprintf(s_download_status, sizeof(s_download_status), "%s failed: %s",
             type == APP_CENTER_ENTRY_FIRMWARE_APP ? "Install" : "Download", esp_err_to_name(ret));
    safe_copy(s_remote_status, sizeof(s_remote_status),
              type == APP_CENTER_ENTRY_FIRMWARE_APP ? "Install failed" : "Download failed");
}

static esp_err_t app_center_install_firmware_entry(const app_center_entry_t *entry, const char *command_id) {
    app_center_firmware_install_context_t ctx = {
        .entry = entry,
        .command_id = command_id,
    };
    ota_service_cancel_cb_t cancel_cb = command_id == NULL ? app_center_firmware_install_cancel_requested : NULL;
    const char *expected_version = NULL;
    app_center_firmware_source_t install_sources[1 + APP_CENTER_FIRMWARE_MIRROR_MAX] = {0};
    int install_url_count = 0;
    esp_err_t ret;

    ret = app_center_validate_firmware_entry_for_install(entry);
    if (ret != ESP_OK) {
        app_center_report_firmware_status(&ctx, "install_failed", esp_err_to_name(ret));
        return ret;
    }

    expected_version = entry->image_version[0] != '\0' ? entry->image_version : NULL;
    if (entry->firmware_url[0] != '\0') {
        install_sources[install_url_count++] = (app_center_firmware_source_t){
            .url = entry->firmware_url,
            .source_index = 0,
        };
    }
    for (int index = 0; index < entry->firmware_mirror_count &&
                        install_url_count < (int)(sizeof(install_sources) / sizeof(install_sources[0]));
         ++index) {
        if (entry->firmware_mirrors[index][0] != '\0') {
            install_sources[install_url_count++] = (app_center_firmware_source_t){
                .url = entry->firmware_mirrors[index],
                .source_index = index + 1,
            };
        }
    }
    if (install_url_count > 1) {
        int preferred_source = app_center_preferred_firmware_source_load(entry->id);
        if (app_center_firmware_sources_promote_preferred(install_sources, (size_t)install_url_count,
                                                          preferred_source)) {
            ESP_LOGI(TAG, "Firmware app preferred source promoted: id=%s source=%d", entry->id, preferred_source);
        }
    }

    ret = ESP_FAIL;
    for (int attempt = 0; attempt < install_url_count; ++attempt) {
        char status_text[96] = {0};
        const char *source_url = install_sources[attempt].url;
        const char *source_label = app_center_firmware_source_label(source_url);

        ctx.source_attempt = attempt + 1;
        ctx.source_count = install_url_count;
        ctx.source_label = source_label;
        snprintf(status_text, sizeof(status_text), "%s %d/%d: starting", source_label, attempt + 1, install_url_count);
        safe_copy(s_download_status, sizeof(s_download_status), status_text);
        s_download_progress = 0;
        s_render_requested = true;
        app_center_report_firmware_status(&ctx, "downloading", status_text);
        ESP_LOGI(TAG, "Firmware app install source attempt: id=%s attempt=%d/%d url=%s", entry->id, attempt + 1,
                 install_url_count, source_url);

        ret = ota_service_install_with_sha256_cancelable(source_url, expected_version, entry->firmware_sha256,
                                                         app_center_firmware_install_progress_cb,
                                                         app_center_firmware_install_status_cb, cancel_cb, &ctx);
        if (ret == ESP_OK) {
            app_center_preferred_firmware_source_save(entry->id, install_sources[attempt].source_index);
            break;
        }

        ESP_LOGW(TAG, "Firmware app source failed: id=%s attempt=%d/%d ret=%s url=%s", entry->id, attempt + 1,
                 install_url_count, esp_err_to_name(ret), source_url);
        if (attempt + 1 < install_url_count) {
            snprintf(status_text, sizeof(status_text), "%s failed. Trying source %d/%d", source_label, attempt + 2,
                     install_url_count);
            safe_copy(s_download_status, sizeof(s_download_status), status_text);
            app_center_report_firmware_status(&ctx, "downloading", status_text);
            s_render_requested = true;
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Firmware app install failed: id=%s ret=%s", entry->id, esp_err_to_name(ret));
        app_center_report_firmware_status(&ctx, "install_failed", esp_err_to_name(ret));
        return ret;
    }

    app_center_app_installed_save(entry->id, true);
    app_center_save_entry_metadata(entry);
    (void)app_center_upsert_firmware_entry(entry, true);
    app_center_report_firmware_status(&ctx, "installed", "installed");
    app_center_report_firmware_status(&ctx, "rebooting", "rebooting into firmware app");
    snprintf(s_remote_status, sizeof(s_remote_status), "%s installed", entry->name);
    safe_copy(s_download_status, sizeof(s_download_status), "Installed. Rebooting...");
    s_download_progress = 100;
    s_render_requested = true;
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static void app_download_task(void *arg) {
    app_center_download_task_arg_t task_arg = {0};
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    app_center_entry_t entry = {0};
    esp_err_t ret = ESP_OK;

    if (arg == NULL) {
        ret = ESP_ERR_INVALID_ARG;
    } else {
        task_arg = *(app_center_download_task_arg_t *)arg;
        free(arg);
    }

    int index = task_arg.index;
    if (ret == ESP_OK && (index < 0 || index >= s_entry_count)) {
        ret = ESP_ERR_INVALID_ARG;
    } else if (ret == ESP_OK) {
        entry = s_entries[index];
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "App.Center download task started: index=%d id=%s url=%s", index, entry.id, entry.package_url);
    }
    if (ret == ESP_OK) {
        if (entry.type == APP_CENTER_ENTRY_FIRMWARE_APP) {
            ret = app_center_install_firmware_entry(&entry, NULL);
        } else {
            ret = app_center_download_package_file(&entry);
        }
    }
    if (ret == ESP_OK && entry.type == APP_CENTER_ENTRY_PACKAGE &&
        app_center_generation_is_active(task_arg.generation)) {
        app_center_app_installed_save(entry.id, true);
        app_center_save_package_metadata(entry.id, entry.name, entry.version, entry.description);
        if (index >= 0 && index < s_entry_count && strcmp(s_entries[index].id, entry.id) == 0) {
            s_entries[index].installed = true;
        }
        app_center_invalidate_manager_snapshot();
    } else if (ret == ESP_OK && entry.type == APP_CENTER_ENTRY_PACKAGE) {
        ret = ESP_ERR_INVALID_STATE;
        ESP_LOGI(TAG, "App.Center download result ignored for stale generation: index=%d id=%s", index, entry.id);
    }

    if (s_download_task == current_task) {
        s_download_result = ret;
        s_download_finished = true;
        s_download_running = false;
        if (app_center_generation_is_active(task_arg.generation)) {
            if (ret == ESP_OK) {
                safe_copy(s_download_status, sizeof(s_download_status), "Installed");
                snprintf(s_remote_status, sizeof(s_remote_status), "%s installed", entry.name);
                s_selected_index = index;
                s_action_index = index;
                s_page = APP_CENTER_PAGE_LIST;
                ESP_LOGI(TAG, "App.Center download installed: index=%d id=%s", index, entry.id);
            } else if (s_download_cancel_requested) {
                safe_copy(s_download_status, sizeof(s_download_status), "Download canceled");
                safe_copy(s_remote_status, sizeof(s_remote_status), "Download canceled");
                s_page = APP_CENTER_PAGE_LIST;
                ESP_LOGW(TAG, "App.Center download canceled: index=%d ret=%s", index, esp_err_to_name(ret));
            } else {
                app_center_set_final_download_failure_status(ret, entry.type);
                ESP_LOGW(TAG, "App.Center download failed: index=%d ret=%s", index, esp_err_to_name(ret));
            }
            s_render_requested = true;
        }
        s_download_cancel_requested = false;
        s_download_task = NULL;
    }
    vTaskDelete(NULL);
}

static void app_center_start_download_task(int index) {
    if (!s_app_center_active) {
        ESP_LOGW(TAG, "App.Center download ignored while inactive");
        return;
    }

    if (s_download_running) {
        ESP_LOGW(TAG, "App.Center download task already running");
        return;
    }

    ESP_LOGI(TAG, "App.Center download task create: index=%d", index);
    s_download_running = true;
    s_download_progress = 0;
    s_download_finished = false;
    s_download_cancel_requested = false;
    safe_copy(s_download_status, sizeof(s_download_status), "Connecting...");
    app_center_download_task_arg_t *task_arg = (app_center_download_task_arg_t *)malloc(sizeof(*task_arg));
    if (task_arg == NULL) {
        s_download_task = NULL;
        s_download_running = false;
        s_download_result = ESP_ERR_NO_MEM;
        s_download_finished = true;
        safe_copy(s_download_status, sizeof(s_download_status), "Download task memory error");
        ESP_LOGE(TAG, "App.Center download task arg alloc failed: index=%d", index);
        s_render_requested = true;
        return;
    }
    task_arg->index = index;
    task_arg->generation = s_app_center_generation;
    if (xTaskCreate(app_download_task, "app_download", 8192, task_arg, 5, &s_download_task) != pdPASS) {
        free(task_arg);
        s_download_task = NULL;
        s_download_running = false;
        s_download_result = ESP_ERR_NO_MEM;
        s_download_finished = true;
        safe_copy(s_download_status, sizeof(s_download_status), "Download task failed");
        ESP_LOGE(TAG, "App.Center download task create failed: index=%d", index);
    }
    s_render_requested = true;
}

static void app_center_ensure_package_entries_loaded(void) {
    if (app_center_prepare_package_storage() != ESP_OK) {
        return;
    }
    app_center_cleanup_legacy_espnow_remote_state();
    app_center_load_persisted_package_metadata();
}

static int app_center_find_entry_by_id(const char *id) {
    if (id == NULL || id[0] == '\0') {
        return -1;
    }
    for (int index = 0; index < s_entry_count; ++index) {
        if (strcmp(s_entries[index].id, id) == 0) {
            return index;
        }
    }
    return -1;
}

static int app_center_upsert_package_entry(const char *id, const char *name, const char *version,
                                           const char *description, bool installed) {
    const char *canonical_id = app_center_canonical_package_id(id, name);
    int index = app_center_find_entry_by_id(canonical_id);
    app_center_entry_t *entry = NULL;

    if (app_center_legacy_espnow_requested(id, name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        return -1;
    }
    if (canonical_id == NULL || canonical_id[0] == '\0') {
        return -1;
    }
    if (index < 0) {
        if (s_entry_count >= APP_CENTER_MAX_ENTRIES) {
            ESP_LOGW(TAG, "App.Center entry list full, cannot add %s", canonical_id);
            return -1;
        }
        index = s_entry_count++;
        memset(&s_entries[index], 0, sizeof(s_entries[index]));
    }

    entry = &s_entries[index];
    safe_copy(entry->id, sizeof(entry->id), canonical_id);
    safe_copy(entry->name, sizeof(entry->name), name != NULL && name[0] != '\0' ? name : canonical_id);
    safe_copy(entry->version, sizeof(entry->version), version != NULL ? version : "");
    safe_copy(entry->description, sizeof(entry->description), description != NULL ? description : "");
    entry->type = APP_CENTER_ENTRY_PACKAGE;
    entry->installed = installed;
    return index;
}

static int app_center_upsert_firmware_entry(const app_center_entry_t *source, bool installed) {
    const char *canonical_id = NULL;
    int index;
    app_center_entry_t *entry = NULL;

    if (source == NULL || source->id[0] == '\0') {
        return -1;
    }
    if (app_center_legacy_espnow_requested(source->id, source->name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        return -1;
    }

    canonical_id = app_center_canonical_package_id(source->id, source->name);
    if (canonical_id == NULL || canonical_id[0] == '\0') {
        return -1;
    }
    index = app_center_find_entry_by_id(canonical_id);
    if (index < 0) {
        if (s_entry_count >= APP_CENTER_MAX_ENTRIES) {
            ESP_LOGW(TAG, "App.Center entry list full, cannot add firmware app %s", canonical_id);
            return -1;
        }
        index = s_entry_count++;
        memset(&s_entries[index], 0, sizeof(s_entries[index]));
    }

    entry = &s_entries[index];
    safe_copy(entry->id, sizeof(entry->id), canonical_id);
    safe_copy(entry->name, sizeof(entry->name), source->name[0] != '\0' ? source->name : canonical_id);
    safe_copy(entry->version, sizeof(entry->version), source->version);
    safe_copy(entry->description, sizeof(entry->description), source->description);
    safe_copy(entry->icon_url, sizeof(entry->icon_url), source->icon_url);
    safe_copy(entry->package_url, sizeof(entry->package_url),
              source->package_url[0] != '\0' ? source->package_url : source->firmware_url);
    safe_copy(entry->firmware_url, sizeof(entry->firmware_url), source->firmware_url);
    safe_copy(entry->firmware_sha256, sizeof(entry->firmware_sha256), source->firmware_sha256);
    safe_copy(entry->image_version, sizeof(entry->image_version), source->image_version);
    safe_copy(entry->signature_algorithm, sizeof(entry->signature_algorithm), source->signature_algorithm);
    safe_copy(entry->min_launcher_version, sizeof(entry->min_launcher_version), source->min_launcher_version);
    entry->firmware_size_bytes = source->firmware_size_bytes;
    entry->compat_ota_slot_size = source->compat_ota_slot_size;
    entry->compat_flash_size_mb = source->compat_flash_size_mb;
    entry->firmware_mirror_count = source->firmware_mirror_count;
    if (entry->firmware_mirror_count > APP_CENTER_FIRMWARE_MIRROR_MAX) {
        entry->firmware_mirror_count = APP_CENTER_FIRMWARE_MIRROR_MAX;
    }
    for (int mirror_index = 0; mirror_index < entry->firmware_mirror_count; ++mirror_index) {
        safe_copy(entry->firmware_mirrors[mirror_index], sizeof(entry->firmware_mirrors[mirror_index]),
                  source->firmware_mirrors[mirror_index]);
    }
    entry->type = APP_CENTER_ENTRY_FIRMWARE_APP;
    entry->installed = installed;
    return index;
}

static void app_center_close_ws_package_transfer(void) {
    if (s_ws_package_file != NULL) {
        fclose(s_ws_package_file);
        s_ws_package_file = NULL;
    }
}

static void app_center_reset_ws_package_transfer_state(bool remove_temp) {
    app_center_close_ws_package_transfer();
    if (remove_temp && s_ws_package_temp_path[0] != '\0') {
        (void)remove(s_ws_package_temp_path);
    }
    memset(s_ws_package_command_id, 0, sizeof(s_ws_package_command_id));
    memset(s_ws_package_id, 0, sizeof(s_ws_package_id));
    memset(s_ws_package_name, 0, sizeof(s_ws_package_name));
    memset(s_ws_package_version, 0, sizeof(s_ws_package_version));
    memset(s_ws_package_description, 0, sizeof(s_ws_package_description));
    memset(s_ws_package_temp_path, 0, sizeof(s_ws_package_temp_path));
    s_ws_package_expected_size = 0;
    s_ws_package_received_size = 0;
    s_ws_package_last_reported_percent = -1;
}

static bool app_center_ws_package_abort_matches_active(const ws_app_package_command_t *command) {
    const char *command_app_id = NULL;

    if (s_ws_package_id[0] == '\0') {
        return false;
    }
    if (command == NULL) {
        return true;
    }
    if (command->command_id[0] != '\0' && s_ws_package_command_id[0] != '\0' &&
        strcmp(command->command_id, s_ws_package_command_id) != 0) {
        return false;
    }
    if (command->app_id[0] != '\0') {
        command_app_id = app_center_canonical_package_id(command->app_id, command->name);
        if (command_app_id == NULL || strcmp(command_app_id, s_ws_package_id) != 0) {
            return false;
        }
    }
    return true;
}

static void app_center_report_ws_package_receive_progress(bool force) {
    char message[64] = {0};
    int percent = 0;

    if (s_ws_package_id[0] == '\0') {
        return;
    }

    if (s_ws_package_expected_size > 0U) {
        percent = (int)((s_ws_package_received_size * 100U) / s_ws_package_expected_size);
        if (percent > 100) {
            percent = 100;
        }
    }

    if (!force && percent < 100 && percent == s_ws_package_last_reported_percent) {
        return;
    }
    if (!force && percent < 100 && s_ws_package_last_reported_percent >= 0 &&
        percent - s_ws_package_last_reported_percent < 10) {
        return;
    }

    s_ws_package_last_reported_percent = percent;
    if (s_ws_package_expected_size > 0U) {
        snprintf(message, sizeof(message), "receiving package %d%%", percent);
    } else {
        snprintf(message, sizeof(message), "receiving package %u KB",
                 (unsigned int)(s_ws_package_received_size / 1024U));
    }
    (void)ws_send_app_package_status(s_ws_package_command_id, s_ws_package_id, s_ws_package_name, s_ws_package_version,
                                     "installing", message);
}

static void app_center_report_ws_package_failure(const char *message) {
    if (s_ws_package_id[0] == '\0') {
        return;
    }
    (void)ws_send_app_package_status(s_ws_package_command_id, s_ws_package_id, s_ws_package_name, s_ws_package_version,
                                     "install_failed",
                                     message != NULL && message[0] != '\0' ? message : "install failed");
}

esp_err_t app_center_package_transfer_begin(const ws_app_package_transfer_t *transfer) {
    esp_err_t ret;
    const char *canonical_id = NULL;

    if (transfer == NULL || transfer->app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    app_center_reset_ws_package_transfer_state(true);

    ret = app_center_prepare_package_storage();
    if (ret != ESP_OK) {
        return ret;
    }
    if (transfer->size_bytes > APP_CENTER_PACKAGE_MAX_BYTES) {
        ESP_LOGW(TAG, "Desktop app package too large: id=%s size=%u max=%u", transfer->app_id,
                 (unsigned int)transfer->size_bytes, (unsigned int)APP_CENTER_PACKAGE_MAX_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    canonical_id = app_center_canonical_package_id(transfer->app_id, transfer->name);
    if (transfer->size_bytes > 0U) {
        ret = app_center_package_storage_check_fit(canonical_id, transfer->size_bytes, "desktop transfer");
        if (ret != ESP_OK) {
            return ret;
        }
    }
    safe_copy(s_ws_package_command_id, sizeof(s_ws_package_command_id), transfer->command_id);
    safe_copy(s_ws_package_id, sizeof(s_ws_package_id), canonical_id);
    safe_copy(s_ws_package_name, sizeof(s_ws_package_name),
              transfer->name[0] != '\0' ? transfer->name : transfer->app_id);
    safe_copy(s_ws_package_version, sizeof(s_ws_package_version), transfer->version);
    safe_copy(s_ws_package_description, sizeof(s_ws_package_description), transfer->description);
    s_ws_package_expected_size = transfer->size_bytes;
    app_center_make_package_path(s_ws_package_id, s_ws_package_temp_path, sizeof(s_ws_package_temp_path), true);
    (void)remove(s_ws_package_temp_path);

    s_ws_package_file = fopen(s_ws_package_temp_path, "wb");
    if (s_ws_package_file == NULL) {
        ESP_LOGW(TAG, "Failed to open desktop package temp file: %s", s_ws_package_temp_path);
        app_center_reset_ws_package_transfer_state(true);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "App package transfer begin: id=%s name=%s size=%u", s_ws_package_id, s_ws_package_name,
             (unsigned int)s_ws_package_expected_size);
    app_center_report_ws_package_receive_progress(true);
    return ESP_OK;
}

esp_err_t app_center_package_transfer_write(uint8_t flags, const uint8_t *payload, size_t payload_len) {
    if (s_ws_package_file == NULL || s_ws_package_id[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_len > 0U && payload == NULL) {
        app_center_report_ws_package_failure("invalid package payload");
        app_center_reset_ws_package_transfer_state(true);
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ws_package_received_size + payload_len > APP_CENTER_PACKAGE_MAX_BYTES) {
        app_center_report_ws_package_failure("package payload exceeds device limit");
        app_center_reset_ws_package_transfer_state(true);
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload_len > 0U) {
        esp_err_t ret = app_center_package_storage_check_next_chunk(payload_len, "desktop transfer");
        if (ret != ESP_OK) {
            app_center_report_ws_package_failure("not enough package storage");
            app_center_reset_ws_package_transfer_state(true);
            return ret;
        }
    }
    if (payload_len > 0U && fwrite(payload, 1, payload_len, s_ws_package_file) != payload_len) {
        app_center_report_ws_package_failure("failed to write package payload");
        app_center_reset_ws_package_transfer_state(true);
        return ESP_FAIL;
    }
    s_ws_package_received_size += payload_len;
    app_center_report_ws_package_receive_progress((flags & WS_FRAME_FLAG_LAST) != 0U);
    return ESP_OK;
}

esp_err_t app_center_package_transfer_commit(const ws_app_package_transfer_t *transfer) {
    char final_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char actual_sha256[65] = {0};
    const char *app_id = s_ws_package_id;
    esp_err_t ret;

    if (app_id[0] == '\0' || s_ws_package_temp_path[0] == '\0') {
        ESP_LOGW(TAG, "App package commit ignored: no active transfer");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "App package commit begin: id=%s received=%u expected=%u", app_id,
             (unsigned int)s_ws_package_received_size, (unsigned int)s_ws_package_expected_size);
    app_center_close_ws_package_transfer();
    (void)ws_send_app_package_status(s_ws_package_command_id, app_id, s_ws_package_name, s_ws_package_version,
                                     "installing", "verifying package");
    if (transfer != NULL && transfer->size_bytes > 0U) {
        s_ws_package_expected_size = transfer->size_bytes;
    }
    if (s_ws_package_expected_size > 0U && s_ws_package_received_size != s_ws_package_expected_size) {
        ESP_LOGW(TAG, "App package size mismatch: received=%u expected=%u", (unsigned int)s_ws_package_received_size,
                 (unsigned int)s_ws_package_expected_size);
        app_center_reset_ws_package_transfer_state(true);
        return ESP_ERR_INVALID_SIZE;
    }

    if (transfer != NULL && transfer->sha256[0] != '\0' && strcmp(transfer->sha256, "sha256-unavailable") != 0) {
        ret = app_center_sha256_file_hex(s_ws_package_temp_path, actual_sha256, sizeof(actual_sha256));
        if (ret != ESP_OK) {
            app_center_reset_ws_package_transfer_state(true);
            return ret;
        }
        if (strcasecmp(actual_sha256, transfer->sha256) != 0) {
            ESP_LOGW(TAG, "App package SHA-256 mismatch: actual=%s expected=%s", actual_sha256, transfer->sha256);
            app_center_reset_ws_package_transfer_state(true);
            return ESP_ERR_INVALID_CRC;
        }
    }

    ret = app_center_validate_package_manifest(s_ws_package_temp_path, app_id);
    if (ret != ESP_OK) {
        app_center_reset_ws_package_transfer_state(true);
        return ret;
    }
    ret = app_center_reject_downgrade_if_needed(app_id, s_ws_package_version);
    if (ret != ESP_OK) {
        app_center_reset_ws_package_transfer_state(true);
        return ret;
    }

    app_center_make_package_path(app_id, final_path, sizeof(final_path), false);
    (void)remove(final_path);
    if (rename(s_ws_package_temp_path, final_path) != 0) {
        app_center_reset_ws_package_transfer_state(true);
        return ESP_FAIL;
    }

    app_center_app_installed_save(app_id, true);
    app_center_save_package_metadata(app_id, s_ws_package_name, s_ws_package_version, s_ws_package_description);
    (void)app_center_upsert_package_entry(app_id, s_ws_package_name, s_ws_package_version, s_ws_package_description,
                                          true);
    (void)ws_send_app_package_status(s_ws_package_command_id, app_id, s_ws_package_name, s_ws_package_version,
                                     "installed", "installed");
    s_render_requested = true;
    ESP_LOGI(TAG, "App package installed from desktop: id=%s path=%s", app_id, final_path);
    app_center_reset_ws_package_transfer_state(false);
    app_center_invalidate_manager_snapshot();
    return ESP_OK;
}

void app_center_package_transfer_abort(const ws_app_package_command_t *command, const char *reason) {
    if (!app_center_ws_package_abort_matches_active(command)) {
        ESP_LOGW(TAG, "Ignoring stale app package abort: active_id=%s active_command=%s abort_id=%s abort_command=%s",
                 s_ws_package_id[0] != '\0' ? s_ws_package_id : "none",
                 s_ws_package_command_id[0] != '\0' ? s_ws_package_command_id : "none",
                 command != NULL && command->app_id[0] != '\0' ? command->app_id : "none",
                 command != NULL && command->command_id[0] != '\0' ? command->command_id : "none");
        return;
    }
    ESP_LOGW(TAG, "App package transfer aborted: id=%s reason=%s", s_ws_package_id,
             reason != NULL && reason[0] != '\0' ? reason : "unspecified");
    app_center_reset_ws_package_transfer_state(true);
}

esp_err_t app_center_package_install_from_url(const ws_app_package_transfer_t *transfer) {
    app_center_entry_t entry = {0};

    if (transfer == NULL || transfer->app_id[0] == '\0' || transfer->source_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (transfer->package_type[0] != '\0' && strcmp(transfer->package_type, "firmware-app") != 0) {
        ESP_LOGW(TAG, "Desktop install rejected: unsupported type=%s id=%s", transfer->package_type, transfer->app_id);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (app_center_legacy_espnow_requested(transfer->app_id, transfer->name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        ESP_LOGW(TAG, "Desktop install rejected: legacy remote app id=%s name=%s", transfer->app_id, transfer->name);
        return ESP_ERR_NOT_SUPPORTED;
    }

    safe_copy(entry.id, sizeof(entry.id), app_center_canonical_package_id(transfer->app_id, transfer->name));
    safe_copy(entry.name, sizeof(entry.name), transfer->name[0] != '\0' ? transfer->name : entry.id);
    safe_copy(entry.version, sizeof(entry.version),
              transfer->version[0] != '\0' ? transfer->version : transfer->image_version);
    safe_copy(entry.description, sizeof(entry.description), transfer->description);
    entry.type = APP_CENTER_ENTRY_FIRMWARE_APP;
    safe_copy(entry.firmware_url, sizeof(entry.firmware_url), transfer->source_url);
    safe_copy(entry.package_url, sizeof(entry.package_url), transfer->source_url);
    safe_copy(entry.firmware_sha256, sizeof(entry.firmware_sha256), transfer->sha256);
    safe_copy(entry.image_version, sizeof(entry.image_version),
              transfer->image_version[0] != '\0' ? transfer->image_version : transfer->version);
    entry.firmware_size_bytes = transfer->size_bytes;
    entry.compat_ota_slot_size = APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES;

    app_center_ensure_package_entries_loaded();
    (void)app_center_upsert_firmware_entry(&entry, false);
    s_render_requested = true;
    ESP_LOGI(TAG, "Desktop requested firmware app install: id=%s name=%s url=%s size=%u", entry.id, entry.name,
             entry.firmware_url, (unsigned int)entry.firmware_size_bytes);
    return app_center_install_firmware_entry(&entry, transfer->command_id);
}

esp_err_t app_center_package_open(const ws_app_package_command_t *command) {
    int index;
    const char *app_id = NULL;

    if (command == NULL || command->app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_center_legacy_espnow_requested(command->app_id, command->name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        app_center_invalidate_manager_snapshot();
        return ESP_ERR_NOT_FOUND;
    }
    app_id = app_center_canonical_package_id(command->app_id, command->name);
    ESP_LOGI(TAG, "Desktop requested app open: id=%s name=%s", app_id, command->name);

    app_center_ensure_package_entries_loaded();
    index = app_center_find_entry_by_id(app_id);
    if (index >= 0 && s_entries[index].type == APP_CENTER_ENTRY_FIRMWARE_APP) {
        if (!s_entries[index].installed || !app_center_app_installed_load(app_id)) {
            return ESP_ERR_NOT_FOUND;
        }
        return app_center_open_firmware_entry(&s_entries[index]);
    }

    if (!app_center_package_exists(app_id)) {
        app_center_app_installed_save(app_id, false);
        app_center_invalidate_manager_snapshot();
        return ESP_ERR_NOT_FOUND;
    }
    index = app_center_upsert_package_entry(app_id, command->name[0] != '\0' ? command->name : app_id, command->version,
                                            "", true);
    if (index < 0) {
        return ESP_FAIL;
    }
    app_center_open_entry(index);
    return ESP_OK;
}

static size_t app_center_entry_package_bytes(const app_center_entry_t *entry) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    char meta_path[APP_CENTER_PACKAGE_PATH_MAX] = {0};
    size_t bytes = 0;

    if (entry == NULL || entry->id[0] == '\0') {
        return 0;
    }
    app_center_make_metadata_path(entry->id, meta_path, sizeof(meta_path));
    bytes += app_center_file_size_or_zero(meta_path);
    if (entry->type == APP_CENTER_ENTRY_PACKAGE) {
        app_center_make_package_path(entry->id, path, sizeof(path), false);
        bytes += app_center_file_size_or_zero(path);
    }
    return bytes;
}

static size_t app_center_entry_display_size_bytes(const app_center_entry_t *entry) {
    char path[APP_CENTER_PACKAGE_PATH_MAX] = {0};

    if (entry == NULL) {
        return 0;
    }
    if (entry->type == APP_CENTER_ENTRY_FIRMWARE_APP) {
        return entry->firmware_size_bytes > 0U ? entry->firmware_size_bytes : APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES;
    }
    app_center_make_package_path(entry->id, path, sizeof(path), false);
    return app_center_file_size_or_zero(path);
}

esp_err_t app_center_get_manager_snapshot(app_center_manager_snapshot_t *out_snapshot) {
    size_t total = 0;
    size_t used = 0;
    size_t free_bytes = 0;

    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->firmware_slot_reserved_bytes = APP_CENTER_FIRMWARE_OTA_SLOT_MAX_BYTES;

    app_center_ensure_package_entries_loaded();
    if (app_center_package_storage_info(&total, &used, &free_bytes) == ESP_OK) {
        out_snapshot->spiffs_available = true;
        out_snapshot->spiffs_total_bytes = total;
        out_snapshot->spiffs_used_bytes = used;
        out_snapshot->spiffs_free_bytes = free_bytes;
    }

    for (int index = 0; index < s_entry_count; ++index) {
        const app_center_entry_t *entry = &s_entries[index];
        app_center_manager_app_info_t *app_info = NULL;
        bool installed = entry->installed && app_center_app_installed_load(entry->id);

        if (app_center_legacy_espnow_requested(entry->id, entry->name)) {
            app_center_cleanup_legacy_espnow_remote_state();
            continue;
        }
        if (!installed) {
            continue;
        }
        if (entry->type == APP_CENTER_ENTRY_PACKAGE && !app_center_package_exists(entry->id)) {
            app_center_app_state_clear(entry->id);
            continue;
        }
        if (out_snapshot->app_count >= APP_CENTER_MANAGER_MAX_APPS) {
            break;
        }

        app_info = &out_snapshot->apps[out_snapshot->app_count++];
        safe_copy(app_info->id, sizeof(app_info->id), entry->id);
        safe_copy(app_info->name, sizeof(app_info->name), entry->name[0] != '\0' ? entry->name : entry->id);
        safe_copy(app_info->version, sizeof(app_info->version), entry->version);
        app_info->type = app_center_entry_manager_kind(entry);
        app_info->size_bytes = app_center_entry_display_size_bytes(entry);
        app_info->installed = true;
        out_snapshot->installed_count++;
        out_snapshot->app_center_package_bytes += app_center_entry_package_bytes(entry);

        if (entry->type == APP_CENTER_ENTRY_FIRMWARE_APP && !out_snapshot->firmware_app_installed) {
            out_snapshot->firmware_app_installed = true;
            safe_copy(out_snapshot->firmware_app_name, sizeof(out_snapshot->firmware_app_name),
                      entry->name[0] != '\0' ? entry->name : entry->id);
            safe_copy(out_snapshot->firmware_app_version, sizeof(out_snapshot->firmware_app_version), entry->version);
            out_snapshot->firmware_app_size_bytes = app_info->size_bytes;
        }
    }

    return ESP_OK;
}

esp_err_t app_center_uninstall_app(const char *app_id, const char *name, const char *command_id) {
    const char *canonical_id = NULL;

    if (app_id == NULL || app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_center_legacy_espnow_requested(app_id, name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        return ESP_OK;
    }
    canonical_id = app_center_canonical_package_id(app_id, name);
    return app_center_uninstall_entry_by_id(canonical_id, name, NULL, command_id, true);
}

esp_err_t app_center_package_uninstall(const ws_app_package_command_t *command) {
    const char *app_id = NULL;

    if (command == NULL || command->app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_center_legacy_espnow_requested(command->app_id, command->name)) {
        app_center_cleanup_legacy_espnow_remote_state();
        return ESP_OK;
    }
    app_id = app_center_canonical_package_id(command->app_id, command->name);
    ESP_LOGI(TAG, "Desktop requested app uninstall: id=%s name=%s", app_id, command->name);
    return app_center_uninstall_entry_by_id(app_id, command->name, command->version, command->command_id, true);
}

esp_err_t app_center_package_send_list(const ws_app_package_command_t *command) {
    cJSON *apps = cJSON_CreateArray();
    char *json = NULL;
    int ret;

    app_center_ensure_package_entries_loaded();
    ESP_LOGI(TAG, "Desktop requested installed app list: command=%s", command != NULL ? command->command_id : "(none)");

    if (apps == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int index = 0; index < s_entry_count; ++index) {
        cJSON *item;
        if (app_center_legacy_espnow_requested(s_entries[index].id, s_entries[index].name)) {
            app_center_cleanup_legacy_espnow_remote_state();
            continue;
        }
        if (!s_entries[index].installed) {
            continue;
        }
        if (s_entries[index].type == APP_CENTER_ENTRY_PACKAGE && !app_center_package_exists(s_entries[index].id)) {
            continue;
        }
        item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(apps);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(item, "app_id", s_entries[index].id);
        cJSON_AddStringToObject(item, "name", s_entries[index].name);
        cJSON_AddStringToObject(item, "version", s_entries[index].version);
        cJSON_AddStringToObject(
            item, "type", s_entries[index].type == APP_CENTER_ENTRY_FIRMWARE_APP ? "firmware-app" : "manifest-app");
        cJSON_AddStringToObject(item, "state", "installed");
        cJSON_AddItemToArray(apps, item);
    }

    json = cJSON_PrintUnformatted(apps);
    cJSON_Delete(apps);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ret = ws_send_app_package_list_json(command != NULL ? command->command_id : NULL, json);
    cJSON_free(json);
    return ret >= 0 ? ESP_OK : ESP_FAIL;
}

static void app_center_button_clicked(void) {
    if (app_center_button_click_ignored()) {
        return;
    }

    if (app_center_handle_wifi_gate_action("button")) {
        return;
    }

    bool exit_visible = false;
    app_center_exit_button_action_t exit_action;
    lvgl_port_lock(0);
    exit_visible = app_center_exit_is_visible_locked();
    if (exit_visible) {
        app_center_hide_exit_locked();
    }
    lvgl_port_unlock();
    exit_action = app_center_exit_policy_button_action(app_center_exit_page_from_current(), exit_visible);
    if (exit_action == APP_CENTER_EXIT_BUTTON_RETURN_TO_LAUNCHER) {
        app_center_request_return_to_launcher("button exit");
        return;
    }
    if (exit_action == APP_CENTER_EXIT_BUTTON_SHOW_EXIT) {
        if (wifi_has_credentials() == 1 && wifi_is_connected() != 1) {
            (void)wifi_resume_background();
            app_center_set_remote_status("Waiting for Wi-Fi to load apps");
        }
        lvgl_port_lock(0);
        app_center_show_exit_locked();
        lvgl_port_unlock();
        return;
    }
    if (exit_action == APP_CENTER_EXIT_BUTTON_CANCEL_DOWNLOAD) {
        app_center_request_download_cancel("install button");
        return;
    }

    if (s_page == APP_CENTER_PAGE_LIST) {
        s_action_index = s_selected_index;
        s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
        s_uninstall_confirm_index = -1;
        s_page = APP_CENTER_PAGE_DETAIL;
        s_render_requested = true;
    } else if (s_page == APP_CENTER_PAGE_DETAIL) {
        if (s_detail_action == APP_CENTER_DETAIL_ACTION_SECONDARY) {
            s_detail_action = APP_CENTER_DETAIL_ACTION_PRIMARY;
            if (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) {
                if (s_uninstall_confirm_index == s_action_index) {
                    (void)app_center_uninstall_entry(s_action_index);
                } else {
                    s_uninstall_confirm_index = s_action_index;
                    app_center_set_remote_status("Tap Confirm remove to delete");
                    s_render_requested = true;
                }
            } else {
                s_uninstall_confirm_index = -1;
                s_page = APP_CENTER_PAGE_LIST;
                s_render_requested = true;
            }
        } else {
            if (s_action_index >= 0 && s_action_index < s_entry_count && s_entries[s_action_index].installed) {
                if (s_uninstall_confirm_index == s_action_index) {
                    s_uninstall_confirm_index = -1;
                    app_center_set_remote_status("Remove canceled");
                    s_render_requested = true;
                    return;
                }
                app_center_open_entry(s_action_index);
            } else {
                s_uninstall_confirm_index = -1;
                app_center_request_download(s_action_index);
            }
        }
    }
}

static void app_center_on_create(void) {
    app_center_ensure_http_client_lock();
    app_center_ensure_manager_snapshot_lock();
    ESP_LOGI(TAG, "App.Center created");
}

static void app_center_cancel_background_work(void) {
    s_app_center_active = false;
    (void)app_center_next_generation();
    s_pending_download_open = false;
    s_return_launcher_requested = false;
    s_render_requested = false;
    s_remote_fetch_started = false;
    s_remote_retry_after_tick = 0;
    s_remote_fetch_failure_count = 0;

    if (s_download_running) {
        app_center_request_download_cancel("App.Center close");
        ESP_LOGI(TAG, "App.Center requested active download cancellation on close");
    }
    app_center_cancel_http_client(&s_remote_fetch_client, "remote fetch");
    app_center_cancel_http_client(&s_download_client, "download");
}

static void app_center_on_open(void) {
    ESP_LOGI(TAG, "App.Center opening");
    (void)app_center_next_generation();
    s_app_center_active = true;
    s_selection = APP_CENTER_SELECTION_NONE;
    s_pending_download_open = false;
    s_return_launcher_requested = false;
    s_page = APP_CENTER_PAGE_LIST;
    s_uninstall_confirm_index = -1;
    ESP_LOGI(TAG, "App.Center initial page=list wifi_connected=%d credentials=%d", wifi_is_connected(),
             wifi_has_credentials());
    s_render_requested = false;
    s_remote_fetch_started = false;
    s_remote_retry_after_tick = 0;
    s_remote_fetch_failure_count = 0;
    s_exit_hide_at_tick = 0;
    s_wifi_gate_active = false;
    if (s_deps.set_wifi_gate_action_enabled != NULL) {
        s_deps.set_wifi_gate_action_enabled(true);
    }
    (void)app_center_prepare_package_storage();
    app_center_reset_entries();
    safe_copy(s_remote_status, sizeof(s_remote_status), "Remote list not loaded");

    app_center_ensure_wifi_resume();
    if (app_center_show_wifi_gate_if_needed()) {
        app_center_start_remote_fetch_once();
        ESP_LOGI(TAG, "App.Center waiting for Wi-Fi gate");
        return;
    }

    lvgl_port_lock(0);
    app_center_render_locked();
    lvgl_port_unlock();

    /*
     * App.Center is driven by the LVGL encoder group. Do not also register a
     * BSP single-click callback here: both paths are backed by the same knob
     * button, so one physical click can otherwise enter the detail page and
     * immediately trigger Open/Download on that new page.
     */

    app_center_start_remote_fetch_once();
    ESP_LOGI(TAG, "App.Center ready");
}

static void app_center_on_tick(void) {
    bool wifi_gate_was_active = s_wifi_gate_active;
    app_center_exit_tick_action_t exit_action;

    app_center_ensure_wifi_resume();
    exit_action = app_center_exit_policy_tick_action(s_return_launcher_requested, s_download_running);
    if (exit_action == APP_CENTER_EXIT_TICK_CANCEL_DOWNLOAD) {
        app_center_request_download_cancel("return to launcher");
    } else if (exit_action == APP_CENTER_EXIT_TICK_RETURN_TO_LAUNCHER) {
        s_return_launcher_requested = false;
        ESP_LOGI(TAG, "App.Center returning to Launcher");
        (void)watcher_app_open("launcher");
        return;
    }

    if (s_exit_button != NULL && s_exit_hide_at_tick != 0 && xTaskGetTickCount() >= s_exit_hide_at_tick) {
        lvgl_port_lock(0);
        app_center_hide_exit_locked();
        lvgl_port_unlock();
    }

    if (app_center_show_wifi_gate_if_needed()) {
        return;
    }
    if (wifi_gate_was_active) {
        s_page = APP_CENTER_PAGE_LIST;
        app_center_set_remote_status("Wi-Fi ready. Loading apps...");
        s_render_requested = true;
        ESP_LOGI(TAG, "App.Center Wi-Fi gate cleared; rendering list");
    }

    if (s_pending_download_open) {
        int index = s_action_index;
        s_pending_download_open = false;
        app_center_start_download_task(index);
    }

    if (!s_remote_fetch_started && !s_remote_fetch_running) {
        app_center_start_remote_fetch_once();
    }

    if (s_render_requested) {
        s_render_requested = false;
        lvgl_port_lock(0);
        if (s_page == APP_CENTER_PAGE_INSTALL && s_screen != NULL && s_install_progress_bar != NULL &&
            s_download_running) {
            app_center_update_install_locked();
        } else {
            app_center_render_locked();
        }
        lvgl_port_unlock();
    }
}

static bool app_center_area_fits_round_screen_locked(const lv_area_t *area) {
    lv_area_t screen_area = {
        .x1 = 0,
        .y1 = 0,
        .x2 = 411,
        .y2 = 411,
    };
    int32_t center_x2;
    int32_t center_y2;
    int32_t radius_x2;
    int32_t corners[4][2];

    if (area == NULL) {
        return false;
    }
    if (s_screen != NULL) {
        lv_obj_get_coords(s_screen, &screen_area);
    }

    center_x2 = (int32_t)screen_area.x1 + (int32_t)screen_area.x2;
    center_y2 = (int32_t)screen_area.y1 + (int32_t)screen_area.y2;
    radius_x2 = ((int32_t)(screen_area.x2 - screen_area.x1) < (int32_t)(screen_area.y2 - screen_area.y1))
                    ? (int32_t)(screen_area.x2 - screen_area.x1 + 1)
                    : (int32_t)(screen_area.y2 - screen_area.y1 + 1);
    corners[0][0] = area->x1;
    corners[0][1] = area->y1;
    corners[1][0] = area->x2;
    corners[1][1] = area->y1;
    corners[2][0] = area->x1;
    corners[2][1] = area->y2;
    corners[3][0] = area->x2;
    corners[3][1] = area->y2;

    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        const int64_t dx2 = ((int64_t)corners[i][0] * 2) - center_x2;
        const int64_t dy2 = ((int64_t)corners[i][1] * 2) - center_y2;
        if ((dx2 * dx2) + (dy2 * dy2) > (int64_t)radius_x2 * radius_x2) {
            return false;
        }
    }
    return true;
}

static void app_center_debug_log_obj_locked(const char *stage, const char *name, lv_obj_t *obj, bool check_round_fit) {
    lv_area_t area;
    bool hidden = false;
    bool round_fit = !check_round_fit;

    if (obj == NULL) {
        ESP_LOGI(TAG, "evt=app_center_ui_obj stage=%s name=%s present=0 round_check=%d round_fit=%d",
                 stage != NULL ? stage : "status", name != NULL ? name : "unknown", 0, 1);
        return;
    }

    lv_obj_get_coords(obj, &area);
    hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (check_round_fit) {
        round_fit = app_center_area_fits_round_screen_locked(&area);
    }

    ESP_LOGI(TAG,
             "evt=app_center_ui_obj stage=%s name=%s present=1 hidden=%d x1=%ld y1=%ld x2=%ld y2=%ld w=%ld h=%ld "
             "round_check=%d round_fit=%d",
             stage != NULL ? stage : "status", name != NULL ? name : "unknown", hidden ? 1 : 0, (long)area.x1,
             (long)area.y1, (long)area.x2, (long)area.y2, (long)lv_obj_get_width(obj), (long)lv_obj_get_height(obj),
             check_round_fit ? 1 : 0, round_fit ? 1 : 0);
}

void app_center_debug_log_ui_snapshot(const char *stage) {
    lv_obj_t *selected_card = NULL;

    if (!s_app_center_active) {
        ESP_LOGI(TAG, "evt=app_center_ui_snapshot stage=%s active=0 page=%s", stage != NULL ? stage : "status",
                 app_center_page_name(s_page));
        return;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "evt=app_center_ui_snapshot stage=%s active=1 lock=timeout page=%s",
                 stage != NULL ? stage : "status", app_center_page_name(s_page));
        return;
    }

    ESP_LOGI(TAG,
             "evt=app_center_ui_snapshot stage=%s active=1 page=%s entries=%d selected=%d action=%d remote=\"%s\" "
             "download=\"%s\" progress=%d running=%d",
             stage != NULL ? stage : "status", app_center_page_name(s_page), s_entry_count, s_selected_index,
             s_action_index, s_remote_status, s_download_status, app_center_clamp_progress(s_download_progress),
             s_download_running ? 1 : 0);
    app_center_debug_log_obj_locked(stage, "screen", s_screen, false);
    app_center_debug_log_obj_locked(stage, "status_panel", s_status_panel, false);
    app_center_debug_log_obj_locked(stage, "status_icon", s_status_icon, true);
    app_center_debug_log_obj_locked(stage, "status_title", s_status_title_label, true);
    app_center_debug_log_obj_locked(stage, "status_accent", s_install_status_label, true);
    app_center_debug_log_obj_locked(stage, "status_body", s_status_body_label, true);
    app_center_debug_log_obj_locked(stage, "progress_bar", s_install_progress_bar, true);
    app_center_debug_log_obj_locked(stage, "progress_label", s_install_progress_label, true);
    app_center_debug_log_obj_locked(stage, "download_button", s_download_cancel_button, true);
    if (s_selected_index >= 0 && s_selected_index < APP_CENTER_MAX_ENTRIES) {
        selected_card = s_entry_cards[s_selected_index];
    }
    app_center_debug_log_obj_locked(stage, "list_title", s_list_title_label, true);
    app_center_debug_log_obj_locked(stage, "list_status", s_list_status_label, true);
    app_center_debug_log_obj_locked(stage, "list_card", selected_card, true);
    app_center_debug_log_obj_locked(stage, "list_card_name", s_list_card_name_label, true);
    app_center_debug_log_obj_locked(stage, "list_card_state", s_list_card_state_label, true);
    app_center_debug_log_obj_locked(stage, "detail_panel", s_detail_panel, true);
    app_center_debug_log_obj_locked(stage, "detail_name", s_detail_name_label, true);
    app_center_debug_log_obj_locked(stage, "detail_state", s_detail_state_label, true);
    app_center_debug_log_obj_locked(stage, "detail_body", s_detail_body_label, true);
    app_center_debug_log_obj_locked(stage, "detail_meta", s_detail_meta_label, true);
    app_center_debug_log_obj_locked(stage, "exit_button", s_exit_button, true);

    lvgl_port_unlock();
}

static void app_center_on_close(void) {
    if (s_deps.set_wifi_gate_action_enabled != NULL) {
        s_deps.set_wifi_gate_action_enabled(false);
    }
    app_center_clear_wifi_gate();
    app_center_cancel_background_work();
    if (!app_center_wait_background_idle(APP_CENTER_CLOSE_WAIT_MS)) {
        ESP_LOGW(TAG, "App.Center background work still exiting after close wait: remote=%d download=%d",
                 s_remote_fetch_running ? 1 : 0, s_download_running ? 1 : 0);
    }

    lvgl_port_lock(0);
    app_center_clear_group();
    if (s_exit_button != NULL) {
        lv_obj_del(s_exit_button);
        s_exit_button = NULL;
    }
    s_exit_hide_at_tick = 0;
    if (s_screen != NULL) {
        if (lv_disp_get_scr_act(NULL) != s_screen) {
            lv_obj_del(s_screen);
        }
        s_screen = NULL;
    }
    lvgl_port_unlock();
}

static esp_err_t app_center_on_destroy(void) {
    if (!app_center_background_work_idle()) {
        return ESP_ERR_INVALID_STATE;
    }
    app_center_close_ws_package_transfer();
    memset(s_entries, 0, sizeof(s_entries));
    if (app_center_take_manager_snapshot_lock()) {
        memset(&s_manager_snapshot_cache, 0, sizeof(s_manager_snapshot_cache));
        s_manager_snapshot_cache_valid = false;
        app_center_give_manager_snapshot_lock();
    }
    s_entry_count = 0;
    s_selection = APP_CENTER_SELECTION_NONE;
    s_selected_index = 0;
    s_action_index = 0;
    s_previous_index = -1;
    s_transition_dir = 0;
    s_uninstall_confirm_index = -1;
    ESP_LOGI(TAG, "App.Center destroyed");
    return ESP_OK;
}

void app_center_process_connect_action_click(void) {
    (void)app_center_handle_wifi_gate_action("touch");
}

static watcher_input_context_t app_center_input_context(void) {
    return s_wifi_gate_active ? WATCHER_INPUT_CONTEXT_APP_ACTION : WATCHER_INPUT_CONTEXT_LVGL_NAV;
}

static const watcher_app_t s_app_center_app = {
    .id = "app.center",
    .name = "App.Center",
    .icon = "center",
    .theme_color = 0x2D7FF9,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_APP_CENTER,
    .lifecycle = WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE,
    .on_create = app_center_on_create,
    .on_open = app_center_on_open,
    .on_tick = app_center_on_tick,
    .on_close = app_center_on_close,
    .on_destroy = app_center_on_destroy,
    .input_context = WATCHER_INPUT_CONTEXT_LVGL_NAV,
    .get_input_context = app_center_input_context,
    .on_button = app_center_button_clicked,
    .on_touch = NULL,
};

esp_err_t app_center_wait_for_selection(app_center_selection_t *out_selection) {
    if (out_selection == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_center_on_open();
    while (s_selection == APP_CENTER_SELECTION_NONE) {
        app_center_on_tick();
        vTaskDelay(pdMS_TO_TICKS(APP_CENTER_WAIT_DELAY_MS));
    }

    *out_selection = s_selection;
    return ESP_OK;
}

void app_center_release_after_launch(void) {
    app_center_on_close();
}

const watcher_app_t *app_center_get_app(void) {
    return &s_app_center_app;
}
