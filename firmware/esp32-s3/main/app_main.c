#include "esp_app_desc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "anim_player.h"
#include "anim_storage.h"
#include "behavior_state_service.h"
#include "ble_service.h"
#include "boot_anim.h"
#include "button_shutdown_low_power.h"
#include "button_shutdown_sequence.h"
#include "bsp_watcher.h"
// #include "camera_service.h"
#include "control_ingress.h"
#include "debug_cli.h"
#include "debug_touch_guard.h"
#include "discovery_client.h"
#include "display_ui.h"
#include "esp_lvgl_port.h"
#include "hal_display.h"
#include "hal_servo.h"
#include "mcu_led_service.h"
#include "mcu_link_bootstrap.h"
#include "mcu_motion_service.h"
#include "mcu_power_service.h"
#include "mcu_sensor_service.h"
#include "mem_monitor.h"
#include "ota_service.h"
#include "power_monitor_service.h"
#include "sensecap-watcher.h"
#include "sfx_service.h"
#include "stress_mode.h"
#include "voice_service.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "ws_handlers.h"
#include "ws_router.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MAIN"
#define MCU_OBS_TAG "MCU_OBS"

/* Physical restart: click count to trigger reboot */
#define RESTART_CLICK_COUNT 4
#define BUTTON_SHUTDOWN_HOLD_MS 3000
#define STM32_POWER_OFF_SETTLE_MS 300
#define ESP32_SHUTDOWN_FALLBACK_SLEEP_MS 800
#define ESP32_SHUTDOWN_WAKE_AFTER_SEC 0
#define ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS 3000
#define ESP32_SHUTDOWN_WAKE_IDLE_POLL_MS 50
#define STARTUP_BEHAVIOR_POLL_MS 50
#define STARTUP_BEHAVIOR_TIMEOUT_MS 10000
#define STARTUP_LED_RETRY_MS 1000
#define READY_IDLE_VARIANT_COUNT 4
#define READY_IDLE_ROUNDS_BEFORE_SLEEP 5
#define READY_IDLE_MIN_VARIANT_DURATION_MS 1000
#define READY_IDLE_FALLBACK_RETRY_MS 10000
#define READY_IDLE_MEMORY_RETRY_MS 3000
#define READY_IDLE_POST_HAPPY_MIN_DELAY_MS 1200
#define READY_IDLE_STANDBY_HANDOFF_TIMEOUT_MS 1000
#define READY_IDLE_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)
#define READY_IDLE_MIN_DMA_LARGEST_BYTES (12U * 1024U)
#define READY_IDLE_MEMORY_PRESSURE_LOG_INTERVAL_US 10000000LL
#define READY_IDLE_MEMORY_FORCE_STANDBY_DEFERS 3U
#define CLOUD_DISCOVERY_TIMEOUT_MS 5000
#define CLOUD_DISCOVERY_TASK_STACK_BYTES 8192
#define CLOUD_RETRY_DELAY_MS 2000
#define CLOUD_PROTOCOL_RETRY_DELAY_MS 5000
#define CACHED_WS_CONNECT_TIMEOUT_MS 5000U
#define CACHED_WS_URL_MAX_LEN 128U
#define WIFI_RESUME_MIN_INTERNAL_FREE_BYTES (28U * 1024U)
#define WIFI_RESUME_MIN_INTERNAL_LARGEST_BYTES (16U * 1024U)
#define WIFI_RESUME_RECOVERY_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)
#define WIFI_RESUME_LOW_MEMORY_RECOVERY_DEFERS 3U
#define WS_START_MIN_INTERNAL_FREE_BYTES (30U * 1024U)
#define WS_START_MIN_INTERNAL_LARGEST_BYTES (15U * 1024U)
#define WS_START_RECOVERY_MIN_INTERNAL_LARGEST_BYTES (14U * 1024U)
#define WS_START_LOW_MEMORY_RECOVERY_DEFERS 3U
#define WS_START_DISPLAY_SETTLE_MS 150U
#define CLOUD_RUNTIME_MIN_INTERNAL_FREE_BYTES (24U * 1024U)
#define CLOUD_RUNTIME_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)
#define MCU_HANDSHAKE_UI_TIMEOUT_MS 5000U
#define BLE_CONNECTED_FEEDBACK_STATE_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_ANIM_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_SOUND_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_ACTION_ID "bluetooth"
#define TOUCH_FONDLE_STATE_ID "fondle_love"
#define TOUCH_FONDLE_ANIM_ID "fondle_love"
#define TOUCH_FONDLE_ACTION_ID "fondle_love"
#define TOUCH_FONDLE_INVALID_ID 0xFFu
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
#define MCU_LINK_RUNTIME_MAX_EVENTS_PER_TICK 64
#define MCU_LINK_RUNTIME_TASK_PERIOD_MS 2
#define MAIN_LOOP_DELAY_MS 10
#else
#define MCU_LINK_RUNTIME_MAX_EVENTS_PER_TICK 4
#define MAIN_LOOP_DELAY_MS 100
#endif
// #define CAMERA_DIAG_MIN_INTERNAL_FREE_BYTES (48U * 1024U)
// #define CAMERA_DIAG_MIN_INTERNAL_LARGEST_BYTES (16U * 1024U)
#ifdef CONFIG_WATCHER_ANIM_FPS
#define BOOT_ANIM_INTERVAL_MS (1000U / CONFIG_WATCHER_ANIM_FPS)
#else
#define BOOT_ANIM_INTERVAL_MS 100U
#endif

typedef enum {
    TRANSPORT_BLE_ACTIVE = 0,
    TRANSPORT_BLE_IDLE_NO_CREDENTIALS,
    TRANSPORT_BLE_IDLE_WIFI_STARTING,
    TRANSPORT_BLE_IDLE_WIFI_CONNECTING,
    TRANSPORT_BLE_IDLE_DISCOVERING,
    TRANSPORT_BLE_IDLE_WS_CONNECTING,
    TRANSPORT_BLE_IDLE_CLOUD_READY,
    TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED,
} transport_state_t;

typedef enum {
    IDLE_HINT_READY = 0,
    IDLE_HINT_BLE_CONNECTED,
    IDLE_HINT_WIFI_SETUP_REQUIRED,
    IDLE_HINT_WIFI_RECOVERING,
    IDLE_HINT_WIFI_FAILED,
    IDLE_HINT_CLOUD_CONNECTING,
    IDLE_HINT_STM32_HANDSHAKE_FAILED,
} idle_hint_mode_t;

typedef struct {
    const char *text;
    int font_size;
    bool alert;
} idle_hint_view_t;

typedef struct {
    uint32_t generation;
    int status;
    server_info_t info;
} discovery_result_t;

static bool s_waiting_for_wifi_provision = false;
static bool s_boot_completed = false;
static bool s_ui_ready = false;
static bool s_cloud_runtime_started = false;
static bool s_ws_router_ready = false;
static bool s_ws_stack_ready = false;
static bool s_discovery_initialized = false;
static bool s_last_ble_connected = false;
static volatile bool s_ble_connected_feedback_pending = false;
static bool s_wifi_failed_since_last_success = false;
static bool s_low_memory_recovery_active = false;
static bool s_ble_advertising_paused_for_recovery = false;
static bool s_cached_ws_attempted_since_wifi_restore = false;
static bool s_cached_ws_connect_inflight = false;
static transport_state_t s_transport_state = TRANSPORT_BLE_IDLE_NO_CREDENTIALS;
static volatile bool s_discovery_inflight = false;
static uint32_t s_consecutive_wifi_resume_defers = 0;
static uint32_t s_consecutive_ws_start_defers = 0;
static uint32_t s_discovery_generation = 0;
static int64_t s_next_cloud_attempt_us = 0;
static int64_t s_cached_ws_connect_started_us = 0;
static int64_t s_wifi_recovery_started_us = 0;
static QueueHandle_t s_discovery_result_queue = NULL;
static char s_cached_ws_url[CACHED_WS_URL_MAX_LEN] = {0};
static int s_ready_idle_variant_index = -1;
static int64_t s_ready_idle_next_switch_us = 0;
static uint32_t s_ready_idle_completed_rounds = 0;
static bool s_ready_idle_sleeping = false;
static int64_t s_ready_idle_after_happy_us = 0;
static int64_t s_ready_idle_happy_observed_us = 0;
static bool s_ready_idle_happy_defer_logged = false;
static bool s_ready_idle_variant_unavailable_logged[READY_IDLE_VARIANT_COUNT] = {0};
static bool s_ready_idle_all_unavailable_logged = false;
static bool s_ready_idle_standby_transition_pending = false;
static int64_t s_ready_idle_standby_transition_deadline_us = 0;
static int64_t s_ready_idle_last_memory_pressure_log_us = 0;
static uint32_t s_ready_idle_memory_defers = 0;
static bool s_mcu_obs_state_initialized = false;
static mcu_link_state_t s_last_mcu_obs_state = MCU_LINK_STATE_DOWN;
static bool s_mcu_obs_stats_initialized = false;
static mcu_link_stats_t s_last_mcu_obs_stats = {0};
static int64_t s_last_mcu_obs_stats_log_us = 0;
static bool s_startup_green_leds_applied = false;
static int64_t s_startup_green_leds_last_attempt_us = 0;
static volatile bool s_shutdown_in_progress = false;
static TaskHandle_t s_shutdown_task = NULL;
static uint32_t s_shutdown_power_seq = 0;
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
static TaskHandle_t s_mcu_link_runtime_task = NULL;
#endif

static uint16_t decode_u16_le(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

static uint32_t decode_u32_le(const uint8_t *src) {
    return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) | ((uint32_t)src[3] << 24u);
}

static const char *mcu_link_state_to_string(mcu_link_state_t state) {
    switch (state) {
    case MCU_LINK_STATE_DOWN:
        return "DOWN";
    case MCU_LINK_STATE_HANDSHAKING:
        return "HANDSHAKING";
    case MCU_LINK_STATE_LINK_READY:
        return "LINK_READY";
    case MCU_LINK_STATE_READY:
        return "READY";
    case MCU_LINK_STATE_DEGRADED:
        return "DEGRADED";
    case MCU_LINK_STATE_RECOVERING:
        return "RECOVERING";
    default:
        return "UNKNOWN";
    }
}

static void log_mcu_obs_stats(mcu_link_t *link, const char *reason) {
    mcu_link_stats_t stats = {0};

    if (link == NULL || mcu_link_copy_stats(link, &stats) != ESP_OK) {
        return;
    }

    ESP_LOGI(MCU_OBS_TAG,
             "evt=stats reason=%s link_state=%s ack_timeout_count=%lu reconnect_count=%lu "
             "motion_done_fault_count=%lu dropped_state_count=%lu crc_error_count=%lu",
             reason ? reason : "unspecified", mcu_link_state_to_string(mcu_link_get_state(link)),
             (unsigned long)stats.ack_timeout_count, (unsigned long)stats.reconnect_count,
             (unsigned long)stats.motion_done_fault_count, (unsigned long)stats.dropped_state_count,
             (unsigned long)stats.crc_error_count);

    s_last_mcu_obs_stats = stats;
    s_last_mcu_obs_stats_log_us = esp_timer_get_time();
    s_mcu_obs_stats_initialized = true;
}

static void maybe_log_mcu_obs_stats(mcu_link_t *link, const char *reason, bool force) {
    mcu_link_stats_t current_stats = {0};
    bool changed;
    bool periodic_due;

    if (link == NULL || mcu_link_copy_stats(link, &current_stats) != ESP_OK) {
        return;
    }

    changed = !s_mcu_obs_stats_initialized || memcmp(&current_stats, &s_last_mcu_obs_stats, sizeof(current_stats)) != 0;
    periodic_due = s_last_mcu_obs_stats_log_us == 0 ||
                   (esp_timer_get_time() - s_last_mcu_obs_stats_log_us) >= (5LL * 1000LL * 1000LL);

#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    if (!force && reason != NULL && strcmp(reason, "event_processed") == 0) {
        return;
    }

    if (!force && reason != NULL && strcmp(reason, "periodic") == 0 && !periodic_due) {
        return;
    }
#endif

    if (force || changed || periodic_due) {
        log_mcu_obs_stats(link, reason);
    }
}

static void maybe_log_mcu_obs_state_transition(mcu_link_t *link, const char *reason) {
    const mcu_link_state_t current_state = (link != NULL) ? mcu_link_get_state(link) : MCU_LINK_STATE_DOWN;

    if (!s_mcu_obs_state_initialized || current_state != s_last_mcu_obs_state) {
        s_last_mcu_obs_state = current_state;
        s_mcu_obs_state_initialized = true;
        maybe_log_mcu_obs_stats(link, reason ? reason : "state_transition", true);
    }
}

static const char *transport_state_to_string(transport_state_t state) {
    switch (state) {
    case TRANSPORT_BLE_ACTIVE:
        return "BLE_ACTIVE";
    case TRANSPORT_BLE_IDLE_NO_CREDENTIALS:
        return "BLE_IDLE_NO_CREDENTIALS";
    case TRANSPORT_BLE_IDLE_WIFI_STARTING:
        return "BLE_IDLE_WIFI_STARTING";
    case TRANSPORT_BLE_IDLE_WIFI_CONNECTING:
        return "BLE_IDLE_WIFI_CONNECTING";
    case TRANSPORT_BLE_IDLE_DISCOVERING:
        return "BLE_IDLE_DISCOVERING";
    case TRANSPORT_BLE_IDLE_WS_CONNECTING:
        return "BLE_IDLE_WS_CONNECTING";
    case TRANSPORT_BLE_IDLE_CLOUD_READY:
        return "BLE_IDLE_CLOUD_READY";
    case TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED:
    default:
        return "BLE_IDLE_CLOUD_SUSPENDED";
    }
}

static int ready_idle_post_happy_delay_ms(void) {
    int duration_ms = emoji_get_loop_duration_ms(EMOJI_ANIM_HAPPY);
    if (duration_ms < READY_IDLE_POST_HAPPY_MIN_DELAY_MS) {
        duration_ms = READY_IDLE_POST_HAPPY_MIN_DELAY_MS;
    }
    return duration_ms;
}

static void update_ready_idle_handoff_deadline(transport_state_t state) {
    if (state == TRANSPORT_BLE_IDLE_CLOUD_READY) {
        int duration_ms = ready_idle_post_happy_delay_ms();
        s_ready_idle_variant_index = -1;
        s_ready_idle_next_switch_us = 0;
        s_ready_idle_completed_rounds = 0;
        s_ready_idle_sleeping = false;
        s_ready_idle_after_happy_us = esp_timer_get_time() + (int64_t)duration_ms * 1000LL;
        s_ready_idle_happy_defer_logged = false;
        s_ready_idle_standby_transition_pending = false;
        s_ready_idle_standby_transition_deadline_us = 0;
        ESP_LOGI(TAG, "Ready idle handoff armed after happy duration=%dms", duration_ms);
        return;
    }

    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_completed_rounds = 0;
    s_ready_idle_sleeping = false;
    s_ready_idle_after_happy_us = 0;
    s_ready_idle_happy_observed_us = 0;
    s_ready_idle_happy_defer_logged = false;
    s_ready_idle_standby_transition_pending = false;
    s_ready_idle_standby_transition_deadline_us = 0;
}

static void transport_set_state(transport_state_t state, const char *reason) {
    if (state == s_transport_state) {
        return;
    }

    ESP_LOGI(TAG, "Transport state: %s -> %s (%s)", transport_state_to_string(s_transport_state),
             transport_state_to_string(state), reason ? reason : "no reason");
    s_transport_state = state;
    update_ready_idle_handoff_deadline(state);
}

static void transport_schedule_retry(uint32_t delay_ms) {
    s_next_cloud_attempt_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000LL);
}

static bool transport_retry_due(void) {
    return s_next_cloud_attempt_us == 0 || esp_timer_get_time() >= s_next_cloud_attempt_us;
}

static void log_firmware_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Firmware version: project=%s version=%s idf=%s", app_desc->project_name, app_desc->version,
             app_desc->idf_ver);
}

static void log_startup_banner(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "WatcheRobot S3 %s starting", app_desc != NULL ? app_desc->version : "unknown");
}

static void log_ble_mac_at_boot(const char *stage) {
    char mac_str[18] = {0};
    esp_err_t ret = ble_service_get_local_mac(mac_str, sizeof(mac_str));
    char boot_detail[32] = {0};

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BLE MAC @ %s: %s", stage ? stage : "boot", mac_str);
        snprintf(boot_detail, sizeof(boot_detail), "BLE %s", mac_str);
        boot_anim_set_detail_text(boot_detail);
    } else {
        ESP_LOGW(TAG, "BLE MAC unavailable @ %s: %s", stage ? stage : "boot", esp_err_to_name(ret));
        boot_anim_set_detail_text("BLE unavailable");
    }
}

static const char *get_wifi_setup_hint_text(void) {
    static char hint_text[96];
    char mac_str[18] = {0};

    if (ble_service_get_local_mac(mac_str, sizeof(mac_str)) == ESP_OK) {
        snprintf(hint_text, sizeof(hint_text), "Reconnect BLE to set Wi-Fi\n%s", mac_str);
        return hint_text;
    }

    return "Reconnect BLE to set Wi-Fi";
}

static void configure_runtime_log_levels(void) {
#if CONFIG_WATCHER_RUNTIME_QUIET_LOGS
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set(MCU_OBS_TAG, ESP_LOG_WARN);
    esp_log_level_set("MEM_MON", ESP_LOG_INFO);
    esp_log_level_set("BSP", ESP_LOG_INFO);
    esp_log_level_set("VOICE", ESP_LOG_INFO);
    esp_log_level_set("HAL_AUDIO", ESP_LOG_INFO);
    esp_log_level_set("HAL_WAKE_WORD", ESP_LOG_INFO);
    esp_log_level_set("SFX_SERVICE", ESP_LOG_INFO);
    esp_log_level_set("WS_CLIENT", ESP_LOG_INFO);
#if CONFIG_WATCHER_ANIM_DEBUG_PERF
    esp_log_level_set("ANIM_PLAYER", ESP_LOG_INFO);
    esp_log_level_set("DISPLAY_UI", ESP_LOG_INFO);
    esp_log_level_set("BEHAVIOR_STATE", ESP_LOG_INFO);
#endif
#endif
}

static void on_ble_connection_changed(bool connected);
static void transport_cancel_discovery(const char *reason);
static void transport_stop_ws(const char *reason);
static void transport_set_ble_recovery_advertising_paused(bool paused, const char *reason);
static void transport_suspend_cloud_runtime_for_low_memory(const char *reason);
static void transport_enter_low_memory_recovery(const char *reason);
static void transport_reset_low_memory_recovery(const char *reason);
static int transport_prepare_ws_client(const char *ws_url);
static void transport_reset_cached_ws_resume_state(void);
static void wait_for_behavior_idle(uint32_t timeout_ms);
static void maybe_apply_startup_green_leds(void);
static void maybe_play_ble_connected_feedback(void);
static void boot_halt_with_error(const char *error_msg);
static void log_directory_contents(const char *path);
static int boot_prepare_animation_assets(void);
static bool s_touch_fondle_press_latched = false;
static uint8_t s_touch_fondle_latched_id = TOUCH_FONDLE_INVALID_ID;
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
static void ensure_mcu_link_runtime_task_started(void);
#endif

#define LOG_HEAP_STATE(stage) mem_monitor_snapshot(stage)

static bool transport_has_wifi_resume_headroom_with_log(bool log_failure) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t min_largest_internal = s_low_memory_recovery_active ? WIFI_RESUME_RECOVERY_MIN_INTERNAL_LARGEST_BYTES
                                                               : WIFI_RESUME_MIN_INTERNAL_LARGEST_BYTES;

    if (free_internal < WIFI_RESUME_MIN_INTERNAL_FREE_BYTES || largest_internal < min_largest_internal) {
        if (log_failure) {
            ESP_LOGW(TAG, "Deferring WiFi resume due to low internal heap: free=%u largest=%u (need >=%u / >=%u)%s",
                     (unsigned)free_internal, (unsigned)largest_internal, (unsigned)WIFI_RESUME_MIN_INTERNAL_FREE_BYTES,
                     (unsigned)min_largest_internal, s_low_memory_recovery_active ? " [low-memory recovery]" : "");
        }
        return false;
    }

    return true;
}

static bool transport_has_wifi_resume_headroom(void) {
    return transport_has_wifi_resume_headroom_with_log(true);
}

static bool transport_has_ws_start_headroom_with_log(bool log_failure) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t min_largest_internal = s_low_memory_recovery_active ? WS_START_RECOVERY_MIN_INTERNAL_LARGEST_BYTES
                                                               : WS_START_MIN_INTERNAL_LARGEST_BYTES;

    if (free_internal < WS_START_MIN_INTERNAL_FREE_BYTES || largest_internal < min_largest_internal) {
        if (log_failure) {
            ESP_LOGW(TAG, "Deferring WebSocket start due to low internal heap: free=%u largest=%u (need >=%u / >=%u)%s",
                     (unsigned)free_internal, (unsigned)largest_internal, (unsigned)WS_START_MIN_INTERNAL_FREE_BYTES,
                     (unsigned)min_largest_internal, s_low_memory_recovery_active ? " [low-memory recovery]" : "");
        }
        return false;
    }

    return true;
}

static bool transport_has_ws_start_headroom(void) {
    return transport_has_ws_start_headroom_with_log(true);
}

static bool transport_has_cloud_runtime_headroom(void) {
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (free_internal < CLOUD_RUNTIME_MIN_INTERNAL_FREE_BYTES ||
        largest_internal < CLOUD_RUNTIME_MIN_INTERNAL_LARGEST_BYTES) {
        ESP_LOGW(TAG, "Deferring cloud runtime due to low internal heap: free=%u largest=%u (need >=%u / >=%u)",
                 (unsigned)free_internal, (unsigned)largest_internal, (unsigned)CLOUD_RUNTIME_MIN_INTERNAL_FREE_BYTES,
                 (unsigned)CLOUD_RUNTIME_MIN_INTERNAL_LARGEST_BYTES);
        return false;
    }

    return true;
}

static void boot_halt_with_error(const char *error_msg) {
    const char *safe_error = (error_msg != NULL && error_msg[0] != '\0') ? error_msg : "Boot failed";

    ESP_LOGE(TAG, "Fatal boot error: %s", safe_error);
    boot_anim_show_error(safe_error);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void log_directory_contents(const char *path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    size_t entry_count = 0;

    if (path == NULL || path[0] == '\0') {
        return;
    }

    errno = 0;
    dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Unable to list %s: errno=%d (%s)", path, errno, strerror(errno));
        return;
    }

    ESP_LOGI(TAG, "Listing directory contents for %s", path);
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        ++entry_count;
        ESP_LOGI(TAG, "  [%u] %s", (unsigned)entry_count, entry->d_name);
    }

    if (errno != 0) {
        ESP_LOGW(TAG, "Directory iteration for %s ended with errno=%d (%s)", path, errno, strerror(errno));
    } else if (entry_count == 0) {
        ESP_LOGW(TAG, "Directory %s is empty", path);
    }

    closedir(dir);
}

static int boot_prepare_animation_assets(void) {
    int boot_frame_count;
    esp_err_t sd_ret;

    boot_anim_set_text("Mounting SD...");
    if (!bsp_sdcard_is_inserted()) {
        ESP_LOGE(TAG, "SD card is not inserted");
        boot_halt_with_error("Insert SD card");
    }

    sd_ret = bsp_sdcard_init_default();
    if (sd_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(sd_ret));
        boot_halt_with_error("SD mount failed");
    }
    log_directory_contents("/sdcard");
    log_directory_contents("/sdcard/anim");
    boot_anim_set_progress(5);

    boot_anim_set_text("Loading anim...");
    if (anim_catalog_init() != 0) {
        ESP_LOGE(TAG, "Failed to load SD animation manifest");
        boot_halt_with_error("Anim manifest missing");
    }
    boot_anim_set_progress(10);

    boot_frame_count = emoji_load_type(EMOJI_ANIM_BOOT);
    if (boot_frame_count <= 0) {
        ESP_LOGE(TAG, "Boot animation type is unavailable");
        boot_halt_with_error("Boot anim missing");
    }

    if (emoji_get_image(EMOJI_ANIM_BOOT, 0) == NULL) {
        ESP_LOGE(TAG, "Failed to load first boot animation frame");
        boot_halt_with_error("Boot anim corrupt");
    }

    return boot_frame_count;
}

static bool transport_is_valid_ws_url(const char *ws_url) {
    return ws_url != NULL && (strncmp(ws_url, "ws://", 5) == 0 || strncmp(ws_url, "wss://", 6) == 0);
}

static void transport_cache_ws_url(const char *ws_url, const char *reason) {
    if (!transport_is_valid_ws_url(ws_url)) {
        return;
    }

    if (strncmp(s_cached_ws_url, ws_url, sizeof(s_cached_ws_url)) == 0) {
        return;
    }

    strncpy(s_cached_ws_url, ws_url, sizeof(s_cached_ws_url) - 1U);
    s_cached_ws_url[sizeof(s_cached_ws_url) - 1U] = '\0';
    ESP_LOGI(TAG, "Cached last known WebSocket URL: %s (%s)", s_cached_ws_url, reason ? reason : "no reason");
}

static void transport_clear_cached_ws_url(const char *reason) {
    if (s_cached_ws_url[0] == '\0') {
        return;
    }

    ESP_LOGW(TAG, "Clearing cached WebSocket URL %s (%s)", s_cached_ws_url, reason ? reason : "no reason");
    s_cached_ws_url[0] = '\0';
}

static void transport_quiet_display_motion(void) {
    if (lvgl_port_lock(0)) {
        emoji_anim_stop();
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Paused emoji animation while bringing up cloud transport");
    }
}

static void transport_prepare_display_for_ws_start(void) {
    transport_quiet_display_motion();
    wait_for_behavior_idle(WS_START_DISPLAY_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(WS_START_DISPLAY_SETTLE_MS));
}

static void transport_reset_cached_ws_resume_state(void) {
    s_cached_ws_attempted_since_wifi_restore = false;
    s_cached_ws_connect_inflight = false;
    s_cached_ws_connect_started_us = 0;
}

static void transport_sync_boot_state(void) {
    s_last_ble_connected = ble_service_is_connected();
    s_ble_connected_feedback_pending = s_last_ble_connected;

    if (s_last_ble_connected) {
        s_transport_state = TRANSPORT_BLE_ACTIVE;
    } else if (wifi_has_credentials() == 1) {
        s_transport_state = TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED;
    } else {
        s_transport_state = TRANSPORT_BLE_IDLE_NO_CREDENTIALS;
    }
}

// #if CONFIG_WATCHER_CAMERA_BOOT_DIAG
// static bool has_internal_heap_headroom(size_t min_free_bytes, size_t min_largest_block_bytes) {
//     size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
//     size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
//
//     return free_internal >= min_free_bytes && largest_internal >= min_largest_block_bytes;
// }
// #endif

static void on_wifi_status_changed(wifi_status_t status, const char *ssid, const char *ip_addr) {
    switch (status) {
    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected: ssid=%s ip=%s", ssid ? ssid : "<unknown>", ip_addr ? ip_addr : "<no-ip>");
        s_wifi_failed_since_last_success = false;
        if (!s_low_memory_recovery_active) {
            transport_reset_low_memory_recovery("wifi connected");
        }
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Wi-Fi connected");
        }
        break;

    case WIFI_STATUS_CONNECTING:
        ESP_LOGI(TAG, "WiFi connecting: ssid=%s", ssid ? ssid : "<unknown>");
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Connecting Wi-Fi...");
        }
        break;

    case WIFI_STATUS_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi connect failed or disconnected: ssid=%s", ssid ? ssid : "<unknown>");
        if (!ble_service_is_connected() && wifi_has_credentials() == 1) {
            s_wifi_failed_since_last_success = true;
        }
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Wi-Fi failed");
        }
        break;

    case WIFI_STATUS_UNCONFIGURED:
    default:
        ESP_LOGI(TAG, "WiFi unconfigured");
        s_wifi_failed_since_last_success = false;
        transport_reset_low_memory_recovery("wifi unconfigured");
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Reconnect BLE to set Wi-Fi");
        }
        break;
    }
}

static void on_button_multi_click_restart(void) {
    ESP_LOGW(TAG, "Button %d-click - REBOOTING!", RESTART_CLICK_COUNT);
    behavior_state_set_with_text("error", "Rebooting...", 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static int shutdown_sequence_request_5v_off(void *ctx) {
    uint32_t *power_seq = (uint32_t *)ctx;
    const esp_err_t ret = mcu_power_set_5v_enabled(false, MCU_POWER_SOURCE_BEHAVIOR, power_seq);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STM32 5V power-off request failed: %s; continuing ESP32 shutdown", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "STM32 5V power-off request queued seq=%lu; shutting down ESP32 after %u ms",
                 (unsigned long)*power_seq, (unsigned)STM32_POWER_OFF_SETTLE_MS);
    }

    return (int)ret;
}

static void shutdown_sequence_delay(void *ctx, uint32_t delay_ms) {
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static int shutdown_sequence_system_shutdown(void *ctx) {
    (void)ctx;
    const esp_err_t ret = bsp_system_shutdown();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Shutdown GPIO request failed: %s", esp_err_to_name(ret));
    }

    return (int)ret;
}

static int shutdown_sequence_wake_line_level(void *ctx) {
    (void)ctx;
    return gpio_get_level(BSP_IO_EXPANDER_INT);
}

static void shutdown_sequence_enter_sleep(void *ctx, uint32_t wake_after_sec) {
    (void)ctx;
    bsp_system_deep_sleep(wake_after_sec);
}

static void shutdown_sequence_low_power_fallback(void *ctx) {
    const button_shutdown_low_power_ops_t sleep_ops = {
        .wake_line_level = shutdown_sequence_wake_line_level,
        .delay_ms = shutdown_sequence_delay,
        .enter_sleep = shutdown_sequence_enter_sleep,
        .ctx = ctx,
    };

    ESP_LOGE(TAG,
             "System power rail still alive after shutdown request; entering BSP deep sleep fallback "
             "(button wake, wait idle up to %u ms)",
             (unsigned)ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS);
    button_shutdown_low_power_enter(&sleep_ops, ESP32_SHUTDOWN_WAKE_AFTER_SEC, ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS,
                                    ESP32_SHUTDOWN_WAKE_IDLE_POLL_MS);
}

static bool run_button_shutdown_sequence(void) {
    button_shutdown_sequence_result_t result;
    const button_shutdown_sequence_ops_t ops = {
        .request_5v_off = shutdown_sequence_request_5v_off,
        .delay_ms = shutdown_sequence_delay,
        .system_shutdown = shutdown_sequence_system_shutdown,
        .low_power_fallback = shutdown_sequence_low_power_fallback,
        .ctx = &s_shutdown_power_seq,
    };

    if (s_shutdown_in_progress) {
        return false;
    }
    s_shutdown_in_progress = true;
    s_shutdown_power_seq = 0;

    ESP_LOGW(TAG, "Button long press - shutting down");
    behavior_state_set_with_text("error", "Shutting down...", 0);
    voice_recorder_stop();

    result = button_shutdown_sequence_run(&ops, STM32_POWER_OFF_SETTLE_MS, ESP32_SHUTDOWN_FALLBACK_SLEEP_MS);
    if (result.system_shutdown_status != ESP_OK) {
        s_shutdown_in_progress = false;
        return false;
    }

    return true;
}

static void button_shutdown_task(void *arg) {
    (void)arg;
    const bool shutdown_requested = run_button_shutdown_sequence();

    if (!shutdown_requested) {
        s_shutdown_task = NULL;
    }

    vTaskDelete(NULL);
}

static void on_button_shutdown_hold(void) {
    if (s_shutdown_in_progress || s_shutdown_task != NULL) {
        return;
    }

    ESP_LOGW(TAG, "Button %d ms hold detected; scheduling shutdown task", BUTTON_SHUTDOWN_HOLD_MS);
    if (xTaskCreate(button_shutdown_task, "button_shutdown", 4096, NULL, 5, &s_shutdown_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button shutdown task");
        s_shutdown_task = NULL;
    }
}

static void init_runtime_inputs_and_restart_path(void) {
    int input_ret;
    bool knob_ready;

    input_ret = hal_display_input_init();
    knob_ready = hal_display_has_knob_input();

    ESP_LOGI(TAG, "Delayed input init result=%d knob_ready=%d", input_ret, knob_ready ? 1 : 0);

    if (input_ret != 0) {
        ESP_LOGW(TAG, "Delayed display input initialization failed; continuing with degraded input availability");
    }

    if (knob_ready) {
        bsp_set_btn_long_press_ms_cb(BUTTON_SHUTDOWN_HOLD_MS, on_button_shutdown_hold);
        ESP_LOGI(TAG, "Registered %d ms shutdown hold callback", BUTTON_SHUTDOWN_HOLD_MS);
        bsp_set_btn_multi_click_cb(RESTART_CLICK_COUNT, on_button_multi_click_restart);
        ESP_LOGI(TAG, "Registered %d-click restart callback", RESTART_CLICK_COUNT);
    } else {
        ESP_LOGW(TAG, "Skipping %d-click restart callback because knob input is unavailable", RESTART_CLICK_COUNT);
    }
}

static void init_mcu_link_bootstrap(void) {
    esp_err_t ret;
    mcu_link_t *link;

    ret = mcu_link_bootstrap_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCU link bootstrap init failed: %s", esp_err_to_name(ret));
        return;
    }

    link = mcu_link_bootstrap_get_link();
    ret = mcu_link_bootstrap_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCU link bootstrap start failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "MCU link scaffold ready (present=%d state=%d link_ready=%d ready=%d)", link != NULL ? 1 : 0,
             (int)mcu_link_bootstrap_get_state(), mcu_link_bootstrap_is_link_ready() ? 1 : 0,
             mcu_link_bootstrap_is_ready() ? 1 : 0);
    ESP_LOGI(MCU_OBS_TAG, "evt=link_bootstrap_ready link_state=%s link_ready=%d ready=%d",
             mcu_link_state_to_string(mcu_link_bootstrap_get_state()), mcu_link_bootstrap_is_link_ready() ? 1 : 0,
             mcu_link_bootstrap_is_ready() ? 1 : 0);
    maybe_log_mcu_obs_state_transition(link, "bootstrap_ready");
}

static void init_mcu_runtime_services(void) {
    esp_err_t ret;

    ret = mcu_led_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU LED service init failed: %s", esp_err_to_name(ret));
        boot_halt_with_error("MCU LED init failed");
    }

    ret = mcu_sensor_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU sensor service init failed: %s", esp_err_to_name(ret));
        boot_halt_with_error("MCU sensor init failed");
    }

    ret = mcu_power_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCU power service init failed: %s", esp_err_to_name(ret));
        boot_halt_with_error("MCU power init failed");
    }

    stress_mode_init();
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    ensure_mcu_link_runtime_task_started();
#endif
}

static void maybe_complete_mcu_link_baseline_restore(const mcu_link_event_t *event) {
    mcu_link_t *link;
    esp_err_t ret;

    if (event == NULL || event->type != MCU_LINK_RX_EVENT_HELLO_RSP) {
        return;
    }

    link = mcu_link_bootstrap_get_link();
    if (link == NULL || !mcu_link_is_link_ready(link) || mcu_link_is_ready(link)) {
        return;
    }

    if (mcu_link_snapshot_supported(link)) {
        ESP_LOGW(TAG, "MCU link snapshot restore is not implemented yet; using explicit safe-default baseline");
    } else {
        ESP_LOGI(TAG, "MCU link restoring explicit safe-default baseline (no snapshot support)");
    }
    ESP_LOGI(MCU_OBS_TAG, "evt=baseline_restore_begin link_state=%s snapshot_supported=%d",
             mcu_link_state_to_string(mcu_link_get_state(link)), mcu_link_snapshot_supported(link) ? 1 : 0);

    ret = mcu_link_mark_baseline_synced(link);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCU link baseline restore failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "MCU link baseline restore completed (state=%d ready=%d)", (int)mcu_link_get_state(link),
             mcu_link_is_ready(link) ? 1 : 0);
    ESP_LOGI(MCU_OBS_TAG, "evt=baseline_restore_done link_state=%s ready=%d",
             mcu_link_state_to_string(mcu_link_get_state(link)), mcu_link_is_ready(link) ? 1 : 0);
    if (mcu_link_is_ready(link)) {
        stress_mode_notify_ready();
        maybe_apply_startup_green_leds();
        ESP_LOGI(MCU_OBS_TAG, "evt=ready link_state=%s", mcu_link_state_to_string(mcu_link_get_state(link)));
    }
    maybe_log_mcu_obs_state_transition(link, "baseline_restore_done");
}

static void maybe_handle_touch_behavior_event(const mcu_link_event_t *event, esp_err_t sensor_ret) {
    mcu_touch_state_t touch = {0};
    esp_err_t ret;

    if (event == NULL || event->type != MCU_LINK_RX_EVENT_TOUCH_EVENT) {
        return;
    }

    if (sensor_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch behavior skipped; sensor parse failed: %s", esp_err_to_name(sensor_ret));
        return;
    }

    ret = mcu_sensor_service_get_latest_touch(&touch);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch behavior skipped; latest touch unavailable: %s", esp_err_to_name(ret));
        return;
    }

    if (touch.event_code == MCU_TOUCH_EVENT_RELEASE) {
        if (s_touch_fondle_press_latched && s_touch_fondle_latched_id == touch.touch_id) {
            ESP_LOGI(TAG, "Touch press latch released touch_id=%u ts=%lu", (unsigned)touch.touch_id,
                     (unsigned long)touch.timestamp_ms);
        }
        s_touch_fondle_press_latched = false;
        s_touch_fondle_latched_id = TOUCH_FONDLE_INVALID_ID;
        return;
    }

    if (touch.event_code != MCU_TOUCH_EVENT_PRESS) {
        return;
    }

    if (debug_touch_guard_is_suppressed()) {
        ESP_LOGI(TAG, "Touch press suppressed during manual motion window touch_id=%u remaining_ms=%lu",
                 (unsigned)touch.touch_id, (unsigned long)debug_touch_guard_remaining_ms());
        return;
    }

    if (s_touch_fondle_press_latched && s_touch_fondle_latched_id == touch.touch_id) {
        ESP_LOGI(TAG, "Touch press ignored while latched touch_id=%u ts=%lu", (unsigned)touch.touch_id,
                 (unsigned long)touch.timestamp_ms);
        return;
    }
    s_touch_fondle_press_latched = true;
    s_touch_fondle_latched_id = touch.touch_id;

    if (behavior_state_is_action_active()) {
        ESP_LOGI(TAG, "Touch press skipped while action active touch_id=%u ts=%lu", (unsigned)touch.touch_id,
                 (unsigned long)touch.timestamp_ms);
        return;
    }

    if (behavior_state_is_busy()) {
        const char *current_state = behavior_state_get_current();
        ESP_LOGI(TAG, "Touch press skipped while behavior busy state=%s touch_id=%u ts=%lu",
                 current_state != NULL ? current_state : "<unknown>", (unsigned)touch.touch_id,
                 (unsigned long)touch.timestamp_ms);
        return;
    }

    if (!anim_catalog_has_type(EMOJI_ANIM_FONDLE_LOVE)) {
        ESP_LOGW(TAG, "Touch press ignored; %s animation is unavailable in SD manifest", TOUCH_FONDLE_ANIM_ID);
        return;
    }

    ret = behavior_state_set_with_resources_and_action(TOUCH_FONDLE_STATE_ID, "", 0, TOUCH_FONDLE_ANIM_ID, "",
                                                       TOUCH_FONDLE_ACTION_ID);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch press triggered %s touch_id=%u ts=%lu", TOUCH_FONDLE_STATE_ID, (unsigned)touch.touch_id,
                 (unsigned long)touch.timestamp_ms);
    } else {
        ESP_LOGW(TAG, "Touch press failed to trigger %s: %s", TOUCH_FONDLE_STATE_ID, esp_err_to_name(ret));
    }
}

static void dispatch_mcu_link_runtime_event(const mcu_link_event_t *event) {
    mcu_link_t *link;
    bool overwrote_latest = false;
    esp_err_t sensor_ret;

    if (event == NULL || event->type == MCU_LINK_RX_EVENT_NONE) {
        return;
    }

    link = mcu_link_bootstrap_get_link();
    switch (event->type) {
    case MCU_LINK_RX_EVENT_HELLO_RSP:
        ESP_LOGI(MCU_OBS_TAG, "evt=hello_rsp seq=%lu msg_class=%u msg_id=%u link_state=%s snapshot_supported=%d",
                 (unsigned long)event->frame.header.seq, (unsigned)event->frame.header.msg_class,
                 (unsigned)event->frame.header.msg_id, mcu_link_state_to_string(mcu_link_get_state(link)),
                 (link != NULL && mcu_link_snapshot_supported(link)) ? 1 : 0);
        break;
    case MCU_LINK_RX_EVENT_ACK:
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
        ESP_LOGI(MCU_OBS_TAG, "evt=ack ref_seq=%lu msg_class=%u msg_id=%u link_state=%s",
                 (unsigned long)decode_u32_le(event->frame.payload), (unsigned)event->frame.header.msg_class,
                 (unsigned)event->frame.header.msg_id, mcu_link_state_to_string(mcu_link_get_state(link)));
#endif
        break;
    case MCU_LINK_RX_EVENT_NACK:
        ESP_LOGI(MCU_OBS_TAG, "evt=nack ref_seq=%lu reason_code=0x%04x msg_class=%u msg_id=%u link_state=%s",
                 (unsigned long)decode_u32_le(event->frame.payload), (unsigned)decode_u16_le(&event->frame.payload[6]),
                 (unsigned)event->frame.header.msg_class, (unsigned)event->frame.header.msg_id,
                 mcu_link_state_to_string(mcu_link_get_state(link)));
        break;
    case MCU_LINK_RX_EVENT_FAULT:
        ESP_LOGI(MCU_OBS_TAG,
                 "evt=fault ref_seq=%lu fault_source=%u reason_code=0x%04x msg_class=%u msg_id=%u link_state=%s",
                 (unsigned long)decode_u32_le(event->frame.payload), (unsigned)event->frame.payload[4],
                 (unsigned)decode_u16_le(&event->frame.payload[5]), (unsigned)event->frame.header.msg_class,
                 (unsigned)event->frame.header.msg_id, mcu_link_state_to_string(mcu_link_get_state(link)));
        break;
    default:
        break;
    }
    maybe_complete_mcu_link_baseline_restore(event);
    (void)mcu_motion_service_handle_link_event(event);
    (void)mcu_led_service_handle_link_event(event);
    (void)mcu_power_service_handle_link_event(event);
    sensor_ret = mcu_sensor_service_handle_link_event(event, &overwrote_latest);
    maybe_handle_touch_behavior_event(event, sensor_ret);
    stress_mode_on_link_event(event);

    if (overwrote_latest &&
        (event->type == MCU_LINK_RX_EVENT_IMU_STATE || event->type == MCU_LINK_RX_EVENT_MAG_STATE)) {
        link = mcu_link_bootstrap_get_link();
        if (link != NULL) {
            (void)mcu_link_record_dropped_state(link);
        }
    }

    maybe_log_mcu_obs_state_transition(link, "event_processed");
    maybe_log_mcu_obs_stats(link, "event_processed", false);
}

static void service_mcu_link_runtime(void) {
    int processed = 0;

    while (processed < MCU_LINK_RUNTIME_MAX_EVENTS_PER_TICK) {
        mcu_link_event_t event = {0};
        esp_err_t ret = mcu_link_bootstrap_poll(&event);

        if (ret == ESP_OK) {
            dispatch_mcu_link_runtime_event(&event);
            processed++;
            continue;
        }

        if (ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_INVALID_STATE) {
            maybe_log_mcu_obs_stats(mcu_link_bootstrap_get_link(), "periodic", false);
            return;
        }

        ESP_LOGW(TAG, "MCU link runtime poll failed: %s", esp_err_to_name(ret));
        maybe_log_mcu_obs_stats(mcu_link_bootstrap_get_link(), "poll_error", true);
        return;
    }
}

static void maybe_apply_startup_green_leds(void) {
    int64_t now_us;
    esp_err_t ret;

    if (s_startup_green_leds_applied || !mcu_link_bootstrap_is_ready()) {
        return;
    }

    now_us = esp_timer_get_time();
    if (s_startup_green_leds_last_attempt_us != 0 &&
        (now_us - s_startup_green_leds_last_attempt_us) < ((int64_t)STARTUP_LED_RETRY_MS * 1000LL)) {
        return;
    }
    s_startup_green_leds_last_attempt_us = now_us;

    ret = mcu_led_submit_boot_green_baseline();
    if (ret == ESP_OK) {
        s_startup_green_leds_applied = true;
        ESP_LOGI(TAG, "Startup LED baseline applied: static green for side and bottom LEDs");
    } else {
        ESP_LOGW(TAG, "Startup LED baseline apply failed: %s", esp_err_to_name(ret));
    }
}

#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
static void mcu_link_runtime_task(void *arg) {
    (void)arg;

    while (true) {
        service_mcu_link_runtime();
        vTaskDelay(pdMS_TO_TICKS(MCU_LINK_RUNTIME_TASK_PERIOD_MS));
    }
}

static void ensure_mcu_link_runtime_task_started(void) {
    if (s_mcu_link_runtime_task != NULL) {
        return;
    }

    if (xTaskCreate(mcu_link_runtime_task, "mcu_link_rt", 4096, NULL, 6, &s_mcu_link_runtime_task) != pdPASS) {
        s_mcu_link_runtime_task = NULL;
        ESP_LOGE(TAG, "Failed to create MCU link runtime task");
    }
}
#endif

// static void run_camera_boot_diag(void) {
//     // Camera module intentionally disabled.
//     // The original boot diagnostic flow is kept here in comments for easy restoration.
//     // #if CONFIG_WATCHER_CAMERA_BOOT_DIAG
//     //     esp_err_t ret;
//     //     size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
//     //     size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
//     //
//     //     if (!has_internal_heap_headroom(CAMERA_DIAG_MIN_INTERNAL_FREE_BYTES, CAMERA_DIAG_MIN_INTERNAL_LARGEST_BYTES)) {
//     //         ESP_LOGW(TAG,
//     //                  "Skipping camera boot diagnostic due to low internal heap: free=%u largest=%u "
//     //                  "(need >=%u / >=%u)",
//     //                  (unsigned)free_internal, (unsigned)largest_internal, (unsigned)CAMERA_DIAG_MIN_INTERNAL_FREE_BYTES,
//     //                  (unsigned)CAMERA_DIAG_MIN_INTERNAL_LARGEST_BYTES);
//     //         return;
//     //     }
//     //
//     //     ESP_LOGI(TAG, "Camera boot diagnostic: begin");
//     //     ret = camera_service_init();
//     //     if (ret != ESP_OK) {
//     //         ESP_LOGW(TAG, "Camera boot diagnostic init failed: %s", esp_err_to_name(ret));
//     //         return;
//     //     }
//     //
//     //     ret = camera_service_capture_once();
//     //     if (ret != ESP_OK) {
//     //         ESP_LOGW(TAG, "Camera boot diagnostic capture failed: %s", esp_err_to_name(ret));
//     //         return;
//     //     }
//     //
//     //     ESP_LOGI(TAG, "Camera boot diagnostic: capture succeeded");
//     // #endif
// }

static void transport_set_ble_recovery_advertising_paused(bool paused, const char *reason) {
    esp_err_t err;

    if (ble_service_is_connected()) {
        s_ble_advertising_paused_for_recovery = false;
        return;
    }

    if (paused) {
        if (s_ble_advertising_paused_for_recovery) {
            return;
        }

        err = ble_service_stop_advertising();
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_ble_advertising_paused_for_recovery = true;
            ESP_LOGI(TAG, "Paused BLE advertising for low-memory recovery (%s)", reason ? reason : "no reason");
        } else if (err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Failed to pause BLE advertising for low-memory recovery: %s", esp_err_to_name(err));
        }
        return;
    }

    if (!s_ble_advertising_paused_for_recovery) {
        return;
    }

    err = ble_service_start_advertising();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_ble_advertising_paused_for_recovery = false;
        ESP_LOGI(TAG, "Resumed BLE advertising after low-memory recovery (%s)", reason ? reason : "no reason");
    } else if (err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to resume BLE advertising after low-memory recovery: %s", esp_err_to_name(err));
    }
}

static void transport_suspend_cloud_runtime_for_low_memory(const char *reason) {
    ESP_LOGW(TAG, "Suspending cloud runtime for low-memory recovery (%s)", reason ? reason : "no reason");
    LOG_HEAP_STATE("lowmem_recovery_before_reclaim");
    transport_cancel_discovery("low memory recovery");

    transport_stop_ws("low memory recovery");
    LOG_HEAP_STATE("lowmem_recovery_after_ws_stop");

    ws_client_deinit();
    s_ws_stack_ready = false;
    LOG_HEAP_STATE("lowmem_recovery_after_ws_deinit");

    voice_recorder_suspend_cloud_audio();
    s_cloud_runtime_started = false;
    LOG_HEAP_STATE("lowmem_recovery_after_voice_suspend");
}

static void transport_enter_low_memory_recovery(const char *reason) {
    if (s_low_memory_recovery_active) {
        return;
    }

    s_low_memory_recovery_active = true;
    s_wifi_recovery_started_us = esp_timer_get_time();
    ESP_LOGW(TAG, "Entering low-memory WiFi recovery after %u deferred resume attempts (%s)",
             (unsigned)s_consecutive_wifi_resume_defers, reason ? reason : "no reason");

    transport_set_ble_recovery_advertising_paused(true, reason);
    transport_suspend_cloud_runtime_for_low_memory(reason);
}

static void transport_reset_low_memory_recovery(const char *reason) {
    int64_t duration_ms = 0;
    bool had_active_recovery = s_low_memory_recovery_active;

    if (s_wifi_recovery_started_us > 0) {
        duration_ms = (esp_timer_get_time() - s_wifi_recovery_started_us) / 1000LL;
    }

    s_consecutive_wifi_resume_defers = 0;
    s_consecutive_ws_start_defers = 0;
    s_low_memory_recovery_active = false;
    s_wifi_recovery_started_us = 0;

    if (ble_service_is_connected()) {
        s_ble_advertising_paused_for_recovery = false;
    } else {
        transport_set_ble_recovery_advertising_paused(false, reason);
    }

    if (had_active_recovery) {
        ESP_LOGI(TAG, "Exiting low-memory WiFi recovery after %lld ms (%s)", duration_ms,
                 reason ? reason : "no reason");
    }
}

static bool transport_start_ws_transport(const char *ws_url, const char *start_reason) {
    if (transport_prepare_ws_client(ws_url) != 0) {
        ESP_LOGW(TAG, "WebSocket prepare failed for URL: %s", ws_url);
        transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ws prepare failed");
        return false;
    }

    if (!transport_has_ws_start_headroom()) {
        bool recovery_was_active = s_low_memory_recovery_active;

        s_consecutive_ws_start_defers += 1U;
        if (!s_low_memory_recovery_active && s_consecutive_ws_start_defers >= WS_START_LOW_MEMORY_RECOVERY_DEFERS) {
            transport_enter_low_memory_recovery("repeated ws start low internal heap");
        }

        if (s_low_memory_recovery_active != recovery_was_active) {
            if (!transport_has_ws_start_headroom()) {
                LOG_HEAP_STATE("ws_start_deferred");
                transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
                transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED,
                                    s_low_memory_recovery_active ? "low-memory recovery waiting ws heap headroom"
                                                                 : "waiting ws heap headroom");
                return false;
            }
        } else {
            LOG_HEAP_STATE("ws_start_deferred");
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, s_low_memory_recovery_active
                                                                        ? "low-memory recovery waiting ws heap headroom"
                                                                        : "waiting ws heap headroom");
            return false;
        }
    }

    transport_prepare_display_for_ws_start();
    LOG_HEAP_STATE("before_ws_start");
    if (ws_client_start() != 0) {
        transport_stop_ws("ws start failed");
        transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ws start failed");
        return false;
    }

    s_consecutive_ws_start_defers = 0;
    transport_reset_low_memory_recovery(start_reason);
    transport_schedule_retry(0);
    LOG_HEAP_STATE("after_ws_start");
    transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, start_reason);
    return true;
}

static bool transport_try_cached_ws_resume(void) {
    if (s_cached_ws_url[0] == '\0' || s_cached_ws_attempted_since_wifi_restore) {
        return false;
    }

    ESP_LOGI(TAG, "Trying cached WebSocket URL before discovery: %s", s_cached_ws_url);
    if (transport_prepare_ws_client(s_cached_ws_url) != 0) {
        ESP_LOGW(TAG, "Cached WebSocket URL is no longer usable, falling back to discovery");
        transport_clear_cached_ws_url("cached ws prepare failed");
        return false;
    }

    if (!transport_has_ws_start_headroom()) {
        bool recovery_was_active = s_low_memory_recovery_active;

        s_consecutive_ws_start_defers += 1U;
        if (!s_low_memory_recovery_active && s_consecutive_ws_start_defers >= WS_START_LOW_MEMORY_RECOVERY_DEFERS) {
            transport_enter_low_memory_recovery("repeated ws start low internal heap");
        }

        if (s_low_memory_recovery_active != recovery_was_active) {
            if (!transport_has_ws_start_headroom()) {
                LOG_HEAP_STATE("cached_ws_start_deferred");
                transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
                transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED,
                                    s_low_memory_recovery_active ? "low-memory recovery waiting cached ws heap headroom"
                                                                 : "waiting cached ws heap headroom");
                return true;
            }
        } else {
            LOG_HEAP_STATE("cached_ws_start_deferred");
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED,
                                s_low_memory_recovery_active ? "low-memory recovery waiting cached ws heap headroom"
                                                             : "waiting cached ws heap headroom");
            return true;
        }
    }

    transport_prepare_display_for_ws_start();
    LOG_HEAP_STATE("before_cached_ws_start");
    if (ws_client_start() != 0) {
        transport_stop_ws("cached ws start failed");
        transport_clear_cached_ws_url("cached ws start failed");
        return false;
    }

    s_consecutive_ws_start_defers = 0;
    transport_reset_low_memory_recovery("cached ws start requested");
    transport_schedule_retry(0);
    LOG_HEAP_STATE("after_cached_ws_start");
    transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, "cached ws start requested");
    s_cached_ws_attempted_since_wifi_restore = true;
    s_cached_ws_connect_inflight = true;
    s_cached_ws_connect_started_us = esp_timer_get_time();
    return true;
}

static void wait_for_behavior_idle(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (behavior_state_is_busy() && waited_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(STARTUP_BEHAVIOR_POLL_MS));
        waited_ms += STARTUP_BEHAVIOR_POLL_MS;
    }

    if (behavior_state_is_busy()) {
        ESP_LOGW(TAG, "Timed out waiting for startup behavior to settle after %lu ms", (unsigned long)waited_ms);
    }
}

static bool should_show_stm32_handshake_failure(void) {
    return mcu_link_bootstrap_handshake_timed_out(MCU_HANDSHAKE_UI_TIMEOUT_MS) && !mcu_link_bootstrap_is_ready();
}

static idle_hint_mode_t get_idle_hint_mode(void) {
    if (should_show_stm32_handshake_failure()) {
        return IDLE_HINT_STM32_HANDSHAKE_FAILED;
    }

    if (ble_service_is_connected()) {
        return IDLE_HINT_BLE_CONNECTED;
    }

    if (wifi_has_credentials() != 1) {
        return IDLE_HINT_WIFI_SETUP_REQUIRED;
    }

    if (s_transport_state == TRANSPORT_BLE_IDLE_CLOUD_READY) {
        return IDLE_HINT_READY;
    }

    if (wifi_is_connected() == 1) {
        return IDLE_HINT_CLOUD_CONNECTING;
    }

    if (s_wifi_failed_since_last_success) {
        return IDLE_HINT_WIFI_FAILED;
    }

    switch (s_transport_state) {
    case TRANSPORT_BLE_IDLE_WIFI_STARTING:
    case TRANSPORT_BLE_IDLE_WIFI_CONNECTING:
    case TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED:
        return IDLE_HINT_WIFI_RECOVERING;

    case TRANSPORT_BLE_IDLE_DISCOVERING:
    case TRANSPORT_BLE_IDLE_WS_CONNECTING:
        return IDLE_HINT_CLOUD_CONNECTING;

    case TRANSPORT_BLE_IDLE_NO_CREDENTIALS:
        return IDLE_HINT_WIFI_SETUP_REQUIRED;

    case TRANSPORT_BLE_ACTIVE:
        return IDLE_HINT_BLE_CONNECTED;

    case TRANSPORT_BLE_IDLE_CLOUD_READY:
        return IDLE_HINT_READY;

    default:
        return IDLE_HINT_WIFI_RECOVERING;
    }
}

static idle_hint_view_t get_idle_hint_view(idle_hint_mode_t mode) {
    switch (mode) {
    case IDLE_HINT_BLE_CONNECTED:
        return (idle_hint_view_t){.text = "BLE connected", .font_size = 0, .alert = false};

    case IDLE_HINT_WIFI_SETUP_REQUIRED:
        return (idle_hint_view_t){.text = get_wifi_setup_hint_text(), .font_size = 14, .alert = true};

    case IDLE_HINT_WIFI_RECOVERING:
        return (idle_hint_view_t){.text = "Reconnecting Wi-Fi...", .font_size = 22, .alert = false};

    case IDLE_HINT_WIFI_FAILED:
        return (idle_hint_view_t){.text = "Wi-Fi failed. Reconnect BLE.", .font_size = 20, .alert = true};

    case IDLE_HINT_CLOUD_CONNECTING:
        return (idle_hint_view_t){.text = "Connecting cloud...", .font_size = 22, .alert = false};

    case IDLE_HINT_STM32_HANDSHAKE_FAILED:
        return (idle_hint_view_t){.text = "STM32 handshake failed\nServo unavailable", .font_size = 20, .alert = true};

    case IDLE_HINT_READY:
    default:
        return (idle_hint_view_t){.text = "Ready!", .font_size = 0, .alert = false};
    }
}

static bool ready_idle_has_animation_headroom(int64_t now_us);

static bool power_monitor_charge_mode_active(void) {
    power_monitor_sample_t snapshot;

    if (power_monitor_service_get_snapshot(&snapshot) != ESP_OK) {
        return false;
    }

    return snapshot.vbus_present;
}

static bool recharge_intro_is_active(void) {
    const char *current_state = behavior_state_get_current();

    if (current_state == NULL || strcmp(current_state, "recharge") != 0) {
        return false;
    }

    return emoji_anim_is_running() || emoji_anim_is_switch_pending();
}

static bool handoff_completed_recharge_to_idle(const idle_hint_view_t *view) {
    const char *current_state = behavior_state_get_current();
    esp_err_t ret;

    if (current_state == NULL || strcmp(current_state, "recharge") != 0 || recharge_intro_is_active()) {
        return false;
    }

    ret = behavior_state_set_with_resources("standby", view != NULL ? view->text : NULL,
                                            view != NULL ? view->font_size : 0, "standby", "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to hand off completed recharge intro to standby: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Recharge intro completed; handoff to standby idle");
    return true;
}

static bool ready_idle_can_replace_happy(void) {
    const char *current_state = behavior_state_get_current();
    if (current_state == NULL || strcmp(current_state, "happy") != 0) {
        s_ready_idle_happy_observed_us = 0;
        return false;
    }

    int64_t now_us = esp_timer_get_time();
    if (s_ready_idle_happy_observed_us == 0) {
        s_ready_idle_happy_observed_us = now_us;
        if (s_ready_idle_after_happy_us <= now_us) {
            int duration_ms = ready_idle_post_happy_delay_ms();
            s_ready_idle_after_happy_us = now_us + (int64_t)duration_ms * 1000LL;
            s_ready_idle_happy_defer_logged = false;
            ESP_LOGI(TAG, "Ready idle observed happy hold; handoff in %dms", duration_ms);
        }
    }

    if (sfx_service_is_busy()) {
        if (!s_ready_idle_happy_defer_logged) {
            ESP_LOGI(TAG, "Ready idle waiting for happy SFX before standby variants");
            s_ready_idle_happy_defer_logged = true;
        }
        return false;
    }

    if (now_us < s_ready_idle_after_happy_us) {
        if (!s_ready_idle_happy_defer_logged) {
            int64_t remaining_ms = (s_ready_idle_after_happy_us - now_us + 999LL) / 1000LL;
            ESP_LOGI(TAG, "Ready idle waiting for happy animation handoff: remaining=%lldms", remaining_ms);
            s_ready_idle_happy_defer_logged = true;
        }
        return false;
    }

    return true;
}

static bool idle_hint_is_blocked(idle_hint_mode_t desired_hint) {
    if (behavior_state_is_action_active()) {
        return true;
    }

    if (!behavior_state_is_busy()) {
        return false;
    }

    if (desired_hint == IDLE_HINT_READY && ready_idle_can_replace_happy()) {
        ESP_LOGI(TAG, "Ready idle replacing completed happy hold with standby variant");
        return false;
    }

    return true;
}

static void reset_ready_idle_rotation(void) {
    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_completed_rounds = 0;
    s_ready_idle_sleeping = false;
    s_ready_idle_standby_transition_pending = false;
    s_ready_idle_standby_transition_deadline_us = 0;
    s_ready_idle_memory_defers = 0;
}

static void schedule_ready_idle_retry(int64_t now_us) {
    reset_ready_idle_rotation();
    s_ready_idle_next_switch_us = now_us + (int64_t)READY_IDLE_FALLBACK_RETRY_MS * 1000LL;
}

static bool ready_idle_retry_pending(int64_t now_us) {
    return s_ready_idle_variant_index < 0 && s_ready_idle_next_switch_us > 0 && now_us < s_ready_idle_next_switch_us;
}

static bool ready_idle_has_animation_headroom(int64_t now_us) {
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    if (largest_internal >= READY_IDLE_MIN_INTERNAL_LARGEST_BYTES && largest_dma >= READY_IDLE_MIN_DMA_LARGEST_BYTES) {
        return true;
    }

    if (s_ready_idle_last_memory_pressure_log_us == 0 ||
        (now_us - s_ready_idle_last_memory_pressure_log_us) >= READY_IDLE_MEMORY_PRESSURE_LOG_INTERVAL_US) {
        ESP_LOGW(TAG, "Ready idle deferred under memory pressure: int_largest=%u dma_largest=%u need=%u/%u",
                 (unsigned)largest_internal, (unsigned)largest_dma, (unsigned)READY_IDLE_MIN_INTERNAL_LARGEST_BYTES,
                 (unsigned)READY_IDLE_MIN_DMA_LARGEST_BYTES);
        s_ready_idle_last_memory_pressure_log_us = now_us;
    }

    return false;
}

static void schedule_ready_idle_memory_retry(int64_t now_us) {
    s_ready_idle_next_switch_us = now_us + (int64_t)READY_IDLE_MEMORY_RETRY_MS * 1000LL;
}

static void mark_ready_idle_standby_transition_pending(void) {
    const char *current_state = behavior_state_get_current();
    if (current_state != NULL && strcmp(current_state, "standby") != 0) {
        s_ready_idle_standby_transition_pending = true;
        s_ready_idle_standby_transition_deadline_us =
            esp_timer_get_time() + (int64_t)READY_IDLE_STANDBY_HANDOFF_TIMEOUT_MS * 1000LL;
        return;
    }

    s_ready_idle_standby_transition_pending = false;
    s_ready_idle_standby_transition_deadline_us = 0;
}

static bool ready_idle_waiting_for_standby_transition(const char *current_state, int64_t now_us) {
    if (!s_ready_idle_standby_transition_pending) {
        return false;
    }

    if (current_state != NULL && strcmp(current_state, "standby") == 0) {
        s_ready_idle_standby_transition_pending = false;
        s_ready_idle_standby_transition_deadline_us = 0;
        return false;
    }

    if (current_state != NULL && strcmp(current_state, "happy") != 0) {
        ESP_LOGI(TAG, "Ready idle cycle reset by active state=%s while standby handoff was pending", current_state);
        reset_ready_idle_rotation();
        return false;
    }

    if (s_ready_idle_standby_transition_deadline_us == 0 || now_us < s_ready_idle_standby_transition_deadline_us) {
        return true;
    }

    ESP_LOGW(TAG, "Ready idle standby handoff timed out while state=%s; retrying later",
             current_state != NULL ? current_state : "<unknown>");
    schedule_ready_idle_retry(now_us);
    return true;
}

static const char *ready_idle_variant_name(int index) {
    static const char *names[READY_IDLE_VARIANT_COUNT] = {
        "standby1",
        "standby2",
        "standby3",
        "standby4",
    };

    if (index < 0 || index >= READY_IDLE_VARIANT_COUNT) {
        return "standby";
    }
    return names[index];
}

static emoji_anim_type_t ready_idle_variant_type(int index) {
    static const emoji_anim_type_t types[READY_IDLE_VARIANT_COUNT] = {
        EMOJI_ANIM_STANDBY_1,
        EMOJI_ANIM_STANDBY_2,
        EMOJI_ANIM_STANDBY_3,
        EMOJI_ANIM_STANDBY_4,
    };

    if (index < 0 || index >= READY_IDLE_VARIANT_COUNT) {
        return EMOJI_ANIM_STANDBY;
    }
    return types[index];
}

static int collect_ready_idle_variants(int available[READY_IDLE_VARIANT_COUNT]) {
    int available_count = 0;

    for (int index = 0; index < READY_IDLE_VARIANT_COUNT; ++index) {
        emoji_anim_type_t type = ready_idle_variant_type(index);
        if (anim_catalog_has_type(type)) {
            available[available_count++] = index;
            continue;
        }
        if (!s_ready_idle_variant_unavailable_logged[index]) {
            ESP_LOGW(TAG, "Ready idle variant unavailable in SD manifest: %s", ready_idle_variant_name(index));
            s_ready_idle_variant_unavailable_logged[index] = true;
        }
    }

    if (available_count == 0) {
        if (!s_ready_idle_all_unavailable_logged) {
            ESP_LOGW(TAG, "No ready idle variants available; falling back to standby");
            s_ready_idle_all_unavailable_logged = true;
        }
        return -1;
    }

    s_ready_idle_all_unavailable_logged = false;
    return available_count;
}

static int choose_ready_idle_variant(bool *completed_round) {
    int available[READY_IDLE_VARIANT_COUNT] = {0};
    int available_count = collect_ready_idle_variants(available);
    if (completed_round != NULL) {
        *completed_round = false;
    }
    if (available_count <= 0) {
        return -1;
    }

    int current_pos = -1;
    for (int pos = 0; pos < available_count; ++pos) {
        if (available[pos] == s_ready_idle_variant_index) {
            current_pos = pos;
            break;
        }
    }

    int next_pos = current_pos + 1;
    if (next_pos >= available_count) {
        next_pos = 0;
        if (current_pos >= 0 && completed_round != NULL) {
            *completed_round = true;
        }
    }
    return available[next_pos];
}

static int ready_idle_variant_duration_ms(int index) {
    int duration_ms = emoji_get_loop_duration_ms(ready_idle_variant_type(index));
    if (duration_ms < READY_IDLE_MIN_VARIANT_DURATION_MS) {
        duration_ms = READY_IDLE_MIN_VARIANT_DURATION_MS;
    }
    return duration_ms;
}

static bool apply_ready_idle_sleep(const idle_hint_view_t *view) {
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, "standby", "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle sleep standby: %s", esp_err_to_name(ret));
        s_ready_idle_next_switch_us = esp_timer_get_time() + (int64_t)READY_IDLE_FALLBACK_RETRY_MS * 1000LL;
        return false;
    }

    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_sleeping = true;
    s_ready_idle_after_happy_us = 0;
    s_ready_idle_happy_observed_us = 0;
    s_ready_idle_happy_defer_logged = false;
    mark_ready_idle_standby_transition_pending();
    ESP_LOGI(TAG, "Ready idle sleep standby applied after %lu completed standby variant rounds",
             (unsigned long)s_ready_idle_completed_rounds);
    return true;
}

static bool apply_ready_idle_fallback_standby(const idle_hint_view_t *view, int64_t now_us) {
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, "standby", "");
    schedule_ready_idle_retry(now_us);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle fallback standby: %s", esp_err_to_name(ret));
        return false;
    }

    mark_ready_idle_standby_transition_pending();
    return true;
}

static bool apply_ready_idle_text_only_standby(const idle_hint_view_t *view, int64_t now_us) {
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, "", "");
    schedule_ready_idle_retry(now_us);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply text-only ready idle standby: %s", esp_err_to_name(ret));
        return false;
    }

    mark_ready_idle_standby_transition_pending();
    return true;
}

static bool apply_ready_idle_variant_if_due(const idle_hint_view_t *view, bool force) {
    int64_t now_us = esp_timer_get_time();

    if (s_ready_idle_sleeping) {
        return true;
    }

    if (ready_idle_retry_pending(now_us)) {
        return true;
    }

    if (!force && s_ready_idle_variant_index >= 0 && now_us < s_ready_idle_next_switch_us) {
        return true;
    }

    if (!ready_idle_has_animation_headroom(now_us)) {
        s_ready_idle_memory_defers++;
        if (s_ready_idle_memory_defers >= READY_IDLE_MEMORY_FORCE_STANDBY_DEFERS) {
            ESP_LOGW(TAG, "Ready idle memory pressure persisted for %lu retries; forcing text-only standby handoff",
                     (unsigned long)s_ready_idle_memory_defers);
            s_ready_idle_memory_defers = 0;
            return apply_ready_idle_text_only_standby(view, now_us);
        }
        schedule_ready_idle_memory_retry(now_us);
        return true;
    }
    s_ready_idle_memory_defers = 0;

    bool completed_round = false;
    int selected = choose_ready_idle_variant(&completed_round);
    if (selected < 0) {
        return apply_ready_idle_fallback_standby(view, now_us);
    }

    if (completed_round) {
        s_ready_idle_completed_rounds++;
        if (s_ready_idle_completed_rounds >= READY_IDLE_ROUNDS_BEFORE_SLEEP) {
            return apply_ready_idle_sleep(view);
        }
    }

    const char *anim_id = ready_idle_variant_name(selected);
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, anim_id, "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle variant %s: %s", anim_id, esp_err_to_name(ret));
        schedule_ready_idle_retry(now_us);
        return false;
    }

    int duration_ms = ready_idle_variant_duration_ms(selected);
    s_ready_idle_variant_index = selected;
    s_ready_idle_next_switch_us = now_us + (int64_t)duration_ms * 1000LL;
    s_ready_idle_after_happy_us = 0;
    s_ready_idle_happy_observed_us = 0;
    s_ready_idle_happy_defer_logged = false;
    mark_ready_idle_standby_transition_pending();
    ESP_LOGI(TAG, "Ready idle variant applied: %s duration=%dms round=%lu/%d", anim_id, duration_ms,
             (unsigned long)(s_ready_idle_completed_rounds + 1U), READY_IDLE_ROUNDS_BEFORE_SLEEP);
    return true;
}

static void apply_idle_hint_if_needed(void) {
    static idle_hint_mode_t s_last_applied_hint = IDLE_HINT_READY;
    static bool s_hint_initialized = false;
    idle_hint_mode_t desired_hint = get_idle_hint_mode();
    idle_hint_view_t view = get_idle_hint_view(desired_hint);

    if (voice_recorder_get_state() != VOICE_STATE_IDLE) {
        reset_ready_idle_rotation();
        return;
    }

    if (power_monitor_charge_mode_active() && recharge_intro_is_active()) {
        reset_ready_idle_rotation();
        return;
    }

    if (power_monitor_charge_mode_active() && handoff_completed_recharge_to_idle(&view)) {
        reset_ready_idle_rotation();
        return;
    }

    if (desired_hint != IDLE_HINT_READY) {
        reset_ready_idle_rotation();
    } else {
        int64_t now_us = esp_timer_get_time();
        const char *current_state = behavior_state_get_current();

        if (ready_idle_waiting_for_standby_transition(current_state, now_us)) {
            return;
        }

        bool cycle_active =
            s_ready_idle_variant_index >= 0 || s_ready_idle_completed_rounds > 0 || s_ready_idle_sleeping;
        if (cycle_active && current_state != NULL && strcmp(current_state, "standby") != 0) {
            ESP_LOGI(TAG, "Ready idle cycle reset by active state=%s", current_state);
            reset_ready_idle_rotation();
        }

        if (ready_idle_retry_pending(now_us)) {
            return;
        }
    }

    if (idle_hint_is_blocked(desired_hint)) {
        return;
    }

    if (desired_hint == IDLE_HINT_READY) {
        bool force = !s_hint_initialized || desired_hint != s_last_applied_hint || s_ready_idle_variant_index < 0;
        if (apply_ready_idle_variant_if_due(&view, force)) {
            s_last_applied_hint = desired_hint;
            s_hint_initialized = true;
            return;
        }
    } else {
        reset_ready_idle_rotation();
    }

    if (s_hint_initialized && desired_hint == s_last_applied_hint) {
        return;
    }

    (void)behavior_state_set_with_text_style("standby", view.text, view.font_size, view.alert);

    s_last_applied_hint = desired_hint;
    s_hint_initialized = true;
}

static bool should_defer_ble_connected_feedback_for_state(const char *state_id) {
    if (state_id == NULL || state_id[0] == '\0') {
        return false;
    }

    return strcmp(state_id, "listening") == 0 || strcmp(state_id, "thinking") == 0 ||
           strcmp(state_id, "processing") == 0 || strcmp(state_id, "speaking") == 0;
}

static void maybe_play_ble_connected_feedback(void) {
    const char *current_state = NULL;
    esp_err_t ret;

    if (!s_ui_ready || !s_ble_connected_feedback_pending) {
        return;
    }

    if (!ble_service_is_connected()) {
        s_ble_connected_feedback_pending = false;
        return;
    }

    if (behavior_state_is_action_active()) {
        return;
    }

    current_state = behavior_state_get_current();
    if (current_state != NULL && strcmp(current_state, BLE_CONNECTED_FEEDBACK_STATE_ID) == 0) {
        s_ble_connected_feedback_pending = false;
        return;
    }

    if (should_defer_ble_connected_feedback_for_state(current_state)) {
        return;
    }

    ret = behavior_state_set_with_resources_and_action(BLE_CONNECTED_FEEDBACK_STATE_ID,
                                                       "BLE connected",
                                                       0,
                                                       BLE_CONNECTED_FEEDBACK_ANIM_ID,
                                                       BLE_CONNECTED_FEEDBACK_SOUND_ID,
                                                       BLE_CONNECTED_FEEDBACK_ACTION_ID);
    if (ret == ESP_OK) {
        s_ble_connected_feedback_pending = false;
        ESP_LOGI(TAG, "Triggered BLE connected feedback state");
    } else {
        ESP_LOGW(TAG, "Failed to trigger BLE connected feedback state: %s", esp_err_to_name(ret));
    }
}

static void ensure_cloud_runtime_started(void) {
    if (!s_boot_completed || s_cloud_runtime_started) {
        return;
    }

    if (!transport_has_cloud_runtime_headroom()) {
        return;
    }

    if (voice_recorder_start() != 0) {
        ESP_LOGE(TAG, "Failed to start voice recorder (non-fatal)");
        return;
    }

    s_cloud_runtime_started = true;
}

static void transport_stop_ws(const char *reason) {
    if (ws_client_is_started() || ws_client_is_connected() || ws_client_is_session_ready()) {
        ESP_LOGI(TAG, "Stopping WebSocket transport (%s)", reason ? reason : "no reason");
        ws_client_stop();
    }
}

static void transport_discovery_task(void *arg) {
    discovery_result_t result = {0};
    uint32_t *generation = (uint32_t *)arg;

    result.generation = generation ? *generation : 0;
    result.status = discovery_start_with_timeout(&result.info, CLOUD_DISCOVERY_TIMEOUT_MS);

    if (s_discovery_result_queue != NULL) {
        (void)xQueueOverwrite(s_discovery_result_queue, &result);
    }

    s_discovery_inflight = false;
    free(generation);
    vTaskDelete(NULL);
}

static int transport_launch_discovery(void) {
    BaseType_t task_ret;
    uint32_t *generation;

    if (s_discovery_inflight) {
        return 0;
    }

    if (s_discovery_result_queue == NULL) {
        s_discovery_result_queue = xQueueCreate(1, sizeof(discovery_result_t));
        if (s_discovery_result_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create discovery result queue");
            return -1;
        }
    }

    generation = (uint32_t *)malloc(sizeof(uint32_t));
    if (generation == NULL) {
        ESP_LOGE(TAG, "Failed to allocate discovery generation");
        return -1;
    }

    s_discovery_generation += 1U;
    *generation = s_discovery_generation;
    s_discovery_inflight = true;

    task_ret = xTaskCreate(transport_discovery_task,
                           "cloud_discovery",
                           CLOUD_DISCOVERY_TASK_STACK_BYTES,
                           generation,
                           5,
                           NULL);
    if (task_ret != pdPASS) {
        s_discovery_inflight = false;
        free(generation);
        ESP_LOGE(TAG, "Failed to create discovery task");
        return -1;
    }

    ESP_LOGI(TAG, "Started discovery generation %lu", (unsigned long)*generation);
    transport_set_state(TRANSPORT_BLE_IDLE_DISCOVERING, "discovery task launched");
    return 0;
}

static void transport_cancel_discovery(const char *reason) {
    if (!s_discovery_inflight) {
        return;
    }

    s_discovery_generation += 1U;
    ESP_LOGI(TAG, "Discovery invalidated (%s)", reason ? reason : "no reason");
}

static int transport_prepare_ws_client(const char *ws_url) {
    if (ws_url == NULL || ws_url[0] == '\0') {
        return -1;
    }

    if (!s_ws_router_ready) {
        ws_router_t router;

        ws_handlers_init();
        router = ws_handlers_get_router();
        ws_router_init(&router);
        s_ws_router_ready = true;
    }

    if (!s_ws_stack_ready || strcmp(ws_client_get_server_url(), ws_url) != 0) {
        if (s_ws_stack_ready) {
            transport_stop_ws("ws url refresh");
            ws_client_deinit();
            s_ws_stack_ready = false;
        }

        if (ws_client_set_server_url(ws_url) != 0) {
            ESP_LOGE(TAG, "Failed to set WebSocket URL: %s", ws_url);
            return -1;
        }

        if (ws_client_init() != 0) {
            ESP_LOGE(TAG, "Failed to initialize WebSocket client");
            return -1;
        }

        s_ws_stack_ready = true;
    }

    return 0;
}

static void transport_begin_wifi_resume(const char *reason) {
    if (wifi_has_credentials() != 1) {
        s_waiting_for_wifi_provision = true;
        transport_reset_low_memory_recovery("missing credentials");
        transport_stop_ws("missing credentials");
        transport_set_state(TRANSPORT_BLE_IDLE_NO_CREDENTIALS, reason);
        return;
    }

    s_waiting_for_wifi_provision = false;

    if (wifi_is_connected() == 1) {
        transport_reset_low_memory_recovery("wifi already connected");
        return;
    }

    if (wifi_is_connect_requested() == 1) {
        transport_set_state(
            wifi_sta_is_started() == 1 ? TRANSPORT_BLE_IDLE_WIFI_CONNECTING : TRANSPORT_BLE_IDLE_WIFI_STARTING, reason);
        return;
    }

    if (!transport_has_wifi_resume_headroom()) {
        bool recovery_was_active = s_low_memory_recovery_active;

        s_consecutive_wifi_resume_defers += 1U;
        if (!s_low_memory_recovery_active &&
            s_consecutive_wifi_resume_defers >= WIFI_RESUME_LOW_MEMORY_RECOVERY_DEFERS) {
            transport_enter_low_memory_recovery("repeated low internal heap");
        }

        if (s_low_memory_recovery_active != recovery_was_active) {
            if (!transport_has_wifi_resume_headroom()) {
                transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
                transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED,
                                    s_low_memory_recovery_active ? "low-memory recovery waiting heap headroom"
                                                                 : "waiting heap headroom");
                return;
            }
        } else {
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, s_low_memory_recovery_active
                                                                        ? "low-memory recovery waiting heap headroom"
                                                                        : "waiting heap headroom");
            return;
        }
    }

    s_consecutive_wifi_resume_defers = 0;

    if (wifi_resume_background() == 0) {
        transport_set_state(
            wifi_sta_is_started() == 1 ? TRANSPORT_BLE_IDLE_WIFI_CONNECTING : TRANSPORT_BLE_IDLE_WIFI_STARTING, reason);
        return;
    }

    transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
    transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "wifi resume failed");
}

static void transport_handle_discovery_results(bool ble_connected) {
    discovery_result_t result;

    if (s_discovery_result_queue == NULL) {
        return;
    }

    while (xQueueReceive(s_discovery_result_queue, &result, 0) == pdTRUE) {
        char *ws_url = NULL;

        if (result.generation != s_discovery_generation) {
            ESP_LOGI(TAG, "Ignoring stale discovery result generation %lu (current %lu)",
                     (unsigned long)result.generation, (unsigned long)s_discovery_generation);
            continue;
        }

        if (ble_connected) {
            ESP_LOGI(TAG, "Ignoring discovery result because BLE is active");
            continue;
        }

        if (result.status != 0) {
            ESP_LOGW(TAG, "Discovery failed, retrying after %u ms", CLOUD_RETRY_DELAY_MS);
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "discovery failed");
            continue;
        }

        if (result.info.protocol_version[0] == '\0' ||
            strcmp(result.info.protocol_version, WATCHER_PROTOCOL_VERSION) != 0) {
            ESP_LOGE(TAG, "Protocol mismatch: server=%s expected=%s",
                     result.info.protocol_version[0] != '\0' ? result.info.protocol_version : "<missing>",
                     WATCHER_PROTOCOL_VERSION);
            transport_schedule_retry(CLOUD_PROTOCOL_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "protocol mismatch");
            continue;
        }

        ESP_LOGI(TAG, "Discovery ready: %s:%u protocol=%s", result.info.ip, result.info.port,
                 result.info.protocol_version);

        ws_url = discovery_get_ws_url(&result.info);
        if (ws_url == NULL) {
            ESP_LOGE(TAG, "Failed to build WebSocket URL from discovery result");
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ws url build failed");
            continue;
        }

        if (!transport_start_ws_transport(ws_url, "ws start requested")) {
            free(ws_url);
            continue;
        }

        transport_cache_ws_url(ws_url, "discovery ready");
        s_cached_ws_attempted_since_wifi_restore = true;
        s_cached_ws_connect_inflight = false;
        s_cached_ws_connect_started_us = 0;
        free(ws_url);
    }
}

static void transport_suspend_for_ble(void) {
    ESP_LOGI(TAG, "BLE connected, pausing WiFi/WS background activity");
    s_wifi_failed_since_last_success = false;
    transport_reset_cached_ws_resume_state();
    transport_reset_low_memory_recovery("ble connected");
    transport_cancel_discovery("ble connected");
    transport_stop_ws("ble connected");
    wifi_suspend_for_ble();
    transport_schedule_retry(0);
    transport_set_state(TRANSPORT_BLE_ACTIVE, "ble connected");
}

static void on_ble_connection_changed(bool connected) {
    s_last_ble_connected = connected;
    s_ble_connected_feedback_pending = connected;

    if (connected) {
        transport_suspend_for_ble();
        return;
    }

    ESP_LOGI(TAG, "BLE disconnected, allowing WiFi/WS recovery");
    transport_reset_cached_ws_resume_state();
    transport_schedule_retry(0);
    transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ble disconnected");
}

static void transport_coordinator_tick(void) {
    bool ble_connected = ble_service_is_connected();

    transport_handle_discovery_results(ble_connected);

    if (ble_connected != s_last_ble_connected) {
        s_last_ble_connected = ble_connected;

        if (ble_connected) {
            transport_suspend_for_ble();
            return;
        }

        ESP_LOGI(TAG, "BLE disconnected, allowing WiFi/WS recovery");
        transport_reset_cached_ws_resume_state();
        transport_schedule_retry(0);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ble disconnected");
    }

    if (ble_connected) {
        transport_set_state(TRANSPORT_BLE_ACTIVE, "ble still connected");
        return;
    }

    if (wifi_has_credentials() != 1) {
        s_waiting_for_wifi_provision = true;
        transport_reset_cached_ws_resume_state();
        transport_clear_cached_ws_url("credentials cleared");
        transport_reset_low_memory_recovery("credentials cleared");
        transport_stop_ws("credentials cleared");
        transport_set_state(TRANSPORT_BLE_IDLE_NO_CREDENTIALS, "no credentials");
        return;
    }

    if (wifi_is_connected() != 1) {
        transport_reset_cached_ws_resume_state();
        transport_stop_ws("wifi not connected");

        if (transport_retry_due()) {
            transport_begin_wifi_resume("wifi recovery");
        } else {
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "waiting wifi retry");
        }
        return;
    }

    s_waiting_for_wifi_provision = false;
    if (!s_low_memory_recovery_active) {
        transport_reset_low_memory_recovery("wifi restored");
    }

    if (!s_discovery_initialized) {
        if (discovery_init() == 0) {
            s_discovery_initialized = true;
        } else {
            ESP_LOGW(TAG, "Discovery init failed, retrying later");
            transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "discovery init failed");
            return;
        }
    }

    if (ws_client_is_session_ready()) {
        transport_cache_ws_url(ws_client_get_server_url(), "ws session ready");
        s_cached_ws_attempted_since_wifi_restore = false;
        s_cached_ws_connect_inflight = false;
        s_cached_ws_connect_started_us = 0;
        ensure_cloud_runtime_started();
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_READY, "ws session ready");
        return;
    }

    if (s_cached_ws_connect_inflight) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_cached_ws_connect_started_us) / 1000LL;

        if (!(ws_client_is_connected() || ws_client_is_started())) {
            s_cached_ws_connect_inflight = false;
            s_cached_ws_connect_started_us = 0;
            ESP_LOGW(TAG, "Cached WebSocket resume failed, falling back to discovery");
            transport_schedule_retry(0);
        } else if (elapsed_ms >= (int64_t)CACHED_WS_CONNECT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Cached WebSocket resume timed out after %lld ms, falling back to discovery", elapsed_ms);
            transport_stop_ws("cached ws connect timeout");
            s_cached_ws_connect_inflight = false;
            s_cached_ws_connect_started_us = 0;
            transport_schedule_retry(0);
        } else {
            transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, "cached ws connecting");
            return;
        }
    }

    if (ws_client_is_connected() || ws_client_is_started()) {
        transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, "ws connecting");
        return;
    }

    if (s_discovery_inflight) {
        transport_set_state(TRANSPORT_BLE_IDLE_DISCOVERING, "discovery in flight");
        return;
    }

    if (!transport_retry_due()) {
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "waiting cloud retry");
        return;
    }

    if (transport_try_cached_ws_resume()) {
        return;
    }

    if (transport_launch_discovery() != 0) {
        transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "discovery launch failed");
    }
}

void app_main(void) {
    int boot_frame_count = 0;

    configure_runtime_log_levels();
    mem_monitor_init();
    log_startup_banner();
    log_firmware_version();
    log_ble_mac_at_boot("startup");
    LOG_HEAP_STATE("app_start");

    /* 1. Minimal display init for boot animation */
    if (hal_display_minimal_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    LOG_HEAP_STATE("after_minimal_display");

    /* 2. Show boot animation */
    boot_anim_init();
    boot_anim_set_text("Initializing...");
    boot_anim_set_detail_text("");
    boot_anim_set_progress(0);
    log_ble_mac_at_boot("boot_screen");

    /* 3. Mount SD and validate animation assets before proceeding. */
    boot_frame_count = boot_prepare_animation_assets();
    LOG_HEAP_STATE("after_anim_assets");
    boot_anim_set_text("Boot...");
    boot_anim_start_intro(EMOJI_ANIM_BOOT, boot_frame_count, BOOT_ANIM_INTERVAL_MS);
    boot_anim_set_text("Preparing...");

    /* 4. Coprocessor link bootstrap (GPIO 19/20 runtime UART) */
    boot_anim_set_progress(25);
    boot_anim_set_text("MCU Link...");
    init_mcu_link_bootstrap();
    init_mcu_runtime_services();

    /* 4.5 Servo compatibility facade (no local PWM backend). */
    boot_anim_set_text("Servo...");
    if (hal_servo_init() != ESP_OK) {
        ESP_LOGE(TAG, "Servo facade init failed");
        boot_halt_with_error("Servo init failed");
    }

    /* 5. Initialize app state only. Input devices stay disabled during BLE provisioning. */
    boot_anim_set_progress(30);
    boot_anim_set_text("State...");
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    if (control_ingress_init() != ESP_OK) {
        ESP_LOGE(TAG, "Control ingress init failed");
        boot_halt_with_error("Control init failed");
    }
    stress_mode_start();
    if (mcu_link_bootstrap_is_ready()) {
        stress_mode_notify_ready();
    }
    LOG_HEAP_STATE("after_control_ingress");
#endif
    behavior_state_init();
    if (power_monitor_service_init() != ESP_OK) {
        ESP_LOGE(TAG, "Power monitor init failed");
        boot_halt_with_error("Power monitor init failed");
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    if (control_ingress_init() != ESP_OK) {
        ESP_LOGE(TAG, "Control ingress init failed");
        boot_halt_with_error("Control init failed");
    }
    LOG_HEAP_STATE("after_control_ingress");
#endif

    if (debug_cli_start() != ESP_OK) {
        ESP_LOGW(TAG, "Debug CLI start failed");
    }

    /* 5.5 BLE control + provisioning */
    boot_anim_set_progress(35);
    boot_anim_set_text("BLE...");
    {
        esp_err_t ble_ret = ble_service_init();
        if (ble_ret == ESP_OK) {
            log_ble_mac_at_boot("after_ble_init");
            ble_service_register_connection_callback(on_ble_connection_changed);
            ble_ret = ble_service_start_advertising();
            if (ble_ret != ESP_OK) {
                ESP_LOGW(TAG, "BLE advertising start failed: %s", esp_err_to_name(ble_ret));
            }
        } else if (ble_ret != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "BLE init failed: %s", esp_err_to_name(ble_ret));
        }
    }
    LOG_HEAP_STATE("after_ble_init");

    /* 6. WiFi manager only; cloud link is coordinated after boot. */
    boot_anim_set_progress(40);
    boot_anim_set_text("WiFi...");
    wifi_init();
    wifi_register_status_callback(on_wifi_status_changed);
    if (wifi_has_credentials() == 1) {
        s_waiting_for_wifi_provision = false;
        boot_anim_set_text("Preparing...");
        ESP_LOGI(TAG, "Stored WiFi credentials detected; deferring WiFi resume until startup settles");
    } else {
        s_waiting_for_wifi_provision = true;
        boot_anim_set_text("Reconnect BLE to set Wi-Fi");
        ESP_LOGI(TAG, "BLE local control is ready without immediate cloud bring-up");
    }
    boot_anim_set_progress(55);
    LOG_HEAP_STATE("after_wifi_ready");

    /* 7. Ready for UI; cloud transport is resumed by the coordinator loop. */
    boot_anim_set_progress(100);
    boot_anim_set_text("BLE Ready");
    vTaskDelay(pdMS_TO_TICKS(500));
    boot_anim_finish();

    LOG_HEAP_STATE("before_ui_init");
    hal_display_ui_init();
    s_ui_ready = true;
    init_runtime_inputs_and_restart_path();
    voice_recorder_init();
    if (voice_recorder_start() != 0) {
        ESP_LOGE(TAG, "Failed to start voice recorder button runtime (non-fatal)");
    }
    behavior_state_set("boot");
    wait_for_behavior_idle(STARTUP_BEHAVIOR_TIMEOUT_MS);
    behavior_state_set_text_style("BLE Ready", 0, false);
    maybe_play_ble_connected_feedback();
    apply_idle_hint_if_needed();
    LOG_HEAP_STATE("after_ui_init");

    // run_camera_boot_diag();
    LOG_HEAP_STATE("after_camera_diag");
    s_boot_completed = true;
    transport_sync_boot_state();
    ESP_LOGI(TAG, "WatcheRobot ready (transport=%s, ble=%s)", transport_state_to_string(s_transport_state),
             ble_service_is_connected() ? "connected" : "advertising");
    LOG_HEAP_STATE("ready");

    /* 10. Mark OTA partition valid (prevent rollback after successful boot) */
    ota_service_mark_valid();

    /* Main loop */
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
        service_mcu_link_runtime();
#endif
        maybe_apply_startup_green_leds();
        transport_coordinator_tick();
        (void)power_monitor_service_tick();
        maybe_play_ble_connected_feedback();
        apply_idle_hint_if_needed();
        ws_tts_timeout_check();
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
        stress_mode_tick();
#endif
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}
