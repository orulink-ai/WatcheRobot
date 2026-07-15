#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "agent_animation_flow_core.h"
#include "agent_audio_player.h"
#include "agent_runtime.h"
#include "anim_storage.h"
#include "animation_registry.h"
#include "animation_service.h"
#include "app_center.h"
#include "app_ui_mode_core.h"
#include "behavior_state_service.h"
#include "ble_service.h"
#include "boot_anim.h"
#include "bsp_watcher.h"
#include "button_shutdown_feedback.h"
#include "button_shutdown_hold_detector.h"
#include "button_shutdown_low_power.h"
#include "button_shutdown_power_channel.h"
#include "button_shutdown_sequence.h"
#include "button_shutdown_soft_off.h"
#include "client_app.h"
// #include "camera_service.h"
#include "control_ingress.h"
#include "debug_cli.h"
#include "debug_touch_guard.h"
#include "discovery_client.h"
#include "display_ui.h"
#include "esp_lvgl_port.h"
#include "factory_home_ui/ui.h"
#include "factory_settings_ui.h"
#include "hal_audio.h"
#include "hal_display.h"
#include "hal_servo.h"
#include "launcher_factory_home.h"
#include "launcher_home_model.h"
#include "launcher_screen_cache_policy.h"
#include "mcu_led_service.h"
#include "mcu_link_bootstrap.h"
#include "mcu_motion_service.h"
#include "mcu_power_service.h"
#include "mcu_sensor_service.h"
#include "mem_monitor.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota_service.h"
#include "power_monitor_service.h"
#include "provision_manager.h"
#include "realtime_client.h"
#include "sdk_control_app.h"
#include "sdk_control_ui.h"
#include "sensecap-watcher.h"
#include "sfx_service.h"
#include "stress_mode.h"
#include "time_sync_service.h"
#include "voice_service.h"
#include "watcher_app_runtime.h"
#include "watcher_build_info.h"
#include "wifi_manager.h"
#include "ws_client.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TAG "MAIN"
#define MCU_OBS_TAG "MCU_OBS"
#define UI_DIAG_TAG "UI_DIAG"

/* Physical restart: click count to trigger reboot */
#define RESTART_CLICK_COUNT 4
#define BUTTON_SHUTDOWN_HOLD_MS 3000
#define STM32_POWER_OFF_SETTLE_MS 300
#define ESP32_SHUTDOWN_FALLBACK_SLEEP_MS 800
#define ESP32_SHUTDOWN_WAKE_AFTER_SEC 0
#define ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS 3000
#define STM32_POWER_CHANNEL_READY_WAIT_MS 1200
#define STM32_POWER_CHANNEL_POLL_MS 50
#define SHUTDOWN_FEEDBACK_SOUND_ID "error"
#define SHUTDOWN_FEEDBACK_SOUND_WAIT_MS 1800
#define SHUTDOWN_FEEDBACK_POLL_MS 50
#define SOFT_OFF_NVS_NAMESPACE "power"
#define SOFT_OFF_NVS_KEY_LATCHED "soft_off"
#define SOFT_OFF_BOOT_PRESS_CONFIRM_MS 900
#define SOFT_OFF_BOOT_PRESS_POLL_MS 50
#define DEBUG_APP_COMMAND_QUEUE_DEPTH 4
#define DEBUG_APP_ID_MAX 32
#define BLE_CONNECTION_EVENT_QUEUE_DEPTH 1
#define BLE_PROVISIONING_RELEASE_DELAY_MS 300
#define BOOT_WIFI_CONNECT_TIMEOUT_MS 10000U
#define BOOT_TIME_SYNC_TIMEOUT_MS 5000U
#define BOOT_NETWORK_READY_POLL_MS 100U
#define BOOT_SDCARD_MOUNT_RESTART_MAGIC 0x53445254U
#define BOOT_SDCARD_MOUNT_RESTART_DELAY_MS 1000U
#define ESP32_SHUTDOWN_WAKE_IDLE_POLL_MS 50
#define GLOBAL_POWER_BUTTON_ACTIVE_LEVEL 0
#define POWER_BUTTON_FALLBACK_POLL_MS 50U
#define POWER_BUTTON_FALLBACK_DEBOUNCE_MS 150U
#define POWER_BUTTON_FALLBACK_REARM_RELEASE_MS 500U
#define POWER_BUTTON_FALLBACK_TASK_STACK_BYTES 3072U
#define VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS 450
#define STARTUP_BEHAVIOR_POLL_MS 50
#define STARTUP_BEHAVIOR_TIMEOUT_MS 10000
#define SETTINGS_DEFAULT_BRIGHTNESS_PERCENT 100
#define SETTINGS_HEAD_RGB_ON_R 0
#define SETTINGS_HEAD_RGB_ON_G 64
#define SETTINGS_HEAD_RGB_ON_B 0
#define LAUNCHER_HOME_SELECTED_INDEX 0
#define LAUNCHER_STATUS_REFRESH_MS 1000
#define LAUNCHER_TIME_RETRY_MS 5000
#define STARTUP_LED_RETRY_MS 1000
#define READY_IDLE_VARIANT_COUNT 4
#define READY_IDLE_STANDBY_TIMEOUT_MS 6000
#define READY_IDLE_FALLBACK_RETRY_MS 10000
#define READY_IDLE_MEMORY_RETRY_MS 3000
#define READY_IDLE_STANDBY_HANDOFF_TIMEOUT_MS 1000
#define READY_IDLE_MIN_INTERNAL_LARGEST_BYTES (12U * 1024U)
#define READY_IDLE_MIN_DMA_LARGEST_BYTES (12U * 1024U)
#define READY_IDLE_MEMORY_PRESSURE_LOG_INTERVAL_US 10000000LL
#define READY_IDLE_MEMORY_FORCE_STANDBY_DEFERS 3U
#define CLOUD_DISCOVERY_TIMEOUT_MS 5000
#define CLOUD_DISCOVERY_CANCEL_WAIT_MS 1500
#define CLOUD_DISCOVERY_TASK_STACK_BYTES 8192
#define TASK_STACK_HWM_WARN_BYTES 1024U
#define CLOUD_RETRY_DELAY_MS 2000
#define CLOUD_DISCOVERY_RETRY_INITIAL_DELAY_MS 5000
#define CLOUD_DISCOVERY_RETRY_STEP_MS 5000
#define CLOUD_DISCOVERY_RETRY_MAX_DELAY_MS 30000
#define CLOUD_PROTOCOL_RETRY_DELAY_MS 5000
#define CACHED_WS_CONNECT_TIMEOUT_MS 5000U
#define WS_SESSION_READY_TIMEOUT_MS 12000U
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
#define VOICE_RUNTIME_START_DEFER_MS 250U
#define VOICE_CONNECT_UI_WS_TIMEOUT_MS 15000U
#define LAUNCHER_RETURN_TOUCH_SETTLE_MS 700U
#define MCU_HANDSHAKE_UI_TIMEOUT_MS 5000U
#define BLE_CONNECTED_FEEDBACK_STATE_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_ANIM_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_SOUND_ID "bluetooth"
#define BLE_CONNECTED_FEEDBACK_ACTION_ID "bluetooth"
#define PROVISIONING_FEEDBACK_SOUND_ID_BLE_CONNECTED "bluetooth"
#define PROVISIONING_FEEDBACK_SOUND_ID_WIFI_SAVED "get"
#define PROVISIONING_FEEDBACK_SOUND_ID_WIFI_CONNECTED "happy"
#define PROVISIONING_FEEDBACK_SOUND_ID_WIFI_FAILED "error"
#define PROVISIONING_FEEDBACK_SOUND_ID_BLE_DISCONNECTED "disconnect"
#define TOUCH_FONDLE_STATE_ID "fondle_love"
#define TOUCH_FONDLE_ANIM_ID "fondle_love"
#define TOUCH_FONDLE_ACTION_ID "fondle_love"
#define TOUCH_FONDLE_INVALID_ID 0xFFu
#define LOCAL_EXIT_AUTO_HIDE_MS 5000
#define PHONE_CONTROL_APP_ID "phone.control.app"
#define CLIENT_APP_ID "client.app"
#define VOICE_APP_ID "voice.app"
#define BASIC_APP_ID "basic"
#define PHONE_CONTROL_NETWORK_REFRESH_MS 1000
#define LAUNCHER_ENTRY_COUNT 5
#define LAUNCHER_PHONE_CONTROL_FIRMWARE_ENTRY_COUNT 2
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
    VOICE_RUNTIME_STAGE_STOPPED = 0,
    VOICE_RUNTIME_STAGE_PENDING,
    VOICE_RUNTIME_STAGE_AUDIO_STARTING,
    VOICE_RUNTIME_STAGE_WAITING_CLOUD,
    VOICE_RUNTIME_STAGE_WS_FAILED,
    VOICE_RUNTIME_STAGE_READY,
    VOICE_RUNTIME_STAGE_DEGRADED,
} voice_runtime_stage_t;

typedef enum {
    IDLE_HINT_READY = 0,
    IDLE_HINT_BLE_CONNECTED,
    IDLE_HINT_WIFI_SETUP_REQUIRED,
    IDLE_HINT_WIFI_RECOVERING,
    IDLE_HINT_WIFI_FAILED,
    IDLE_HINT_CLOUD_CONNECTING,
    IDLE_HINT_STM32_HANDSHAKE_FAILED,
} idle_hint_mode_t;

typedef enum {
    VOICE_CONNECT_VIEW_NONE = 0,
    VOICE_CONNECT_VIEW_WIFI_SETUP,
    VOICE_CONNECT_VIEW_WIFI_CONNECTING,
    VOICE_CONNECT_VIEW_WIFI_FAILED,
    VOICE_CONNECT_VIEW_WS_CONNECTING,
    VOICE_CONNECT_VIEW_WS_FAILED,
    VOICE_CONNECT_VIEW_RUNTIME_DEGRADED,
} voice_connect_view_t;

typedef enum {
    APP_CONNECT_ACTION_NONE = 0,
    APP_CONNECT_ACTION_RETRY,
    APP_CONNECT_ACTION_WIFI,
    APP_CONNECT_ACTION_SETTINGS,
} app_connect_action_t;

typedef enum {
    APP_WIFI_GATE_VIEW_NONE = 0,
    APP_WIFI_GATE_VIEW_SETUP,
    APP_WIFI_GATE_VIEW_CONNECTING,
    APP_WIFI_GATE_VIEW_FAILED,
} app_wifi_gate_view_t;

typedef struct {
    app_wifi_gate_view_t view;
    app_connect_action_t action;
} app_wifi_gate_state_t;

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

typedef enum {
    PROVISIONING_FEEDBACK_SOUND_NONE = 0,
    PROVISIONING_FEEDBACK_SOUND_BLE_CONNECTED,
    PROVISIONING_FEEDBACK_SOUND_WIFI_SAVED,
    PROVISIONING_FEEDBACK_SOUND_WIFI_CONNECTED,
    PROVISIONING_FEEDBACK_SOUND_WIFI_FAILED,
    PROVISIONING_FEEDBACK_SOUND_BLE_DISCONNECTED,
} provisioning_feedback_sound_t;

static bool s_waiting_for_wifi_provision = false;
static provision_manager_t s_provision_manager;
static volatile bool s_provision_credentials_saved_pending = false;
static volatile bool s_provision_wifi_status_pending_valid = false;
static volatile wifi_status_t s_provision_wifi_status_pending = WIFI_STATUS_UNCONFIGURED;
static bool s_boot_completed = false;
static bool s_ui_ready = false;
static app_ui_mode_core_t s_app_ui_mode;
static const char *s_app_ui_initial_text = "";
static bool s_cloud_runtime_started = false;
static bool s_voice_ready_ui_shown = false;
static volatile bool s_local_return_to_launcher_pending = false;
static voice_connect_view_t s_voice_connect_view = VOICE_CONNECT_VIEW_NONE;
static app_connect_action_t s_voice_connect_action = APP_CONNECT_ACTION_NONE;
static app_wifi_gate_state_t s_client_wifi_gate = {0};
#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static app_wifi_gate_state_t s_app_center_wifi_gate = {0};
#endif
static int64_t s_voice_connect_ws_wait_start_us = 0;
static int64_t s_voice_diag_stable_1s_due_us = 0;
static int64_t s_voice_diag_stable_3s_due_us = 0;
static int64_t s_voice_last_touch_tap_us = 0;
static volatile bool s_app_connect_action_click_pending = false;
static bool s_agent_ready_ui_shown = false;
static voice_connect_view_t s_agent_connect_view = VOICE_CONNECT_VIEW_NONE;
static app_connect_action_t s_agent_connect_action = APP_CONNECT_ACTION_NONE;
static volatile bool s_agent_connect_action_click_pending = false;
static bool s_agent_activity_seen = false;
static int s_agent_ready_idle_last_variant_index = -1;
static int64_t s_agent_ready_idle_next_switch_us = 0;
static int64_t s_agent_last_touch_tap_us = 0;
static bool s_agent_ready_idle_sleeping = false;
static agent_animation_flow_core_t s_agent_animation_flow;
static bool s_ws_stack_ready = false;
static bool s_discovery_initialized = false;
static bool s_last_ble_connected = false;
RTC_DATA_ATTR static uint32_t s_boot_sdcard_mount_restart_marker = 0;
static volatile bool s_ble_connected_feedback_pending = false;
static lv_group_t *s_phone_control_group = NULL;
static volatile bool s_phone_control_network_ui_active = false;
static bool s_phone_control_sleep_ui_active = false;
static int64_t s_phone_control_network_refresh_due_us = 0;
static char s_phone_control_ble_mac_text[24] = "-";
static volatile provisioning_feedback_sound_t s_provisioning_feedback_pending_sound = PROVISIONING_FEEDBACK_SOUND_NONE;
static bool s_provisioning_feedback_last_wifi_status_valid = false;
static wifi_status_t s_provisioning_feedback_last_wifi_status = WIFI_STATUS_UNCONFIGURED;
static bool s_wifi_failed_since_last_success = false;
static bool s_low_memory_recovery_active = false;
static bool s_ble_advertising_paused_for_recovery = false;
static bool s_cached_ws_attempted_since_wifi_restore = false;
static bool s_cached_ws_connect_inflight = false;
static bool s_ws_connect_inflight = false;
static bool s_boot_saved_wifi_unavailable = false;
static transport_state_t s_transport_state = TRANSPORT_BLE_IDLE_NO_CREDENTIALS;
static voice_runtime_stage_t s_voice_runtime_stage = VOICE_RUNTIME_STAGE_STOPPED;
static volatile bool s_discovery_inflight = false;
static TaskHandle_t s_discovery_task = NULL;
static uint32_t s_consecutive_wifi_resume_defers = 0;
static uint32_t s_consecutive_ws_start_defers = 0;
static uint32_t s_consecutive_discovery_failures = 0;
static uint32_t s_discovery_generation = 0;
static int64_t s_next_cloud_attempt_us = 0;
static int64_t s_cached_ws_connect_started_us = 0;
static int64_t s_ws_connect_started_us = 0;
static int64_t s_wifi_recovery_started_us = 0;
static int64_t s_voice_runtime_open_us = 0;
static QueueHandle_t s_discovery_result_queue = NULL;
static char s_cached_ws_url[CACHED_WS_URL_MAX_LEN] = {0};
static int s_ready_idle_variant_index = -1;
static int s_ready_idle_last_variant_index = -1;
static int64_t s_ready_idle_next_switch_us = 0;
static bool s_ready_idle_sleeping = false;
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
static bool s_mcu_runtime_active = false;
static bool s_mcu_runtime_services_initialized = false;
static bool s_head_rgb_initialized = false;
static bool s_startup_green_leds_applied = false;
static int64_t s_startup_green_leds_last_attempt_us = 0;
static int s_settings_volume_percent = CONFIG_WATCHER_AUDIO_VOLUME;
static int s_settings_brightness_percent = SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
static bool s_settings_rgb_enabled = true;
static volatile bool s_shutdown_in_progress = false;
static TaskHandle_t s_shutdown_task = NULL;
static TaskHandle_t s_power_button_fallback_task = NULL;
static uint32_t s_shutdown_power_seq = 0;
static bool s_runtime_button_callbacks_registered = false;
static bool s_ble_stack_initialized = false;
static volatile bool s_ble_provisioning_release_pending = false;
static int64_t s_ble_provisioning_release_due_us = 0;
static watcher_app_resource_mode_t s_app_resource_mode = WATCHER_APP_RESOURCE_OFF;
static watcher_app_resource_set_t s_app_resource_set = WATCHER_APP_RESOURCE_SET_NONE;
static char s_app_resource_owner[32] = "none";
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
typedef enum {
    DEBUG_APP_COMMAND_OPEN = 0,
    DEBUG_APP_COMMAND_CLOSE,
    DEBUG_APP_COMMAND_STATUS,
    DEBUG_APP_COMMAND_CONNECT,
} debug_app_command_type_t;

typedef struct {
    debug_app_command_type_t type;
    char app_id[DEBUG_APP_ID_MAX];
} debug_app_command_t;
#endif

typedef struct {
    bool connected;
} ble_connection_event_t;

#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
static QueueHandle_t s_debug_app_command_queue = NULL;
#endif
static QueueHandle_t s_ble_connection_event_queue = NULL;
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
static TaskHandle_t s_mcu_link_runtime_task = NULL;
#endif

static int app_ui_prepare_mode(void *ctx, app_ui_mode_t previous, app_ui_mode_t required, void **surface_out) {
    (void)ctx;
    if (surface_out != NULL) {
        *surface_out = NULL;
    }
    if (required == APP_UI_MODE_TEXT_ONLY) {
        return previous == APP_UI_MODE_NONE ? hal_display_ui_init_text_only(s_app_ui_initial_text) : 0;
    }
    if (required != APP_UI_MODE_ANIMATION) {
        return -1;
    }
    int result = previous == APP_UI_MODE_NONE ? hal_display_ui_init_with_text(s_app_ui_initial_text)
                                              : hal_display_ui_upgrade_to_animation();
    if (result == 0 && surface_out != NULL) {
        *surface_out = hal_display_get_animation_surface();
    }
    return result;
}

static int app_ui_bind_surface(void *ctx, void *surface) {
    (void)ctx;
    return animation_service_bind_surface(surface) == ANIMATION_SERVICE_OK ? 0 : -1;
}

static int app_ui_unbind_surface(void *ctx) {
    (void)ctx;
    animation_service_result_t result = animation_service_unbind_surface();
    return result == ANIMATION_SERVICE_OK || result == ANIMATION_SERVICE_NOT_INITIALIZED ? 0 : -1;
}

static void app_ui_rollback_mode(void *ctx, app_ui_mode_t previous, app_ui_mode_t attempted) {
    (void)ctx;
    (void)attempted;
    if (previous == APP_UI_MODE_NONE) {
        hal_display_ui_deinit();
    }
}

static void app_ui_release_mode(void *ctx, app_ui_mode_t mode) {
    (void)ctx;
    (void)mode;
    hal_display_ui_deinit();
}

static const app_ui_mode_ops_t s_app_ui_mode_ops = {
    .prepare_mode = app_ui_prepare_mode,
    .bind_surface = app_ui_bind_surface,
    .unbind_surface = app_ui_unbind_surface,
    .rollback_mode = app_ui_rollback_mode,
    .release_mode = app_ui_release_mode,
    .ctx = NULL,
};

static int app_animation_ui_ensure(app_ui_mode_t mode, const char *initial_text) {
    s_app_ui_initial_text = initial_text != NULL ? initial_text : "";
    app_ui_mode_result_t result = app_ui_mode_core_ensure(&s_app_ui_mode, mode, &s_app_ui_mode_ops);
    s_ui_ready = app_ui_mode_core_get(&s_app_ui_mode) != APP_UI_MODE_NONE;
    if (result != APP_UI_MODE_RESULT_OK) {
        ESP_LOGE(TAG, "UI mode ensure failed required=%d current=%d result=%d", (int)mode,
                 (int)app_ui_mode_core_get(&s_app_ui_mode), (int)result);
        return -1;
    }
    return 0;
}

static int app_animation_ui_init(void) {
    return app_animation_ui_ensure(APP_UI_MODE_ANIMATION, "");
}

static int app_animation_ui_init_with_text(const char *initial_text) {
    return app_animation_ui_ensure(APP_UI_MODE_ANIMATION, initial_text);
}

static int app_animation_ui_init_text_only(const char *initial_text) {
    return app_animation_ui_ensure(APP_UI_MODE_TEXT_ONLY, initial_text);
}

static void app_animation_ui_deinit(void) {
    app_ui_mode_result_t result = app_ui_mode_core_close(&s_app_ui_mode, &s_app_ui_mode_ops);
    if (result != APP_UI_MODE_RESULT_OK) {
        ESP_LOGE(TAG, "UI close refused because animation surface could not unbind: %d", (int)result);
        return;
    }
    s_ui_ready = false;
}

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
        return "WIFI_NO_CREDENTIALS";
    case TRANSPORT_BLE_IDLE_WIFI_STARTING:
        return "WIFI_STARTING";
    case TRANSPORT_BLE_IDLE_WIFI_CONNECTING:
        return "WIFI_CONNECTING";
    case TRANSPORT_BLE_IDLE_DISCOVERING:
        return "CLOUD_DISCOVERING";
    case TRANSPORT_BLE_IDLE_WS_CONNECTING:
        return "WS_CONNECTING";
    case TRANSPORT_BLE_IDLE_CLOUD_READY:
        return "CLOUD_READY";
    case TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED:
    default:
        return "CLOUD_SUSPENDED";
    }
}

static void reset_ready_idle_handoff(transport_state_t state) {
    if (state == TRANSPORT_BLE_IDLE_CLOUD_READY) {
        s_ready_idle_variant_index = -1;
        s_ready_idle_next_switch_us = 0;
        s_ready_idle_sleeping = false;
        s_ready_idle_standby_transition_pending = false;
        s_ready_idle_standby_transition_deadline_us = 0;
        ESP_LOGI(TAG, "Ready idle handoff armed; happy completion is driven by its animation ticket");
        return;
    }

    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_sleeping = false;
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
    reset_ready_idle_handoff(state);
}

static void transport_schedule_retry(uint32_t delay_ms) {
    s_next_cloud_attempt_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000LL);
}

static bool transport_retry_due(void) {
    return s_next_cloud_attempt_us == 0 || esp_timer_get_time() >= s_next_cloud_attempt_us;
}

static uint32_t transport_note_discovery_failure_and_get_retry_delay(void) {
    uint32_t delay_ms;

    if (s_consecutive_discovery_failures < UINT32_MAX) {
        s_consecutive_discovery_failures++;
    }

    delay_ms = CLOUD_DISCOVERY_RETRY_INITIAL_DELAY_MS;
    if (s_consecutive_discovery_failures > 1U) {
        uint32_t extra_failures = s_consecutive_discovery_failures - 1U;
        uint32_t max_extra = (CLOUD_DISCOVERY_RETRY_MAX_DELAY_MS - CLOUD_DISCOVERY_RETRY_INITIAL_DELAY_MS) /
                             CLOUD_DISCOVERY_RETRY_STEP_MS;
        if (extra_failures > max_extra) {
            extra_failures = max_extra;
        }
        delay_ms += extra_failures * CLOUD_DISCOVERY_RETRY_STEP_MS;
    }

    return delay_ms;
}

static void transport_reset_discovery_retry_backoff(const char *reason) {
    if (s_consecutive_discovery_failures == 0U) {
        return;
    }

    ESP_LOGI(TAG, "Discovery retry backoff reset after %lu failure(s): %s",
             (unsigned long)s_consecutive_discovery_failures, reason != NULL ? reason : "none");
    s_consecutive_discovery_failures = 0;
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

static const char *sleep_wakeup_cause_to_string(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return "undefined";
    case ESP_SLEEP_WAKEUP_EXT0:
        return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1:
        return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
        return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "gpio";
    case ESP_SLEEP_WAKEUP_UART:
        return "uart";
    case ESP_SLEEP_WAKEUP_WIFI:
        return "wifi";
    case ESP_SLEEP_WAKEUP_COCPU:
        return "cocpu";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return "cocpu_trap";
    case ESP_SLEEP_WAKEUP_BT:
        return "bt";
    default:
        return "unknown";
    }
}

static void log_sleep_wakeup_cause(void) {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "Sleep wakeup cause: %s (%d)", sleep_wakeup_cause_to_string(cause), (int)cause);
}

static esp_err_t ensure_nvs_flash_ready(void) {
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t soft_off_set_latched(bool latched) {
    nvs_handle_t handle;
    esp_err_t ret = ensure_nvs_flash_ready();

    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_open(SOFT_OFF_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_u8(handle, SOFT_OFF_NVS_KEY_LATCHED, latched ? 1u : 0u);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t soft_off_get_latched(bool *out_latched) {
    nvs_handle_t handle;
    uint8_t value = 0;
    esp_err_t ret;

    if (out_latched == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_latched = false;
    ret = ensure_nvs_flash_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_open(SOFT_OFF_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_u8(handle, SOFT_OFF_NVS_KEY_LATCHED, &value);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    *out_latched = value != 0u;
    return ESP_OK;
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
    esp_log_level_set(MCU_OBS_TAG, ESP_LOG_INFO);
    esp_log_level_set("MCU_LINK_BOOT", ESP_LOG_INFO);
    esp_log_level_set("MCU_LINK_UART", ESP_LOG_INFO);
    esp_log_level_set("MEM_MON", ESP_LOG_INFO);
    esp_log_level_set(UI_DIAG_TAG, ESP_LOG_INFO);
    esp_log_level_set("BSP", ESP_LOG_INFO);
    esp_log_level_set("VOICE", ESP_LOG_INFO);
    esp_log_level_set("HAL_AUDIO", ESP_LOG_INFO);
    esp_log_level_set("HAL_WAKE_WORD", ESP_LOG_INFO);
    esp_log_level_set("SFX_SERVICE", ESP_LOG_INFO);
    esp_log_level_set("WS_CLIENT", ESP_LOG_INFO);
    esp_log_level_set("APP_CENTER", ESP_LOG_INFO);
#if CONFIG_WATCHER_ANIM_DEBUG_PERF
    esp_log_level_set("ANIM_PLAYER", ESP_LOG_INFO);
    esp_log_level_set("DISPLAY_UI", ESP_LOG_INFO);
    esp_log_level_set("BEHAVIOR_STATE", ESP_LOG_INFO);
#endif
#endif
}

static void on_ble_connection_changed(bool connected);
static void transport_cancel_discovery(const char *reason);
static void transport_abort_discovery_request(const char *reason);
static void transport_reset_discovery_result_queue(void);
static bool transport_wait_for_discovery_idle(uint32_t timeout_ms);
static bool transport_discovery_cancel_requested(void *ctx);
static void transport_stop_ws(const char *reason);
static esp_err_t transport_deinit_ws_stack(const char *reason);
static void transport_set_ble_recovery_advertising_paused(bool paused, const char *reason);
static void transport_suspend_cloud_runtime_for_low_memory(const char *reason);
static void transport_enter_low_memory_recovery(const char *reason);
static void transport_reset_low_memory_recovery(const char *reason);
static int transport_prepare_ws_client(const char *ws_url);
static void transport_reset_cached_ws_resume_state(void);
static void wait_for_behavior_idle(uint32_t timeout_ms);
static void maybe_apply_startup_green_leds(void);
static void service_mcu_link_runtime(void);
static void shutdown_freeze_app_inputs(void);
static void shutdown_sequence_delay(void *ctx, uint32_t delay_ms);
static esp_err_t settings_submit_rgb_light(bool enabled);
static esp_err_t app_resource_start_mcu_runtime(const char *reason);
static void app_resource_stop_mcu_runtime(const char *reason);
static const char *voice_runtime_stage_to_string(voice_runtime_stage_t stage);
static void voice_runtime_request_start(const char *reason);
static void voice_runtime_reset(const char *reason);
static void voice_runtime_tick(void);
static void ensure_cloud_runtime_started(void);
static void voice_app_update_connect_ui_if_needed(void);
static void voice_app_retry_connect(const char *reason);
static void app_connect_process_action_click(void);
static void transport_begin_wifi_resume(const char *reason);
static void boot_resume_saved_wifi_if_needed(void);
static void launcher_request_status_refresh(void);
static void on_time_synchronized(void);
static void maybe_play_ble_connected_feedback(void);
static void boot_halt_with_error(const char *error_msg);
static void log_directory_contents(const char *path);
static int boot_prepare_animation_assets(void);
static esp_err_t app_resource_apply(watcher_app_resource_mode_t mode, watcher_app_resource_set_t resources,
                                    const char *app_id);
static const char *app_resource_mode_to_string(watcher_app_resource_mode_t mode);
static void app_ui_diag_log_snapshot(const char *stage);
static void mem_monitor_fill_app_context(char *app_buf, size_t app_buf_size, char *resource_buf,
                                         size_t resource_buf_size);
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
static void debug_app_control_init(void);
static void debug_app_control_tick(void);
static esp_err_t debug_app_enqueue_open(const char *app_id);
static esp_err_t debug_app_enqueue_close(void);
static esp_err_t debug_app_enqueue_status(void);
static esp_err_t debug_app_enqueue_connect(void);
static esp_err_t debug_settings_open_page(const char *page_id);
static esp_err_t debug_settings_focus_target(const char *target_id);
static esp_err_t debug_settings_rotate(int steps);
static esp_err_t debug_settings_click(void);
static esp_err_t debug_settings_status(void);
#endif
static void ble_connection_event_init(void);
static void ble_connection_event_tick(void);
static void on_ble_connection_event(bool connected);
static void ble_provisioning_schedule_release(const char *reason);
static void ble_provisioning_release_tick(void);
static void settings_update_stm32_git_ref_from_link(mcu_link_t *link);
static void settings_request_state_refresh(void);
static void settings_on_ble_wifi_configured(ble_service_wifi_config_event_t event);
static void provisioning_feedback_tick(void);
static void transport_schedule_retry(uint32_t delay_ms);
static void local_behavior_ui_open_ex(const char *state_id, const char *text, const char *anim_id, const char *sound_id,
                                      bool enable_animation);
static void local_behavior_ui_open(const char *state_id, const char *text, const char *anim_id, const char *sound_id);
static void local_app_tick(void);
static void local_behavior_app_cleanup(void);
static void local_app_on_button(void);
static bool local_exit_is_visible(void);
static bool power_monitor_behavior_gate(power_monitor_behavior_t behavior, const power_monitor_event_t *event,
                                        void *user_ctx);
static bool active_app_is(const char *id);
static bool is_basic_app_active(void);
static bool app_id_is_client_voice_host(const char *app_id);
static bool active_app_is_client_voice_host(void);
static bool active_app_is_voice_runtime_context(void);
static void settings_open_wifi_detail_from_app(void);
static void app_connect_action_clicked(void *user_ctx);
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static void arm_firmware_app_return_to_launcher_on_next_boot(const char *app_label);
#endif
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
        if (s_boot_sdcard_mount_restart_marker != BOOT_SDCARD_MOUNT_RESTART_MAGIC) {
            s_boot_sdcard_mount_restart_marker = BOOT_SDCARD_MOUNT_RESTART_MAGIC;
            ESP_LOGW(TAG, "SD mount failed on first boot attempt; restarting once before showing boot error");
            boot_anim_set_text("Retrying SD...");
            boot_anim_set_detail_text("Restarting");
            vTaskDelay(pdMS_TO_TICKS(BOOT_SDCARD_MOUNT_RESTART_DELAY_MS));
            esp_restart();
        }
        s_boot_sdcard_mount_restart_marker = 0;
        boot_halt_with_error("SD mount failed");
    }
    s_boot_sdcard_mount_restart_marker = 0;
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
    animation_service_result_t result;

    result = animation_service_suspend(ANIMATION_SUSPEND_HOLD_LAST_FRAME);
    if (result == ANIMATION_SERVICE_OK) {
        ESP_LOGI(TAG, "Suspended animation while bringing up cloud transport");
    }
}

static void transport_resume_display_motion(void) {
    animation_service_result_t result = animation_service_resume();

    if (result != ANIMATION_SERVICE_OK) {
        ESP_LOGW(TAG, "Failed to resume animation after cloud transport start: %d", (int)result);
        return;
    }
    (void)behavior_state_refresh_animation();
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

static void transport_reset_ws_connect_watchdog(void) {
    s_ws_connect_inflight = false;
    s_ws_connect_started_us = 0;
}

static void transport_arm_ws_connect_watchdog(void) {
    s_ws_connect_inflight = true;
    s_ws_connect_started_us = esp_timer_get_time();
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

static void boot_resume_saved_wifi_if_needed(void) {
    if (wifi_has_credentials() != 1) {
        return;
    }
    if (ble_service_is_connected()) {
        ESP_LOGI(TAG, "Boot saved Wi-Fi resume skipped while BLE is connected");
        return;
    }
    if (wifi_is_connected() == 1 || wifi_is_connect_requested() == 1) {
        return;
    }

    ESP_LOGI(TAG, "Boot resuming saved Wi-Fi connection");
    if (wifi_resume_background() != 0) {
        ESP_LOGW(TAG, "Boot saved Wi-Fi resume failed");
    }
}

static bool boot_wait_for_time_sync(uint32_t timeout_ms) {
    int64_t deadline_us;

    if (time_sync_service_has_valid_time()) {
        return true;
    }

    if (time_sync_service_start_on_network() != ESP_OK) {
        ESP_LOGW(TAG, "Boot time sync start failed");
        return false;
    }

    deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);
    while (!time_sync_service_has_valid_time() && esp_timer_get_time() < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(BOOT_NETWORK_READY_POLL_MS));
    }

    if (time_sync_service_has_valid_time()) {
        ESP_LOGI(TAG, "Boot time sync ready before launcher");
        return true;
    }

    ESP_LOGW(TAG, "Boot time sync timed out after %u ms", (unsigned)timeout_ms);
    return false;
}

static void boot_prepare_saved_wifi_before_launcher(void) {
    if (wifi_has_credentials() != 1) {
        s_boot_saved_wifi_unavailable = false;
        s_waiting_for_wifi_provision = true;
        boot_anim_set_text("Reconnect BLE to set Wi-Fi");
        boot_anim_set_detail_text("");
        ESP_LOGI(TAG, "WiFi credentials missing; Provision app can start BLE setup on demand");
        return;
    }

    s_waiting_for_wifi_provision = false;
    s_boot_saved_wifi_unavailable = false;
    boot_anim_set_text("Connecting Wi-Fi...");
    boot_anim_set_detail_text("Saved network");
    ESP_LOGI(TAG, "Stored WiFi credentials detected; preparing WiFi before launcher");
    boot_resume_saved_wifi_if_needed();

    if (wifi_wait_for_connection((int)BOOT_WIFI_CONNECT_TIMEOUT_MS) != 0) {
        s_boot_saved_wifi_unavailable = true;
        s_wifi_failed_since_last_success = true;
        boot_anim_set_text("Wi-Fi unavailable");
        boot_anim_set_detail_text("Open Settings to reconnect");
        ESP_LOGW(TAG, "Boot saved Wi-Fi timed out after %u ms; launcher will start offline",
                 (unsigned)BOOT_WIFI_CONNECT_TIMEOUT_MS);
        wifi_disconnect();
        return;
    }

    boot_anim_set_text("Syncing time...");
    boot_anim_set_detail_text("Network ready");
    if (!boot_wait_for_time_sync(BOOT_TIME_SYNC_TIMEOUT_MS)) {
        boot_anim_set_detail_text("Time sync pending");
    } else {
        boot_anim_set_detail_text("");
    }
}

static bool provisioning_feedback_visible_context(void) {
    return active_app_is("settings.app") || active_app_is("provision.app") || active_app_is("ble.app") ||
           active_app_is(PHONE_CONTROL_APP_ID);
}

static const char *provisioning_feedback_sound_id(provisioning_feedback_sound_t sound) {
    switch (sound) {
    case PROVISIONING_FEEDBACK_SOUND_BLE_CONNECTED:
        return PROVISIONING_FEEDBACK_SOUND_ID_BLE_CONNECTED;
    case PROVISIONING_FEEDBACK_SOUND_WIFI_SAVED:
        return PROVISIONING_FEEDBACK_SOUND_ID_WIFI_SAVED;
    case PROVISIONING_FEEDBACK_SOUND_WIFI_CONNECTED:
        return PROVISIONING_FEEDBACK_SOUND_ID_WIFI_CONNECTED;
    case PROVISIONING_FEEDBACK_SOUND_WIFI_FAILED:
        return PROVISIONING_FEEDBACK_SOUND_ID_WIFI_FAILED;
    case PROVISIONING_FEEDBACK_SOUND_BLE_DISCONNECTED:
        return PROVISIONING_FEEDBACK_SOUND_ID_BLE_DISCONNECTED;
    case PROVISIONING_FEEDBACK_SOUND_NONE:
    default:
        return NULL;
    }
}

static void provisioning_feedback_queue_sound(provisioning_feedback_sound_t sound) {
    if (sound == PROVISIONING_FEEDBACK_SOUND_NONE || !provisioning_feedback_visible_context()) {
        return;
    }
    s_provisioning_feedback_pending_sound = sound;
}

static void provisioning_feedback_note_wifi_status(wifi_status_t status) {
    if (!provisioning_feedback_visible_context()) {
        return;
    }
    if (s_provisioning_feedback_last_wifi_status_valid && s_provisioning_feedback_last_wifi_status == status) {
        return;
    }

    s_provisioning_feedback_last_wifi_status = status;
    s_provisioning_feedback_last_wifi_status_valid = true;

    if (status == WIFI_STATUS_CONNECTED) {
        provisioning_feedback_queue_sound(PROVISIONING_FEEDBACK_SOUND_WIFI_CONNECTED);
    } else if (status == WIFI_STATUS_DISCONNECTED && !ble_service_is_connected() && wifi_has_credentials() == 1) {
        provisioning_feedback_queue_sound(PROVISIONING_FEEDBACK_SOUND_WIFI_FAILED);
    }
}

static void provisioning_feedback_tick(void) {
    provisioning_feedback_sound_t sound = s_provisioning_feedback_pending_sound;
    const char *sound_id;
    esp_err_t ret;

    if (sound == PROVISIONING_FEEDBACK_SOUND_NONE || !s_ui_ready) {
        return;
    }

    s_provisioning_feedback_pending_sound = PROVISIONING_FEEDBACK_SOUND_NONE;
    sound_id = provisioning_feedback_sound_id(sound);
    if (sound_id == NULL || sound_id[0] == '\0') {
        return;
    }

    ret = sfx_service_play(sound_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Provisioning feedback sound skipped id=%s ret=%s", sound_id, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Provisioning feedback sound played id=%s", sound_id);
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
    bool provision_app_active = active_app_is("provision.app");
    watcher_app_resource_mode_t resource_mode = s_app_resource_mode;
    bool wifi_expected = wifi_has_credentials() == 1 || s_waiting_for_wifi_provision;

    switch (status) {
    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected: ssid=%s ip=%s", ssid ? ssid : "<unknown>", ip_addr ? ip_addr : "<no-ip>");
        s_boot_saved_wifi_unavailable = false;
        transport_reset_discovery_retry_backoff("wifi connected");
        if (!ble_service_is_connected() && (s_transport_state == TRANSPORT_BLE_IDLE_NO_CREDENTIALS ||
                                            s_transport_state == TRANSPORT_BLE_IDLE_WIFI_STARTING ||
                                            s_transport_state == TRANSPORT_BLE_IDLE_WIFI_CONNECTING)) {
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "wifi connected");
        }
        (void)time_sync_service_start_on_network();
        s_wifi_failed_since_last_success = false;
        if (!s_low_memory_recovery_active) {
            transport_reset_low_memory_recovery("wifi connected");
        }
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Wi-Fi connected");
        }
        if (provision_app_active && s_ui_ready) {
            char wifi_feedback[96];
            if (ssid && ssid[0] != '\0') {
                snprintf(wifi_feedback, sizeof(wifi_feedback), "WiFi connected\n%s", ssid);
            } else {
                snprintf(wifi_feedback, sizeof(wifi_feedback), "WiFi connected");
            }
            (void)behavior_state_set_with_resources("happy", wifi_feedback, 0, "happy", "");
            (void)behavior_state_set_text_style(wifi_feedback, 0, false);
            (void)hal_display_set_text_with_style(wifi_feedback, 0, false);
        }
        break;

    case WIFI_STATUS_CONNECTING:
        ESP_LOGI(TAG, "WiFi connecting: ssid=%s", ssid ? ssid : "<unknown>");
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Connecting Wi-Fi...");
        }
        if (provision_app_active && s_ui_ready) {
            char wifi_feedback[96];
            if (ssid && ssid[0] != '\0') {
                snprintf(wifi_feedback, sizeof(wifi_feedback), "Waiting WiFi\n%s", ssid);
            } else {
                snprintf(wifi_feedback, sizeof(wifi_feedback), "Waiting WiFi");
            }
            (void)behavior_state_set_text_style(wifi_feedback, 0, false);
            (void)hal_display_set_text_with_style(wifi_feedback, 0, false);
        }
        break;

    case WIFI_STATUS_DISCONNECTED:
        if (wifi_expected) {
            ESP_LOGW(TAG, "WiFi connect failed or disconnected: ssid=%s", ssid ? ssid : "<unknown>");
        } else {
            ESP_LOGI(TAG, "WiFi disconnected while resource=%s", app_resource_mode_to_string(resource_mode));
        }
        if (wifi_expected && !ble_service_is_connected() && wifi_has_credentials() == 1) {
            s_wifi_failed_since_last_success = true;
        }
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Wi-Fi failed");
        }
        break;

    case WIFI_STATUS_UNCONFIGURED:
    default:
        ESP_LOGI(TAG, "WiFi unconfigured");
        s_boot_saved_wifi_unavailable = false;
        s_wifi_failed_since_last_success = false;
        transport_reset_low_memory_recovery("wifi unconfigured");
        if (!s_boot_completed && s_waiting_for_wifi_provision) {
            boot_anim_set_text("Reconnect BLE to set Wi-Fi");
        }
        break;
    }
    s_provision_wifi_status_pending = status;
    s_provision_wifi_status_pending_valid = true;
    provisioning_feedback_note_wifi_status(status);
    settings_request_state_refresh();
    launcher_request_status_refresh();
}

static void on_button_single_click_dispatch(void) {
    const watcher_app_t *active_app = watcher_app_get_active();
    if (active_app != NULL && active_app->on_button != NULL) {
        ESP_LOGI(TAG, "Dispatching button single-click to app %s", active_app->id ? active_app->id : "(unknown)");
        active_app->on_button();
    } else {
        ESP_LOGI(TAG, "Button single-click ignored; no active app handler");
    }
}

static watcher_input_scope_t current_input_scope(void *user_ctx) {
    (void)user_ctx;
    const watcher_app_t *active_app = watcher_app_get_active();
    watcher_input_scope_t scope = {
        .context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .owner_token = 0,
    };

    if (!s_shutdown_in_progress && active_app != NULL) {
        const watcher_input_context_t app_context =
            active_app->get_input_context != NULL ? active_app->get_input_context() : active_app->input_context;
        scope.context =
            app_context == WATCHER_INPUT_CONTEXT_UNSPECIFIED ? WATCHER_INPUT_CONTEXT_SYSTEM_ONLY : app_context;
        scope.owner_token = (uintptr_t)active_app;
    }
    return scope;
}

static int shutdown_power_channel_start(void *ctx) {
    (void)ctx;
    esp_err_t ret = mcu_link_bootstrap_init();

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown MCU link init failed: %s", esp_err_to_name(ret));
        return (int)ret;
    }

    ret = mcu_link_bootstrap_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown MCU link start failed: %s", esp_err_to_name(ret));
    }

    ret = mcu_power_service_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown MCU power service init failed: %s", esp_err_to_name(ret));
        return (int)ret;
    }

    return mcu_link_bootstrap_get_link() != NULL ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static int shutdown_power_channel_poll_once(void *ctx) {
    (void)ctx;
    mcu_link_event_t event = {0};
    const esp_err_t ret = mcu_link_bootstrap_poll(&event);

    if (ret == ESP_OK) {
        (void)mcu_power_service_handle_link_event(&event);
    }
    return (int)ret;
}

static bool shutdown_power_channel_is_ready(void *ctx) {
    (void)ctx;
    return mcu_link_bootstrap_is_ready();
}

static void shutdown_prepare_power_channel(void) {
    const button_shutdown_power_channel_ops_t ops = {
        .ensure_started = shutdown_power_channel_start,
        .poll_once = shutdown_power_channel_poll_once,
        .is_ready = shutdown_power_channel_is_ready,
        .delay_ms = shutdown_sequence_delay,
        .ctx = NULL,
    };
    const button_shutdown_power_channel_result_t result =
        button_shutdown_power_channel_prepare(&ops, STM32_POWER_CHANNEL_READY_WAIT_MS, STM32_POWER_CHANNEL_POLL_MS);

    ESP_LOGW(TAG, "Shutdown MCU power channel prepared: started=%u ready=%u start_status=%s last_poll=%s waited=%lu ms",
             result.started ? 1u : 0u, result.ready ? 1u : 0u, esp_err_to_name((esp_err_t)result.start_status),
             esp_err_to_name((esp_err_t)result.last_poll_status), (unsigned long)result.waited_ms);
}

static int shutdown_sequence_request_5v_off(void *ctx) {
    uint32_t *power_seq = (uint32_t *)ctx;
    esp_err_t ret;

    shutdown_prepare_power_channel();
    ret = mcu_power_set_5v_enabled(false, MCU_POWER_SOURCE_BEHAVIOR, power_seq);

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
    uint32_t waited_ms = 0;

    while (waited_ms < delay_ms) {
        const uint32_t step_ms =
            (delay_ms - waited_ms) > STM32_POWER_CHANNEL_POLL_MS ? STM32_POWER_CHANNEL_POLL_MS : (delay_ms - waited_ms);
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited_ms += step_ms;
        if (mcu_link_bootstrap_get_link() != NULL) {
            (void)shutdown_power_channel_poll_once(NULL);
        }
    }
}

static int shutdown_feedback_play_sound(void *ctx, const char *sound_id) {
    (void)ctx;
    const esp_err_t ret = sfx_service_play(sound_id);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown feedback sound skipped id=%s ret=%s", sound_id != NULL ? sound_id : "<null>",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Shutdown feedback sound queued id=%s", sound_id);
    }

    return (int)ret;
}

static bool shutdown_feedback_sound_busy(void *ctx) {
    (void)ctx;
    return sfx_service_is_busy();
}

static void shutdown_feedback_display_off(void *ctx) {
    (void)ctx;
    esp_err_t ret = bsp_lcd_brightness_set(0);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown LCD backlight off failed: %s", esp_err_to_name(ret));
    }

    ret = bsp_exp_io_set_level(BSP_PWR_LCD, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown LCD rail off failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "Shutdown display turned off before power rail fallback");
    }
}

static void shutdown_present_user_feedback(void) {
    const button_shutdown_feedback_ops_t ops = {
        .play_sound = shutdown_feedback_play_sound,
        .sound_busy = shutdown_feedback_sound_busy,
        .display_off = shutdown_feedback_display_off,
        .delay_ms = shutdown_sequence_delay,
        .ctx = NULL,
    };
    animation_request_t animation_request = {
        .type = EMOJI_ANIM_ERROR,
        .priority = ANIM_PRIORITY_SYSTEM,
        .preempt_policy = ANIM_PROTECTED_AFTER_COMMIT,
        .repeat_count = 0,
        .source = ANIM_SOURCE_POWER,
        .owner_epoch = 1,
        .correlation_id = 0,
    };
    animation_ticket_t animation_ticket = ANIMATION_TICKET_INVALID;
    esp_err_t ret = behavior_state_set_with_resources("error", "Shutting down...", 0, "", "");

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown behavior feedback failed: %s", esp_err_to_name(ret));
    }

    animation_service_result_t animation_result = animation_submit(&animation_request, &animation_ticket);
    if (animation_result != ANIMATION_SERVICE_OK) {
        ESP_LOGW(TAG, "Shutdown SYSTEM animation submit failed: %d", (int)animation_result);
    } else {
        ESP_LOGI(TAG, "Shutdown SYSTEM animation accepted ticket=%lu", (unsigned long)animation_ticket);
    }

    ret = voice_recorder_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Shutdown voice recorder stop failed: %s", esp_err_to_name(ret));
    }

    sfx_service_set_cloud_audio_busy(false);
    sfx_service_set_voice_audio_busy(false);

    const button_shutdown_feedback_result_t result = button_shutdown_feedback_run(
        &ops, SHUTDOWN_FEEDBACK_SOUND_ID, SHUTDOWN_FEEDBACK_SOUND_WAIT_MS, SHUTDOWN_FEEDBACK_POLL_MS);

    ESP_LOGW(TAG, "Shutdown feedback complete: sound_status=%s sound_finished=%u display_off=%u waited=%lu ms",
             esp_err_to_name((esp_err_t)result.play_status), result.sound_finished ? 1u : 0u,
             result.display_off_called ? 1u : 0u, (unsigned long)result.waited_ms);
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

static int shutdown_sequence_hold_line_level(void *ctx) {
    (void)ctx;
    return bsp_knob_btn_get_key_value(NULL);
}

static void shutdown_sequence_prepare_sleep_idle(void *ctx) {
    (void)ctx;
    (void)bsp_knob_btn_get_key_value(NULL);
}

static void shutdown_sequence_enter_sleep(void *ctx, uint32_t wake_after_sec) {
    (void)ctx;
    bsp_system_deep_sleep(wake_after_sec);
}

static void shutdown_sequence_low_power_fallback(void *ctx) {
    const button_shutdown_low_power_ops_t sleep_ops = {
        .wake_line_level = shutdown_sequence_wake_line_level,
        .hold_line_level = shutdown_sequence_hold_line_level,
        .prepare_idle = shutdown_sequence_prepare_sleep_idle,
        .delay_ms = shutdown_sequence_delay,
        .enter_sleep = shutdown_sequence_enter_sleep,
        .ctx = ctx,
    };

    ESP_LOGE(TAG,
             "System power rail still alive after shutdown request; entering BSP deep sleep fallback "
             "(button wake, wait idle up to %u ms)",
             (unsigned)ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS);
    while (true) {
        const button_shutdown_low_power_result_t result =
            button_shutdown_low_power_enter(&sleep_ops, ESP32_SHUTDOWN_WAKE_AFTER_SEC, ESP32_SHUTDOWN_WAKE_IDLE_WAIT_MS,
                                            ESP32_SHUTDOWN_WAKE_IDLE_POLL_MS);
        if (result.sleep_requested) {
            return;
        }
        ESP_LOGW(TAG, "Shutdown sleep postponed: wake_idle=%u hold_idle=%u waited=%lu ms; waiting for release/idle",
                 result.wake_line_idle ? 1u : 0u, result.hold_line_idle ? 1u : 0u, (unsigned long)result.waited_ms);
        shutdown_sequence_delay(ctx, ESP32_SHUTDOWN_WAKE_IDLE_POLL_MS);
    }
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
    shutdown_freeze_app_inputs();
    esp_err_t latch_ret = soft_off_set_latched(true);
    if (latch_ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft-off latch set failed: %s; shutdown will continue without boot guard",
                 esp_err_to_name(latch_ret));
    } else {
        ESP_LOGW(TAG, "Soft-off latch set for user shutdown");
    }

    ESP_LOGW(TAG, "Button long press - shutting down");
    shutdown_present_user_feedback();

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

static void on_button_long_hold_source(void) {
    const watcher_input_result_t result =
        watcher_input_router_global_on_long_hold((uint32_t)(esp_timer_get_time() / 1000LL));

    ESP_LOGI(TAG, "input context=%s event=%s owner=%s reject=%s duration_ms=%lu",
             watcher_input_context_name(result.scope.context), watcher_input_event_name(result.event),
             watcher_input_owner_name(result.owner), watcher_input_reject_name(result.reject),
             (unsigned long)result.duration_ms);
    if (result.event == WATCHER_INPUT_EVENT_LONG_HOLD && result.owner == WATCHER_INPUT_OWNER_SYSTEM) {
        on_button_shutdown_hold();
    }
}

static void power_button_shutdown_fallback_task(void *arg) {
    button_shutdown_hold_detector_t detector;
    esp_err_t init_ret;

    (void)arg;
    init_ret = bsp_knob_btn_init(NULL);
    if (init_ret != ESP_OK) {
        ESP_LOGE(TAG, "Power-button shutdown fallback unavailable: %s", esp_err_to_name(init_ret));
        s_power_button_fallback_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    button_shutdown_hold_detector_init(&detector, POWER_BUTTON_FALLBACK_DEBOUNCE_MS, BUTTON_SHUTDOWN_HOLD_MS,
                                       POWER_BUTTON_FALLBACK_REARM_RELEASE_MS);
    ESP_LOGW(TAG, "Power-button shutdown fallback monitor started; short-click routing remains disabled");
    while (true) {
        const bool raw_pressed = bsp_knob_btn_get_key_value(NULL) == GLOBAL_POWER_BUTTON_ACTIVE_LEVEL;

        if (button_shutdown_hold_detector_update(&detector, raw_pressed, POWER_BUTTON_FALLBACK_POLL_MS)) {
            ESP_LOGW(TAG, "Power-button fallback detected %u ms hold", (unsigned)BUTTON_SHUTDOWN_HOLD_MS);
            on_button_shutdown_hold();
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_FALLBACK_POLL_MS));
    }
}

static void start_power_button_shutdown_fallback(const char *reason) {
    if (s_runtime_button_callbacks_registered || s_power_button_fallback_task != NULL) {
        return;
    }

    ESP_LOGW(TAG, "Starting power-button shutdown fallback: %s", reason != NULL ? reason : "input unavailable");
    if (xTaskCreate(power_button_shutdown_fallback_task, "button_fallback", POWER_BUTTON_FALLBACK_TASK_STACK_BYTES,
                    NULL, 5, &s_power_button_fallback_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power-button shutdown fallback task");
        s_power_button_fallback_task = NULL;
    }
}

static bool soft_off_sample_user_power_press(void) {
    uint32_t waited_ms = 0;

    while (waited_ms <= SOFT_OFF_BOOT_PRESS_CONFIRM_MS) {
        if (bsp_knob_btn_get_key_value(NULL) == GLOBAL_POWER_BUTTON_ACTIVE_LEVEL) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(SOFT_OFF_BOOT_PRESS_POLL_MS));
        waited_ms += SOFT_OFF_BOOT_PRESS_POLL_MS;
    }
    return false;
}

static void boot_hold_if_soft_off_latched(void) {
    bool latched = false;
    bool user_power_pressed = false;
    esp_err_t ret = soft_off_get_latched(&latched);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Soft-off latch read failed: %s; continuing normal boot", esp_err_to_name(ret));
        return;
    }
    if (!latched) {
        return;
    }

    if (bsp_knob_btn_init(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Soft-off latch active but knob read unavailable; continuing normal boot");
        return;
    }

    user_power_pressed = soft_off_sample_user_power_press();
    switch (button_shutdown_soft_off_decide(latched, user_power_pressed)) {
    case BUTTON_SHUTDOWN_BOOT_CLEAR_LATCH_AND_CONTINUE:
        ret = soft_off_set_latched(false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Soft-off latch clear failed: %s; continuing boot by user request", esp_err_to_name(ret));
        } else {
            ESP_LOGW(TAG, "Soft-off latch cleared by user power button; continuing normal boot");
        }
        return;
    case BUTTON_SHUTDOWN_BOOT_KEEP_SOFT_OFF:
        ESP_LOGW(TAG, "Soft-off latch active without user power button; staying in charging/off sleep");
        (void)bsp_system_shutdown();
        shutdown_sequence_low_power_fallback(NULL);
        return;
    case BUTTON_SHUTDOWN_BOOT_CONTINUE:
    default:
        return;
    }
}

static void init_runtime_inputs_and_restart_path(void) {
    int input_ret;
    bool knob_ready;
    bool touch_ready;

    input_ret = hal_display_input_init();
    knob_ready = hal_display_has_knob_input();
    touch_ready = hal_display_has_touch_input();

    ESP_LOGW(TAG, "Display input init result=%d knob_ready=%d touch_ready=%d", input_ret, knob_ready ? 1 : 0,
             touch_ready ? 1 : 0);

    if (input_ret != 0) {
        ESP_LOGW(TAG, "Delayed display input initialization failed; continuing with degraded input availability");
    }

    if (s_runtime_button_callbacks_registered || s_power_button_fallback_task != NULL) {
        ESP_LOGI(TAG, "Button long-press owner already initialized: callback=%d fallback=%d",
                 s_runtime_button_callbacks_registered ? 1 : 0, s_power_button_fallback_task != NULL ? 1 : 0);
        return;
    }

    if (knob_ready) {
        const esp_err_t callback_ret =
            bsp_set_btn_long_press_ms_cb(BUTTON_SHUTDOWN_HOLD_MS, on_button_long_hold_source);
        if (callback_ret == ESP_OK) {
            ESP_LOGI(TAG, "Input router owns encoder short-click; BSP long-press callback owns shutdown");
            s_runtime_button_callbacks_registered = true;
            return;
        } else {
            ESP_LOGE(TAG, "Failed to register shutdown long-press callback: %s", esp_err_to_name(callback_ret));
            start_power_button_shutdown_fallback("BSP long-press callback registration failed");
        }
    } else {
        start_power_button_shutdown_fallback("LVGL knob input unavailable");
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

    if (s_mcu_runtime_services_initialized) {
        return;
    }

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
    s_mcu_runtime_services_initialized = true;
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    ensure_mcu_link_runtime_task_started();
#endif
}

static esp_err_t app_resource_start_mcu_runtime(const char *reason) {
    esp_err_t ret;

    if (s_mcu_runtime_active) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting MCU runtime for %s", reason != NULL ? reason : "app");
    init_mcu_link_bootstrap();
    if (mcu_link_bootstrap_get_link() == NULL) {
        ESP_LOGW(TAG, "MCU runtime start skipped; bootstrap unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    init_mcu_runtime_services();

    ret = hal_servo_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo facade init failed while starting MCU runtime: %s", esp_err_to_name(ret));
        mcu_link_bootstrap_stop();
        return ret;
    }

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    ret = control_ingress_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Control ingress init failed while starting MCU runtime: %s", esp_err_to_name(ret));
        mcu_link_bootstrap_stop();
        return ret;
    }
#endif

    s_mcu_runtime_active = true;
    s_mcu_obs_state_initialized = false;
    s_mcu_obs_stats_initialized = false;
    s_last_mcu_obs_stats_log_us = 0;
    s_startup_green_leds_applied = false;
    s_startup_green_leds_last_attempt_us = 0;
    ESP_LOGI(TAG, "MCU runtime active for %s", reason != NULL ? reason : "app");
    LOG_HEAP_STATE("after_mcu_runtime_start");
    return ESP_OK;
}

static void app_resource_stop_mcu_runtime(const char *reason) {
    if (!s_mcu_runtime_active && mcu_link_bootstrap_get_link() == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping MCU runtime for %s", reason != NULL ? reason : "app");
    mcu_link_bootstrap_stop();
    s_mcu_runtime_active = false;
    s_mcu_obs_state_initialized = false;
    s_mcu_obs_stats_initialized = false;
    s_last_mcu_obs_stats_log_us = 0;
    s_touch_fondle_press_latched = false;
    s_touch_fondle_latched_id = TOUCH_FONDLE_INVALID_ID;
    LOG_HEAP_STATE("after_mcu_runtime_stop");
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

    if (local_exit_is_visible()) {
        ESP_LOGI(TAG, "Touch press ignored while local exit overlay is visible touch_id=%u ts=%lu",
                 (unsigned)touch.touch_id, (unsigned long)touch.timestamp_ms);
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

    s_touch_fondle_press_latched = true;
    s_touch_fondle_latched_id = touch.touch_id;

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
        settings_update_stm32_git_ref_from_link(link);
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

    (void)transport_deinit_ws_stack("low memory recovery");
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
    int ws_start_result = ws_client_start();
    transport_resume_display_motion();
    if (ws_start_result != 0) {
        transport_stop_ws("ws start failed");
        (void)transport_deinit_ws_stack("ws start failed");
        transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ws start failed");
        return false;
    }

    s_consecutive_ws_start_defers = 0;
    transport_reset_low_memory_recovery(start_reason);
    transport_schedule_retry(0);
    transport_arm_ws_connect_watchdog();
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
    int ws_start_result = ws_client_start();
    transport_resume_display_motion();
    if (ws_start_result != 0) {
        transport_stop_ws("cached ws start failed");
        (void)transport_deinit_ws_stack("cached ws start failed");
        transport_clear_cached_ws_url("cached ws start failed");
        return false;
    }

    s_consecutive_ws_start_defers = 0;
    transport_reset_low_memory_recovery("cached ws start requested");
    transport_schedule_retry(0);
    transport_arm_ws_connect_watchdog();
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
    if (!s_mcu_runtime_active) {
        return false;
    }

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
    animation_snapshot_t snapshot;

    if (current_state == NULL || strcmp(current_state, "recharge") != 0) {
        return false;
    }

    if (animation_get_snapshot(&snapshot) != ANIMATION_SERVICE_OK) {
        return false;
    }
    return snapshot.active_ticket != ANIMATION_TICKET_INVALID || snapshot.preparing_ticket != ANIMATION_TICKET_INVALID;
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

static bool idle_hint_is_blocked(idle_hint_mode_t desired_hint) {
    if (behavior_state_is_action_active()) {
        return true;
    }

    if (!behavior_state_is_busy()) {
        return false;
    }

    return true;
}

static void reset_ready_idle_rotation(void) {
    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
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

static bool ready_idle_state_is_sleep_state(const char *state_id) {
    return state_id != NULL && (strcmp(state_id, "standby") == 0 || strcmp(state_id, "standby_entry") == 0 ||
                                strcmp(state_id, "standby_loop") == 0 || strcmp(state_id, "standby_start") == 0);
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
    if (!ready_idle_state_is_sleep_state(current_state)) {
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

    if (ready_idle_state_is_sleep_state(current_state)) {
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

static int choose_ready_idle_variant(void) {
    int available[READY_IDLE_VARIANT_COUNT] = {0};
    int available_count = collect_ready_idle_variants(available);
    int filtered[READY_IDLE_VARIANT_COUNT] = {0};
    int filtered_count = 0;
    if (available_count <= 0) {
        return -1;
    }

    for (int i = 0; i < available_count; ++i) {
        if (available_count > 1 && available[i] == s_ready_idle_last_variant_index) {
            continue;
        }
        filtered[filtered_count++] = available[i];
    }

    if (filtered_count <= 0) {
        return available[esp_random() % (uint32_t)available_count];
    }

    return filtered[esp_random() % (uint32_t)filtered_count];
}

static void voice_app_resume_wake_word_for_sleep_standby(const char *reason) {
#ifdef CONFIG_ENABLE_WAKE_WORD
    if (!active_app_is("voice.app")) {
        return;
    }

    ESP_LOGI(TAG, "Voice sleep standby requests wake word resume: %s", reason != NULL ? reason : "unspecified");
    voice_recorder_resume_wake_word_for_sleep();
#else
    (void)reason;
#endif
}

static bool apply_ready_idle_sleep(const idle_hint_view_t *view) {
    esp_err_t ret = behavior_state_set_with_resources("standby_start", view->text, view->font_size, NULL, "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle sleep transition: %s", esp_err_to_name(ret));
        s_ready_idle_next_switch_us = esp_timer_get_time() + (int64_t)READY_IDLE_FALLBACK_RETRY_MS * 1000LL;
        return false;
    }

    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_sleeping = true;
    voice_app_resume_wake_word_for_sleep_standby("ready idle sleep");
    ESP_LOGI(TAG, "Ready idle sleep transition applied after %ums standby timeout",
             (unsigned)READY_IDLE_STANDBY_TIMEOUT_MS);
    return true;
}

static bool apply_ready_idle_fallback_standby(const idle_hint_view_t *view, int64_t now_us) {
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, "standby_loop", "");
    schedule_ready_idle_retry(now_us);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle fallback standby: %s", esp_err_to_name(ret));
        return false;
    }

    mark_ready_idle_standby_transition_pending();
    voice_app_resume_wake_word_for_sleep_standby("ready idle fallback standby");
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
    voice_app_resume_wake_word_for_sleep_standby("ready idle text-only standby");
    return true;
}

static bool ready_idle_should_use_lightweight_ui(void) {
    return is_basic_app_active() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_READY;
}

static bool apply_ready_idle_lightweight_standby(const idle_hint_view_t *view) {
    const char *text = view->text;
    esp_err_t ret = behavior_state_set_with_resources("standby", text, view->font_size, "standby", "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply lightweight ready idle standby: %s", esp_err_to_name(ret));
        return false;
    }

    s_ready_idle_variant_index = -1;
    s_ready_idle_next_switch_us = 0;
    s_ready_idle_sleeping = true;
    s_ready_idle_memory_defers = 0;
    mark_ready_idle_standby_transition_pending();
    voice_app_resume_wake_word_for_sleep_standby("ready idle lightweight standby");
    ESP_LOGI(TAG, "Ready idle lightweight standby applied for app runtime");
    return true;
}

static void app_wifi_gate_reset(app_wifi_gate_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->view = APP_WIFI_GATE_VIEW_NONE;
    state->action = APP_CONNECT_ACTION_NONE;
}

static bool app_wifi_gate_is_ready(void) {
    return wifi_has_credentials() == 1 && wifi_is_connected() == 1;
}

static app_wifi_gate_view_t app_wifi_gate_select_view(void) {
    if (app_wifi_gate_is_ready()) {
        return APP_WIFI_GATE_VIEW_NONE;
    }

    if (wifi_has_credentials() != 1) {
        return APP_WIFI_GATE_VIEW_SETUP;
    }

    if (wifi_is_connected() != 1) {
        return s_wifi_failed_since_last_success ? APP_WIFI_GATE_VIEW_FAILED : APP_WIFI_GATE_VIEW_CONNECTING;
    }

    return APP_WIFI_GATE_VIEW_NONE;
}

static app_connect_action_t app_wifi_gate_action_for_view(app_wifi_gate_view_t view) {
    switch (view) {
    case APP_WIFI_GATE_VIEW_SETUP:
    case APP_WIFI_GATE_VIEW_CONNECTING:
    case APP_WIFI_GATE_VIEW_FAILED:
        return APP_CONNECT_ACTION_WIFI;
    case APP_WIFI_GATE_VIEW_NONE:
    default:
        return APP_CONNECT_ACTION_NONE;
    }
}

static void app_wifi_gate_fill_status(app_wifi_gate_view_t view, const char *app_label, const char **title,
                                      char *detail, size_t detail_size, const char **action, bool *show_spinner,
                                      bool *alert, app_connect_action_t *connect_action) {
    const char *safe_label = (app_label != NULL && app_label[0] != '\0') ? app_label : "this app";

    if (title == NULL || detail == NULL || detail_size == 0 || action == NULL || show_spinner == NULL ||
        alert == NULL || connect_action == NULL) {
        return;
    }

    *title = "Connecting Wi-Fi";
    (void)snprintf(detail, detail_size, "Waiting for network before %s", safe_label);
    *action = "";
    *show_spinner = true;
    *alert = false;
    *connect_action = app_wifi_gate_action_for_view(view);

    switch (view) {
    case APP_WIFI_GATE_VIEW_SETUP:
        *title = "Wi-Fi required";
        (void)snprintf(detail, detail_size, "Configure Wi-Fi to start %s", safe_label);
        *action = "Button: Open Wi-Fi";
        *show_spinner = false;
        *alert = true;
        break;

    case APP_WIFI_GATE_VIEW_FAILED:
        *title = "Check Wi-Fi";
        (void)snprintf(detail, detail_size, "Saved Wi-Fi is not connected");
        *action = "Button: Open Wi-Fi";
        *show_spinner = false;
        *alert = true;
        break;

    case APP_WIFI_GATE_VIEW_CONNECTING:
        *action = "Button: Open Wi-Fi";
        break;

    case APP_WIFI_GATE_VIEW_NONE:
    default:
        break;
    }
}

static bool app_wifi_gate_show(app_wifi_gate_state_t *state, const char *app_label, const char *owner) {
    app_wifi_gate_view_t view = app_wifi_gate_select_view();
    app_connect_action_t connect_action = app_wifi_gate_action_for_view(view);
    const char *title = NULL;
    const char *action = NULL;
    char detail[96];
    bool show_spinner = true;
    bool alert = false;

    if (view == APP_WIFI_GATE_VIEW_NONE) {
        if (state != NULL && state->view != APP_WIFI_GATE_VIEW_NONE) {
            hal_display_voice_connect_status_clear();
            app_wifi_gate_reset(state);
        }
        return false;
    }

    if (state != NULL && state->view == view && state->action == connect_action) {
        return true;
    }

    app_wifi_gate_fill_status(view, app_label, &title, detail, sizeof(detail), &action, &show_spinner, &alert,
                              &connect_action);
    if (hal_display_voice_connect_status_set(title, detail, action, show_spinner, alert) == 0) {
        if (state != NULL) {
            state->view = view;
            state->action = connect_action;
        }
        ESP_LOGI(TAG, "%s Wi-Fi gate view=%d action=%d", owner != NULL ? owner : "App", (int)view, (int)connect_action);
    }
    return true;
}

static void app_wifi_gate_clear(app_wifi_gate_state_t *state) {
    hal_display_voice_connect_status_clear();
    app_wifi_gate_reset(state);
}

static bool app_wifi_gate_handle_action(app_wifi_gate_state_t *state, const char *reason) {
    app_connect_action_t action = state != NULL ? state->action : APP_CONNECT_ACTION_NONE;

    if (action == APP_CONNECT_ACTION_NONE) {
        action = app_wifi_gate_action_for_view(app_wifi_gate_select_view());
    }

    if (action == APP_CONNECT_ACTION_WIFI) {
        ESP_LOGI(TAG, "Wi-Fi gate action requested: %s", reason != NULL ? reason : "none");
        settings_open_wifi_detail_from_app();
        return true;
    }

    return false;
}

static void voice_app_reset_connect_ui_state(void) {
    s_voice_connect_view = VOICE_CONNECT_VIEW_NONE;
    s_voice_connect_action = APP_CONNECT_ACTION_NONE;
    s_voice_connect_ws_wait_start_us = 0;
}

static void voice_app_enter_ws_failed(const char *reason, int64_t elapsed_ms) {
    const char *safe_reason = reason != NULL ? reason : "voice ws timeout";

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        return;
    }

    ESP_LOGW(TAG, "evt=voice_runtime stage=ws_failed reason=%s elapsed_ms=%lld", safe_reason, elapsed_ms);
    transport_abort_discovery_request(safe_reason);
    transport_stop_ws(safe_reason);
    if (s_ws_stack_ready) {
        (void)transport_deinit_ws_stack(safe_reason);
    }
    transport_reset_cached_ws_resume_state();
    transport_reset_ws_connect_watchdog();
    transport_clear_cached_ws_url(safe_reason);
    if (voice_recorder_close() != ESP_OK) {
        ESP_LOGW(TAG, "Voice recorder close timed out while entering WS failed state");
    }
    s_cloud_runtime_started = false;
    s_voice_runtime_stage = VOICE_RUNTIME_STAGE_WS_FAILED;
    s_voice_runtime_open_us = 0;
    s_voice_connect_ws_wait_start_us = 0;
    transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "voice ws failed waiting retry");
}

static bool voice_app_ws_connect_timed_out(void) {
    if (s_voice_runtime_open_us <= 0 || wifi_is_connected() != 1 || ws_client_is_session_ready()) {
        return false;
    }

    bool cloud_connect_pending = s_voice_runtime_stage == VOICE_RUNTIME_STAGE_PENDING ||
                                 s_voice_runtime_stage == VOICE_RUNTIME_STAGE_AUDIO_STARTING ||
                                 s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD;
    if (!cloud_connect_pending) {
        return false;
    }

    if (s_voice_connect_ws_wait_start_us <= 0) {
        s_voice_connect_ws_wait_start_us =
            s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD ? esp_timer_get_time() : s_voice_runtime_open_us;
    }

    int64_t elapsed_ms = (esp_timer_get_time() - s_voice_connect_ws_wait_start_us) / 1000LL;
    if (elapsed_ms < (int64_t)VOICE_CONNECT_UI_WS_TIMEOUT_MS) {
        return false;
    }

    voice_app_enter_ws_failed("voice ws connect timeout", elapsed_ms);
    return true;
}

static voice_connect_view_t voice_app_select_connect_view(void) {
    if (wifi_has_credentials() != 1) {
        s_voice_connect_ws_wait_start_us = 0;
        return VOICE_CONNECT_VIEW_WIFI_SETUP;
    }

    if (wifi_is_connected() != 1) {
        s_voice_connect_ws_wait_start_us = 0;
        return s_wifi_failed_since_last_success ? VOICE_CONNECT_VIEW_WIFI_FAILED : VOICE_CONNECT_VIEW_WIFI_CONNECTING;
    }

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        s_voice_connect_ws_wait_start_us = 0;
        return VOICE_CONNECT_VIEW_WS_FAILED;
    }

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_DEGRADED) {
        s_voice_connect_ws_wait_start_us = 0;
        return VOICE_CONNECT_VIEW_RUNTIME_DEGRADED;
    }

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        s_voice_connect_ws_wait_start_us = 0;
        return VOICE_CONNECT_VIEW_WS_FAILED;
    }

    if (s_voice_runtime_stage != VOICE_RUNTIME_STAGE_WAITING_CLOUD) {
        s_voice_connect_ws_wait_start_us = 0;
    }

    if (voice_app_ws_connect_timed_out()) {
        return VOICE_CONNECT_VIEW_WS_FAILED;
    }

    return VOICE_CONNECT_VIEW_WS_CONNECTING;
}

static void voice_app_apply_connect_view(voice_connect_view_t view) {
    const char *title = "Connecting WebSocket";
    const char *detail = "Preparing cloud voice session";
    const char *action = "";
    char wifi_detail[96] = {0};
    bool show_spinner = true;
    bool alert = false;
    app_connect_action_t connect_action = APP_CONNECT_ACTION_NONE;

    switch (view) {
    case VOICE_CONNECT_VIEW_WIFI_SETUP:
        app_wifi_gate_fill_status(APP_WIFI_GATE_VIEW_SETUP, "Desktop Link", &title, wifi_detail, sizeof(wifi_detail),
                                  &action, &show_spinner, &alert, &connect_action);
        detail = wifi_detail;
        break;

    case VOICE_CONNECT_VIEW_WIFI_CONNECTING:
        app_wifi_gate_fill_status(APP_WIFI_GATE_VIEW_CONNECTING, "Desktop Link", &title, wifi_detail,
                                  sizeof(wifi_detail), &action, &show_spinner, &alert, &connect_action);
        detail = wifi_detail;
        break;

    case VOICE_CONNECT_VIEW_WIFI_FAILED:
        app_wifi_gate_fill_status(APP_WIFI_GATE_VIEW_FAILED, "Desktop Link", &title, wifi_detail, sizeof(wifi_detail),
                                  &action, &show_spinner, &alert, &connect_action);
        detail = wifi_detail;
        break;

    case VOICE_CONNECT_VIEW_WS_FAILED:
        title = "WebSocket failed";
        detail = "Cloud did not become ready";
        action = "Button: Retry";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_RETRY;
        break;

    case VOICE_CONNECT_VIEW_RUNTIME_DEGRADED:
        title = "Desktop Link unavailable";
        detail = "Audio runtime failed to start";
        action = "Button: Retry";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_RETRY;
        break;

    case VOICE_CONNECT_VIEW_WS_CONNECTING:
    case VOICE_CONNECT_VIEW_NONE:
    default:
        break;
    }

    if (view == s_voice_connect_view && connect_action == s_voice_connect_action) {
        return;
    }

    if (hal_display_voice_connect_status_set(title, detail, action, show_spinner, alert) == 0) {
        s_voice_connect_view = view;
        s_voice_connect_action = connect_action;
        ESP_LOGI(TAG, "Desktop Link connect UI view=%d action=%d", (int)view, (int)connect_action);
    }
}

static void voice_app_update_connect_ui_if_needed(void) {
    if (s_local_return_to_launcher_pending || !active_app_is_client_voice_host() || !s_ui_ready ||
        s_voice_ready_ui_shown || s_voice_runtime_stage == VOICE_RUNTIME_STAGE_READY || local_exit_is_visible()) {
        return;
    }

    voice_app_apply_connect_view(voice_app_select_connect_view());
}

static void voice_app_retry_connect(const char *reason) {
    if (!active_app_is_client_voice_host()) {
        ESP_LOGI(TAG, "Desktop Link connect retry ignored; active app is not voice host");
        return;
    }

    voice_runtime_stage_t previous_stage = s_voice_runtime_stage;
    int64_t now_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Desktop Link connect retry requested: %s", reason != NULL ? reason : "none");
    voice_app_reset_connect_ui_state();
    s_voice_ready_ui_shown = false;
    s_voice_runtime_open_us = now_us;

    if (wifi_has_credentials() == 1 && wifi_is_connected() != 1) {
        s_wifi_failed_since_last_success = false;
        transport_schedule_retry(0);
        transport_begin_wifi_resume("voice connect retry");
        voice_app_update_connect_ui_if_needed();
        return;
    }

    if (wifi_has_credentials() == 1 && wifi_is_connected() == 1) {
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_WAITING_CLOUD;
        s_voice_connect_ws_wait_start_us = now_us;
        voice_app_apply_connect_view(VOICE_CONNECT_VIEW_WS_CONNECTING);
    }

    if (previous_stage == VOICE_RUNTIME_STAGE_DEGRADED || previous_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        if (previous_stage == VOICE_RUNTIME_STAGE_DEGRADED) {
            voice_recorder_close();
        }
        voice_recorder_init();
        s_cloud_runtime_started = false;
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_STOPPED;
        voice_runtime_request_start("voice connect retry");
    } else if (!s_cloud_runtime_started) {
        voice_runtime_request_start("voice connect retry");
    } else {
        s_voice_runtime_stage =
            ws_client_is_session_ready() ? VOICE_RUNTIME_STAGE_READY : VOICE_RUNTIME_STAGE_WAITING_CLOUD;
        s_voice_connect_ws_wait_start_us =
            s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD ? esp_timer_get_time() : 0;
    }

    transport_abort_discovery_request("voice connect retry");
    transport_stop_ws("voice connect retry");
    if (s_ws_stack_ready) {
        (void)transport_deinit_ws_stack("voice connect retry");
    }
    transport_reset_cached_ws_resume_state();
    transport_clear_cached_ws_url("voice connect retry");
    transport_reset_discovery_retry_backoff("voice connect retry");
    transport_reset_ws_connect_watchdog();
    transport_clear_cached_ws_url("voice connect retry");
    transport_reset_discovery_retry_backoff("voice connect retry");
    transport_schedule_retry(0);
    ensure_cloud_runtime_started();
    if (wifi_has_credentials() == 1 && wifi_is_connected() == 1 && !ws_client_is_session_ready()) {
        voice_app_apply_connect_view(VOICE_CONNECT_VIEW_WS_CONNECTING);
    }
    voice_app_update_connect_ui_if_needed();
}

static bool voice_app_handle_connect_action(const char *reason) {
    ESP_LOGI(TAG, "Desktop Link connect action requested: %s view=%d action=%d", reason != NULL ? reason : "none",
             (int)s_voice_connect_view, (int)s_voice_connect_action);

    if (s_voice_connect_action == APP_CONNECT_ACTION_RETRY) {
        voice_app_retry_connect(reason);
        return true;
    }

    if (s_voice_connect_action == APP_CONNECT_ACTION_WIFI) {
        settings_open_wifi_detail_from_app();
        return true;
    }

    return false;
}

static void app_connect_action_clicked(void *user_ctx) {
    (void)user_ctx;
    ESP_LOGI(TAG, "App connect action click queued");
    s_app_connect_action_click_pending = true;
}

static void app_connect_process_action_click(void) {
    if (!s_app_connect_action_click_pending) {
        return;
    }

    s_app_connect_action_click_pending = false;
    const watcher_app_t *active_app = watcher_app_get_active();
    ESP_LOGI(TAG, "App connect action dispatch active=%s",
             active_app != NULL && active_app->id != NULL ? active_app->id : "none");
    if (active_app_is_client_voice_host()) {
        (void)voice_app_handle_connect_action("touch");
        return;
    }

    if (active_app_is("app.center")) {
        app_center_process_connect_action_click();
    }
}

static bool voice_app_enter_ready_ui_if_needed(const char *reason) {
    esp_err_t ret;

    if (s_local_return_to_launcher_pending || !active_app_is_client_voice_host() || !s_ui_ready ||
        s_voice_ready_ui_shown) {
        return false;
    }
    if (s_voice_runtime_stage != VOICE_RUNTIME_STAGE_READY) {
        return false;
    }

    if (app_animation_ui_init_with_text("") != 0) {
        ESP_LOGE(TAG, "Voice ready UI cannot start without an animation surface");
        return false;
    }

    hal_display_voice_connect_status_clear();
    ret = behavior_state_set_with_resources("standby_entry", "", 0, NULL, "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enter voice ready UI: %s", esp_err_to_name(ret));
        return false;
    }

    (void)hal_display_set_text_with_style("", 0, false);
    s_voice_ready_ui_shown = true;
    voice_app_reset_connect_ui_state();
    reset_ready_idle_rotation();
    s_ready_idle_sleeping = true;
    mark_ready_idle_standby_transition_pending();
    voice_app_resume_wake_word_for_sleep_standby("voice ready standby entry");
    ESP_LOGI(TAG, "Voice ready UI entered: reason=%s", reason != NULL ? reason : "none");
    return true;
}

static bool apply_ready_idle_variant_if_due(const idle_hint_view_t *view, bool force) {
    int64_t now_us = esp_timer_get_time();

    if (ready_idle_should_use_lightweight_ui()) {
        if (s_ready_idle_sleeping) {
            return true;
        }
        return apply_ready_idle_lightweight_standby(view);
    }

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

    if (s_ready_idle_variant_index >= 0) {
        if (now_us < s_ready_idle_next_switch_us) {
            return true;
        }
        return apply_ready_idle_sleep(view);
    }

    int selected = choose_ready_idle_variant();
    if (selected < 0) {
        return apply_ready_idle_fallback_standby(view, now_us);
    }

    const char *anim_id = ready_idle_variant_name(selected);
    (void)animation_prefetch_hint(ready_idle_variant_type(selected));
    esp_err_t ret = behavior_state_set_with_resources("standby", view->text, view->font_size, anim_id, "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply ready idle variant %s: %s", anim_id, esp_err_to_name(ret));
        schedule_ready_idle_retry(now_us);
        return false;
    }

    s_ready_idle_variant_index = selected;
    s_ready_idle_last_variant_index = selected;
    s_ready_idle_next_switch_us = now_us + (int64_t)READY_IDLE_STANDBY_TIMEOUT_MS * 1000LL;
    mark_ready_idle_standby_transition_pending();
    ESP_LOGI(TAG, "Ready idle variant applied: %s timeout=%ums", anim_id, (unsigned)READY_IDLE_STANDBY_TIMEOUT_MS);
    return true;
}

static void apply_idle_hint_if_needed(void) {
    bool client_voice_ready = active_app_is_client_voice_host() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_READY;

    if (local_exit_is_visible()) {
        return;
    }

    if (client_voice_ready) {
        (void)voice_app_enter_ready_ui_if_needed("idle hint ready");
    }

    if (active_app_is_client_voice_host() && !client_voice_ready) {
        voice_app_update_connect_ui_if_needed();
        return;
    }

    static idle_hint_mode_t s_last_applied_hint = IDLE_HINT_READY;
    static bool s_hint_initialized = false;
    idle_hint_mode_t desired_hint = client_voice_ready ? IDLE_HINT_READY : get_idle_hint_mode();
    idle_hint_view_t view = get_idle_hint_view(desired_hint);
    if (client_voice_ready) {
        view.text = "";
        view.font_size = 0;
        view.alert = false;
    }

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

        bool cycle_active = s_ready_idle_variant_index >= 0 || s_ready_idle_sleeping;
        if (cycle_active && current_state != NULL && !ready_idle_state_is_sleep_state(current_state)) {
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

    if (!active_app_is("ble.app") && !active_app_is("provision.app")) {
        s_ble_connected_feedback_pending = false;
        return;
    }

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

    ret = behavior_state_set_with_resources_and_action(BLE_CONNECTED_FEEDBACK_STATE_ID, "BLE connected", 0,
                                                       BLE_CONNECTED_FEEDBACK_ANIM_ID, BLE_CONNECTED_FEEDBACK_SOUND_ID,
                                                       BLE_CONNECTED_FEEDBACK_ACTION_ID);
    if (ret == ESP_OK) {
        s_ble_connected_feedback_pending = false;
        if (active_app_is("provision.app")) {
            (void)behavior_state_set_text_style("Waiting WiFi", 0, false);
            (void)hal_display_set_text_with_style("Waiting WiFi", 0, false);
        }
        ESP_LOGI(TAG, "Triggered BLE connected feedback state");
    } else {
        ESP_LOGW(TAG, "Failed to trigger BLE connected feedback state: %s", esp_err_to_name(ret));
    }
}

static const char *voice_runtime_stage_to_string(voice_runtime_stage_t stage) {
    switch (stage) {
    case VOICE_RUNTIME_STAGE_PENDING:
        return "voice_pending";
    case VOICE_RUNTIME_STAGE_AUDIO_STARTING:
        return "voice_audio_starting";
    case VOICE_RUNTIME_STAGE_WAITING_CLOUD:
        return "voice_waiting_cloud";
    case VOICE_RUNTIME_STAGE_WS_FAILED:
        return "voice_ws_failed";
    case VOICE_RUNTIME_STAGE_READY:
        return "voice_ready";
    case VOICE_RUNTIME_STAGE_DEGRADED:
        return "voice_degraded";
    case VOICE_RUNTIME_STAGE_STOPPED:
    default:
        return "voice_stopped";
    }
}

static void voice_runtime_request_start(const char *reason) {
    int64_t now_us = esp_timer_get_time();

    if (s_cloud_runtime_started) {
        s_voice_runtime_stage =
            ws_client_is_session_ready() ? VOICE_RUNTIME_STAGE_READY : VOICE_RUNTIME_STAGE_WAITING_CLOUD;
        s_voice_connect_ws_wait_start_us =
            s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD ? esp_timer_get_time() : 0;
        return;
    }

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_PENDING ||
        s_voice_runtime_stage == VOICE_RUNTIME_STAGE_AUDIO_STARTING ||
        s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD) {
        return;
    }

    s_voice_runtime_open_us = now_us;
    s_voice_runtime_stage = VOICE_RUNTIME_STAGE_PENDING;
    ESP_LOGI(TAG, "evt=voice_runtime stage=pending reason=%s voice_open_ui_ms=0", reason != NULL ? reason : "none");
}

static void voice_runtime_reset(const char *reason) {
    if (s_voice_runtime_stage != VOICE_RUNTIME_STAGE_STOPPED || s_cloud_runtime_started) {
        ESP_LOGI(TAG, "evt=voice_runtime stage=stopped reason=%s previous=%s", reason != NULL ? reason : "none",
                 voice_runtime_stage_to_string(s_voice_runtime_stage));
    }
    s_voice_runtime_stage = VOICE_RUNTIME_STAGE_STOPPED;
    s_voice_runtime_open_us = 0;
    s_cloud_runtime_started = false;
    s_voice_ready_ui_shown = false;
    voice_app_reset_connect_ui_state();
}

static void voice_runtime_start_if_due(void) {
    int64_t now_us;
    int64_t start_us;
    int64_t wake_init_ms;
    int64_t voice_open_ui_ms;

    if (s_voice_runtime_stage != VOICE_RUNTIME_STAGE_PENDING) {
        return;
    }
    if (!s_boot_completed || s_local_return_to_launcher_pending || !active_app_is_voice_runtime_context()) {
        return;
    }
    if (wifi_has_credentials() != 1 || wifi_is_connected() != 1) {
        return;
    }

    now_us = esp_timer_get_time();
    if (s_voice_runtime_open_us > 0 &&
        (now_us - s_voice_runtime_open_us) < ((int64_t)VOICE_RUNTIME_START_DEFER_MS * 1000LL)) {
        return;
    }

    if (!transport_has_cloud_runtime_headroom()) {
        return;
    }

    voice_open_ui_ms = s_voice_runtime_open_us > 0 ? (now_us - s_voice_runtime_open_us) / 1000LL : 0;
    s_voice_runtime_stage = VOICE_RUNTIME_STAGE_AUDIO_STARTING;
    ESP_LOGI(TAG, "evt=voice_runtime stage=audio_starting voice_open_ui_ms=%lld", voice_open_ui_ms);

    start_us = esp_timer_get_time();
    if (voice_recorder_start() != 0) {
        wake_init_ms = (esp_timer_get_time() - start_us) / 1000LL;
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_DEGRADED;
        s_cloud_runtime_started = false;
        ESP_LOGW(TAG, "evt=voice_runtime stage=degraded wake_init_ms=%lld reason=start_failed", wake_init_ms);
        return;
    }

    wake_init_ms = (esp_timer_get_time() - start_us) / 1000LL;
    s_cloud_runtime_started = true;
    LOG_HEAP_STATE("after_voice_runtime_start");
    if (ws_client_is_session_ready()) {
        int64_t voice_ready_ms =
            s_voice_runtime_open_us > 0 ? (esp_timer_get_time() - s_voice_runtime_open_us) / 1000LL : 0;
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_READY;
        s_voice_connect_ws_wait_start_us = 0;
        ESP_LOGI(TAG, "evt=voice_runtime stage=ready wake_init_ms=%lld voice_ready_ms=%lld", wake_init_ms,
                 voice_ready_ms);
    } else {
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_WAITING_CLOUD;
        s_voice_connect_ws_wait_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "evt=voice_runtime stage=waiting_cloud wake_init_ms=%lld", wake_init_ms);
    }
}

static void voice_diag_schedule_stable_samples(void) {
    int64_t now_us = esp_timer_get_time();

    s_voice_diag_stable_1s_due_us = now_us + 1000000LL;
    s_voice_diag_stable_3s_due_us = now_us + 3000000LL;
}

static void voice_diag_cancel_stable_samples(void) {
    s_voice_diag_stable_1s_due_us = 0;
    s_voice_diag_stable_3s_due_us = 0;
}

static void voice_diag_log_stable_if_due(void) {
    int64_t now_us = esp_timer_get_time();

    if (s_voice_diag_stable_1s_due_us > 0 && now_us >= s_voice_diag_stable_1s_due_us) {
        s_voice_diag_stable_1s_due_us = 0;
        app_ui_diag_log_snapshot("voice_stable_1s");
    }
    if (s_voice_diag_stable_3s_due_us > 0 && now_us >= s_voice_diag_stable_3s_due_us) {
        s_voice_diag_stable_3s_due_us = 0;
        app_ui_diag_log_snapshot("voice_stable_3s");
    }
}

static void voice_runtime_tick(void) {
    if (!active_app_is_voice_runtime_context()) {
        return;
    }

    voice_runtime_start_if_due();

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD && ws_client_is_session_ready()) {
        int64_t voice_ready_ms =
            s_voice_runtime_open_us > 0 ? (esp_timer_get_time() - s_voice_runtime_open_us) / 1000LL : 0;
        s_voice_runtime_stage = VOICE_RUNTIME_STAGE_READY;
        s_voice_connect_ws_wait_start_us = 0;
        ESP_LOGI(TAG, "evt=voice_runtime stage=ready voice_ready_ms=%lld", voice_ready_ms);
    }

    if (s_voice_runtime_stage == VOICE_RUNTIME_STAGE_READY) {
        (void)voice_app_enter_ready_ui_if_needed("voice runtime ready");
    } else {
        voice_app_update_connect_ui_if_needed();
    }

    if (active_app_is_client_voice_host()) {
        voice_diag_log_stable_if_due();
    }
}

static void ensure_cloud_runtime_started(void) {
    if (active_app_is_client_voice_host() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        return;
    }

    if (!s_boot_completed || s_cloud_runtime_started) {
        if (s_cloud_runtime_started && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WAITING_CLOUD &&
            ws_client_is_session_ready()) {
            s_voice_runtime_stage = VOICE_RUNTIME_STAGE_READY;
        }
        return;
    }

    if (active_app_is_client_voice_host() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        return;
    }

    if (!active_app_is_voice_runtime_context()) {
        return;
    }

    if (!transport_has_cloud_runtime_headroom()) {
        return;
    }

    voice_runtime_request_start("cloud ready");
}

static void transport_stop_ws(const char *reason) {
    if (ws_client_is_started() || ws_client_is_connected() || ws_client_is_session_ready()) {
        ESP_LOGI(TAG, "Stopping WebSocket transport (%s)", reason ? reason : "no reason");
        ws_client_stop();
    }
    transport_reset_ws_connect_watchdog();
}

static void transport_stop_ws_for_resource_release(const char *reason) {
    if (ws_client_is_started() || ws_client_is_connected() || ws_client_is_session_ready()) {
        ESP_LOGI(TAG, "Stopping WebSocket transport for resource release (%s)", reason ? reason : "no reason");
        ws_client_stop_for_resource_release();
    }
    transport_reset_ws_connect_watchdog();
}

static esp_err_t transport_deinit_ws_stack(const char *reason) {
    esp_err_t ret;

    if (!s_ws_stack_ready) {
        return ESP_OK;
    }

    ret = ws_client_deinit();
    if (ret == ESP_OK) {
        s_ws_stack_ready = false;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WebSocket stack deinit incomplete (%s): %s", reason ? reason : "no reason", esp_err_to_name(ret));
    return ret;
}

static void transport_recreate_ws_after_timeout(const char *reason, int64_t elapsed_ms) {
    ESP_LOGW(TAG, "WebSocket session did not become ready after %lld ms; rebuilding client (%s)", elapsed_ms,
             reason ? reason : "no reason");
    transport_stop_ws(reason);
    (void)transport_deinit_ws_stack(reason);
    transport_reset_ws_connect_watchdog();
    s_cached_ws_connect_inflight = false;
    s_cached_ws_connect_started_us = 0;
    transport_schedule_retry(0);
    transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, reason);
}

static esp_err_t app_resource_start_ble(void) {
    esp_err_t ret;

    if (!s_ble_stack_initialized) {
        ret = ble_service_init();
        if (ret != ESP_OK) {
            return ret;
        }
        ble_service_register_connection_callback(on_ble_connection_changed);
        ble_service_register_wifi_config_callback(settings_on_ble_wifi_configured);
        s_ble_stack_initialized = true;
        log_ble_mac_at_boot("resource_ble");
    } else {
        ble_service_register_wifi_config_callback(settings_on_ble_wifi_configured);
    }

    ret = ble_service_start_advertising();
    if (ret == ESP_ERR_INVALID_STATE && ble_service_is_connected()) {
        return ESP_OK;
    }
    return ret;
}

static esp_err_t app_resource_stop_ble(void) {
    esp_err_t ret;
    esp_err_t status = ESP_OK;
    int64_t stop_started_us = esp_timer_get_time();

    if (!s_ble_stack_initialized) {
        return ESP_OK;
    }

    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;

    ret = ble_service_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to disconnect BLE client: %s", esp_err_to_name(ret));
        if (status == ESP_OK) {
            status = ret;
        }
    }

    ret = ble_service_stop_advertising();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to stop BLE advertising: %s", esp_err_to_name(ret));
        if (status == ESP_OK) {
            status = ret;
        }
    }

    ret = ble_service_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to deinit BLE service: %s", esp_err_to_name(ret));
        if (status == ESP_OK) {
            status = ret;
        }
    }
    s_ble_stack_initialized = false;
    s_ble_advertising_paused_for_recovery = false;
    ESP_LOGI(TAG, "evt=app_resource stage=ble_stop ble_stop_ms=%lld result=%s",
             (esp_timer_get_time() - stop_started_us) / 1000LL, esp_err_to_name(status));
    return ESP_OK;
}

static void ble_provisioning_schedule_release(const char *reason) {
    if (!s_ble_stack_initialized) {
        return;
    }

    s_ble_provisioning_release_due_us = esp_timer_get_time() + ((int64_t)BLE_PROVISIONING_RELEASE_DELAY_MS * 1000LL);
    s_ble_provisioning_release_pending = true;
    ESP_LOGI(TAG, "BLE provisioning release scheduled (%s) delay_ms=%d", reason != NULL ? reason : "no reason",
             BLE_PROVISIONING_RELEASE_DELAY_MS);
}

static void ble_provisioning_release_tick(void) {
    if (!s_ble_provisioning_release_pending) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_ble_provisioning_release_due_us > 0 && now_us < s_ble_provisioning_release_due_us) {
        return;
    }

    s_ble_provisioning_release_pending = false;
    s_ble_provisioning_release_due_us = 0;
    if (!s_ble_stack_initialized) {
        return;
    }

    ESP_LOGI(TAG, "BLE provisioning release starting after Wi-Fi saved");
    esp_err_t ret = app_resource_stop_ble();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE provisioning release failed: %s", esp_err_to_name(ret));
    }
    settings_request_state_refresh();
}

static esp_err_t app_resource_stop_cloud(const char *reason) {
    esp_err_t status = ESP_OK;

    transport_cancel_discovery(reason);
    if (!transport_wait_for_discovery_idle(CLOUD_DISCOVERY_CANCEL_WAIT_MS)) {
        ESP_LOGW(TAG, "Discovery task still exiting while stopping cloud (%s)", reason ? reason : "no reason");
        status = ESP_ERR_TIMEOUT;
    }
    transport_stop_ws_for_resource_release(reason);
    if (s_ws_stack_ready) {
        esp_err_t ret = transport_deinit_ws_stack(reason);
        if (ret != ESP_OK && status == ESP_OK) {
            status = ret;
        }
    }
    voice_recorder_suspend_cloud_audio();
    voice_runtime_reset(reason);
    transport_reset_cached_ws_resume_state();
    return status;
}

static const char *app_resource_mode_to_string(watcher_app_resource_mode_t mode) {
    switch (mode) {
    case WATCHER_APP_RESOURCE_BLE_ONLY:
        return "ble";
    case WATCHER_APP_RESOURCE_WIFI_ONLY:
        return "wifi";
    case WATCHER_APP_RESOURCE_PROVISIONING:
        return "provisioning";
    case WATCHER_APP_RESOURCE_OFF:
    default:
        return "off";
    }
}

static void app_resource_set_to_string(watcher_app_resource_set_t resources, char *buffer, size_t buffer_size) {
    static const struct {
        watcher_app_resource_set_t bit;
        const char *name;
    } labels[] = {
        {WATCHER_APP_RESOURCE_SET_WIFI_STA, "wifi"},
        {WATCHER_APP_RESOURCE_SET_BLE, "ble"},
        {WATCHER_APP_RESOURCE_SET_PROVISIONING, "provisioning"},
        {WATCHER_APP_RESOURCE_SET_CLOUD, "cloud"},
        {WATCHER_APP_RESOURCE_SET_AUDIO, "audio"},
        {WATCHER_APP_RESOURCE_SET_MCU_RUNTIME, "mcu"},
        {WATCHER_APP_RESOURCE_SET_APP_CENTER, "app-center"},
    };
    size_t used = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    for (size_t index = 0; index < sizeof(labels) / sizeof(labels[0]); ++index) {
        int written;

        if ((resources & labels[index].bit) == 0 || used >= buffer_size - 1U) {
            continue;
        }
        written = snprintf(buffer + used, buffer_size - used, "%s%s", used > 0 ? "+" : "", labels[index].name);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            buffer[buffer_size - 1U] = '\0';
            return;
        }
        used += (size_t)written;
    }
    if (used == 0) {
        (void)snprintf(buffer, buffer_size, "none");
    }
}

static const char *app_resource_owner_to_string(void) {
    return s_app_resource_owner[0] != '\0' ? s_app_resource_owner : "none";
}

static esp_err_t app_resource_resume_wifi_if_needed(const char *app_id) {
    wifi_status_t wifi_status;
    const bool settings_app = app_id != NULL && strcmp(app_id, "settings.app") == 0;

    if (wifi_has_credentials() != 1) {
        ESP_LOGW(TAG, "Wi-Fi resource requested without saved credentials (%s)", app_id != NULL ? app_id : "(unknown)");
        return ESP_OK;
    }

    if (s_boot_saved_wifi_unavailable && !settings_app) {
        ESP_LOGI(TAG, "Wi-Fi resource resume skipped for %s because boot saved Wi-Fi timed out",
                 app_id != NULL ? app_id : "(unknown)");
        return ESP_OK;
    }

    wifi_status = wifi_get_status();
    if (wifi_is_connected() == 1 || wifi_is_connect_requested() == 1 || wifi_status == WIFI_STATUS_CONNECTED ||
        wifi_status == WIFI_STATUS_CONNECTING) {
        return ESP_OK;
    }

    if (wifi_resume_background() != 0) {
        ESP_LOGW(TAG, "Wi-Fi resource resume failed for %s", app_id != NULL ? app_id : "(unknown)");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void app_resource_set_owner(watcher_app_resource_mode_t mode, const char *app_id) {
    const char *owner = (mode == WATCHER_APP_RESOURCE_OFF || app_id == NULL || app_id[0] == '\0') ? "none" : app_id;
    (void)snprintf(s_app_resource_owner, sizeof(s_app_resource_owner), "%s", owner);
}

static void mem_monitor_fill_app_context(char *app_buf, size_t app_buf_size, char *resource_buf,
                                         size_t resource_buf_size) {
    const watcher_app_t *active_app = watcher_app_get_active();
    const char *app_id = (active_app != NULL && active_app->id != NULL) ? active_app->id : "none";
    char resource[80];
    const bool voice_context =
        active_app != NULL && active_app->id != NULL &&
        (app_id_is_client_voice_host(active_app->id) || strcmp(active_app->id, BASIC_APP_ID) == 0);

    app_resource_set_to_string(s_app_resource_set, resource, sizeof(resource));

    if (app_buf != NULL && app_buf_size > 0) {
        (void)snprintf(app_buf, app_buf_size, "%s", app_id);
    }
    if (resource_buf != NULL && resource_buf_size > 0) {
        if (voice_context) {
            (void)snprintf(resource_buf, resource_buf_size, "%s/%s", resource,
                           voice_runtime_stage_to_string(s_voice_runtime_stage));
        } else {
            (void)snprintf(resource_buf, resource_buf_size, "%s", resource);
        }
    }
}

#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
static void debug_app_control_init(void) {
    if (s_debug_app_command_queue == NULL) {
        s_debug_app_command_queue = xQueueCreate(DEBUG_APP_COMMAND_QUEUE_DEPTH, sizeof(debug_app_command_t));
    }

    if (s_debug_app_command_queue == NULL) {
        ESP_LOGW(TAG, "Debug app command queue allocation failed");
        return;
    }

    debug_cli_set_app_control_callbacks(debug_app_enqueue_open, debug_app_enqueue_close, debug_app_enqueue_status,
                                        debug_app_enqueue_connect);
    debug_cli_set_settings_callbacks(debug_settings_open_page, debug_settings_focus_target, debug_settings_rotate,
                                     debug_settings_click, debug_settings_status);
}

static esp_err_t debug_app_enqueue_command(const debug_app_command_t *command) {
    if (s_debug_app_command_queue == NULL || command == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_debug_app_command_queue, command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t debug_app_enqueue_open(const char *app_id) {
    debug_app_command_t command = {
        .type = DEBUG_APP_COMMAND_OPEN,
    };

    if (app_id == NULL || app_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    (void)snprintf(command.app_id, sizeof(command.app_id), "%s", app_id);
    return debug_app_enqueue_command(&command);
}

static esp_err_t debug_app_enqueue_close(void) {
    const debug_app_command_t command = {
        .type = DEBUG_APP_COMMAND_CLOSE,
    };
    return debug_app_enqueue_command(&command);
}

static esp_err_t debug_app_enqueue_status(void) {
    const debug_app_command_t command = {
        .type = DEBUG_APP_COMMAND_STATUS,
    };
    return debug_app_enqueue_command(&command);
}

static esp_err_t debug_app_enqueue_connect(void) {
    const debug_app_command_t command = {
        .type = DEBUG_APP_COMMAND_CONNECT,
    };
    return debug_app_enqueue_command(&command);
}

static esp_err_t debug_settings_open_page(const char *page_id) {
    const char *current;
    const char *focus;
    bool ok;

    if (page_id == NULL || page_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!active_app_is("settings.app")) {
        ESP_LOGW(TAG, "debug.settings.open rejected because active app is not settings.app");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_lock(0);
    ok = factory_settings_ui_debug_open_page(page_id);
    current = factory_settings_ui_debug_page_name();
    focus = factory_settings_ui_debug_focus_name();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "evt=debug_settings stage=open page=%s current=%s focus=%s result=%s", page_id, current, focus,
             ok ? esp_err_to_name(ESP_OK) : esp_err_to_name(ESP_ERR_NOT_FOUND));
    LOG_HEAP_STATE("debug_settings_open");
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t debug_settings_focus_target(const char *target_id) {
    const char *current;
    const char *focus;
    bool ok;

    if (target_id == NULL || target_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!active_app_is("settings.app")) {
        ESP_LOGW(TAG, "debug.settings.focus rejected because active app is not settings.app");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_lock(0);
    ok = factory_settings_ui_debug_focus(target_id);
    current = factory_settings_ui_debug_page_name();
    focus = factory_settings_ui_debug_focus_name();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "evt=debug_settings stage=focus target=%s current=%s focus=%s result=%s", target_id, current, focus,
             ok ? esp_err_to_name(ESP_OK) : esp_err_to_name(ESP_ERR_NOT_FOUND));
    LOG_HEAP_STATE("debug_settings_focus");
    return ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t debug_settings_rotate(int steps) {
    const char *current;
    const char *focus;
    bool ok;

    if (!active_app_is("settings.app")) {
        ESP_LOGW(TAG, "debug.settings.rotate rejected because active app is not settings.app");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_lock(0);
    ok = factory_settings_ui_debug_rotate(steps);
    current = factory_settings_ui_debug_page_name();
    focus = factory_settings_ui_debug_focus_name();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "evt=debug_settings stage=rotate steps=%d current=%s focus=%s result=%s", steps, current, focus,
             ok ? esp_err_to_name(ESP_OK) : esp_err_to_name(ESP_ERR_INVALID_STATE));
    LOG_HEAP_STATE("debug_settings_rotate");
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t debug_settings_click(void) {
    const char *current;
    const char *focus;
    bool ok;

    if (!active_app_is("settings.app")) {
        ESP_LOGW(TAG, "debug.settings.click rejected because active app is not settings.app");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_lock(0);
    ok = factory_settings_ui_debug_click();
    current = factory_settings_ui_debug_page_name();
    focus = factory_settings_ui_debug_focus_name();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "evt=debug_settings stage=click current=%s focus=%s result=%s", current, focus,
             ok ? esp_err_to_name(ESP_OK) : esp_err_to_name(ESP_ERR_INVALID_STATE));
    LOG_HEAP_STATE("debug_settings_click");
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t debug_settings_status(void) {
    const char *current;
    const char *focus;

    if (!active_app_is("settings.app")) {
        ESP_LOGW(TAG, "debug.settings.status rejected because active app is not settings.app");
        return ESP_ERR_INVALID_STATE;
    }
    lvgl_port_lock(0);
    current = factory_settings_ui_debug_page_name();
    focus = factory_settings_ui_debug_focus_name();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "evt=debug_settings stage=status current=%s focus=%s result=%s", current, focus,
             esp_err_to_name(ESP_OK));
    LOG_HEAP_STATE("debug_settings_status");
    return ESP_OK;
}

static void debug_app_control_log_status(const char *stage, esp_err_t result) {
    const watcher_app_t *active_app = watcher_app_get_active();
    ESP_LOGI(TAG, "evt=debug_app stage=%s result=%s active=%s resource=%s owner=%s", stage != NULL ? stage : "status",
             esp_err_to_name(result), (active_app != NULL && active_app->id != NULL) ? active_app->id : "none",
             app_resource_mode_to_string(s_app_resource_mode), app_resource_owner_to_string());
    LOG_HEAP_STATE(stage);
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    if (active_app != NULL && active_app->id != NULL && strcmp(active_app->id, "app.center") == 0) {
        app_center_debug_log_ui_snapshot(stage);
    }
#endif
}

static void debug_app_control_tick(void) {
    debug_app_command_t command;

    if (s_debug_app_command_queue == NULL) {
        return;
    }

    while (xQueueReceive(s_debug_app_command_queue, &command, 0) == pdTRUE) {
        esp_err_t ret = ESP_OK;

        switch (command.type) {
        case DEBUG_APP_COMMAND_OPEN:
            ret = watcher_app_open(command.app_id);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "debug app open executed: %s", command.app_id);
            } else {
                ESP_LOGW(TAG, "debug app open failed: %s (%s)", command.app_id, esp_err_to_name(ret));
            }
            debug_app_control_log_status("debug_app_open", ret);
            break;
        case DEBUG_APP_COMMAND_CLOSE:
            ret = watcher_app_close_current();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "debug app close executed");
            } else {
                ESP_LOGW(TAG, "debug app close failed: %s", esp_err_to_name(ret));
            }
            debug_app_control_log_status("debug_app_close", ret);
            break;
        case DEBUG_APP_COMMAND_STATUS:
            debug_app_control_log_status("debug_app_status", ESP_OK);
            break;
        case DEBUG_APP_COMMAND_CONNECT:
            app_connect_action_clicked(NULL);
            debug_app_control_log_status("debug_app_connect", ESP_OK);
            break;
        default:
            debug_app_control_log_status("debug_app_unknown", ESP_ERR_INVALID_ARG);
            break;
        }
    }
}
#endif

static esp_err_t app_resource_apply(watcher_app_resource_mode_t mode, watcher_app_resource_set_t resources,
                                    const char *app_id) {
    esp_err_t ret;
    esp_err_t status = ESP_OK;
    const watcher_app_resource_mode_t from_mode = s_app_resource_mode;
    const watcher_app_resource_set_t from_resources = s_app_resource_set;
    const char *safe_app_id = (app_id != NULL && app_id[0] != '\0') ? app_id : "unknown";
    char from_resource_text[80];
    char target_resource_text[80];
    bool requires_ble;
    bool requires_cloud;
    bool requires_mcu_runtime;

    /* Wi-Fi is platform infrastructure, not an app-owned disposable resource. */
    resources |= WATCHER_APP_RESOURCE_SET_WIFI_STA;
    requires_ble = (resources & WATCHER_APP_RESOURCE_SET_BLE) != 0;
    requires_cloud = (resources & WATCHER_APP_RESOURCE_SET_CLOUD) != 0;
    requires_mcu_runtime = (resources & WATCHER_APP_RESOURCE_SET_MCU_RUNTIME) != 0;
    app_resource_set_to_string(from_resources, from_resource_text, sizeof(from_resource_text));
    app_resource_set_to_string(resources, target_resource_text, sizeof(target_resource_text));

    ESP_LOGI(TAG, "Switching app resources: %s -> %s for %s", from_resource_text, target_resource_text, safe_app_id);
    ESP_LOGI(TAG, "evt=app_resource stage=request from=%s to=%s app=%s owner=%s result=pending", from_resource_text,
             target_resource_text, safe_app_id, app_resource_owner_to_string());

    if (!requires_mcu_runtime) {
        app_resource_stop_mcu_runtime(safe_app_id);
    }

    if (!requires_cloud) {
        ret = app_resource_stop_cloud(app_id);
        if (ret != ESP_OK && status == ESP_OK) {
            status = ret;
        }
        s_transport_state = TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED;
    }

    if (requires_ble) {
        ret = app_resource_start_ble();
        if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "BLE resource start failed for %s: %s", app_id != NULL ? app_id : "(unknown)",
                     esp_err_to_name(ret));
            if (status == ESP_OK) {
                status = ret;
            }
        }
    } else {
        ret = app_resource_stop_ble();
        if (ret != ESP_OK && status == ESP_OK) {
            status = ret;
        }
    }

    ret = app_resource_resume_wifi_if_needed(app_id);
    if (ret != ESP_OK && status == ESP_OK) {
        status = ret;
    }

    if (status == ESP_OK && requires_mcu_runtime) {
        ret = app_resource_start_mcu_runtime(safe_app_id);
        if (ret != ESP_OK) {
            status = ret;
        }
    }

    if (status == ESP_OK) {
        s_app_resource_mode = mode;
        s_app_resource_set = resources;
        app_resource_set_owner(mode, app_id);
        ESP_LOGI(TAG, "evt=app_resource stage=applied from=%s to=%s app=%s owner=%s result=%s", from_resource_text,
                 target_resource_text, safe_app_id, app_resource_owner_to_string(), esp_err_to_name(status));
    } else {
        ESP_LOGW(TAG, "evt=app_resource stage=failed from=%s to=%s app=%s owner=%s mode=%s->%s result=%s",
                 from_resource_text, target_resource_text, safe_app_id, app_resource_owner_to_string(),
                 app_resource_mode_to_string(from_mode), app_resource_mode_to_string(mode), esp_err_to_name(status));
    }
    LOG_HEAP_STATE("after_app_resource_switch");
    return status;
}

static void transport_discovery_task(void *arg) {
    discovery_result_t result = {0};
    uint32_t *generation = (uint32_t *)arg;

    result.generation = generation ? *generation : 0;
    result.status = discovery_start_with_cancel(&result.info, CLOUD_DISCOVERY_TIMEOUT_MS,
                                                transport_discovery_cancel_requested, generation);

    if (s_discovery_result_queue != NULL) {
        (void)xQueueOverwrite(s_discovery_result_queue, &result);
    }

    s_discovery_inflight = false;
    s_discovery_task = NULL;
    free(generation);
    vTaskDelete(NULL);
}

static bool transport_discovery_cancel_requested(void *ctx) {
    const uint32_t *generation = (const uint32_t *)ctx;

    return generation == NULL || *generation != s_discovery_generation;
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

    task_ret = xTaskCreate(transport_discovery_task, "cloud_discovery", CLOUD_DISCOVERY_TASK_STACK_BYTES, generation, 5,
                           &s_discovery_task);
    if (task_ret != pdPASS) {
        s_discovery_inflight = false;
        s_discovery_task = NULL;
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

static void transport_abort_discovery_request(const char *reason) {
    s_discovery_generation += 1U;
    transport_reset_discovery_result_queue();
    ESP_LOGI(TAG, "Discovery request aborted (%s)", reason ? reason : "no reason");
}

static void transport_reset_discovery_result_queue(void) {
    if (s_discovery_result_queue != NULL) {
        xQueueReset(s_discovery_result_queue);
    }
}

static bool transport_wait_for_discovery_idle(uint32_t timeout_ms) {
    uint32_t waited_ms = 0;

    while (s_discovery_inflight || s_discovery_task != NULL) {
        if (waited_ms >= timeout_ms) {
            transport_reset_discovery_result_queue();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        waited_ms += 50;
    }

    transport_reset_discovery_result_queue();
    return true;
}

static int transport_prepare_ws_client(const char *ws_url) {
    if (ws_url == NULL || ws_url[0] == '\0') {
        return -1;
    }

    if (!s_ws_stack_ready || strcmp(ws_client_get_server_url(), ws_url) != 0) {
        if (s_ws_stack_ready) {
            transport_stop_ws("ws url refresh");
            if (transport_deinit_ws_stack("ws url refresh") != ESP_OK) {
                return -1;
            }
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
            uint32_t retry_delay_ms = transport_note_discovery_failure_and_get_retry_delay();
            ESP_LOGW(TAG, "Discovery failed, retrying after %u ms (failures=%lu)", (unsigned)retry_delay_ms,
                     (unsigned long)s_consecutive_discovery_failures);
            transport_schedule_retry(retry_delay_ms);
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
        transport_reset_discovery_retry_backoff("discovery ready");

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
    const ble_connection_event_t event = {
        .connected = connected,
    };

    if (s_ble_connection_event_queue == NULL) {
        ESP_LOGW(TAG, "BLE connection event dropped before queue init: connected=%d", connected ? 1 : 0);
        return;
    }

    (void)xQueueOverwrite(s_ble_connection_event_queue, &event);
}

static void ble_connection_event_init(void) {
    if (s_ble_connection_event_queue == NULL) {
        s_ble_connection_event_queue = xQueueCreate(BLE_CONNECTION_EVENT_QUEUE_DEPTH, sizeof(ble_connection_event_t));
    }
    if (s_ble_connection_event_queue == NULL) {
        ESP_LOGW(TAG, "BLE connection event queue allocation failed");
    }
}

static void ble_connection_event_tick(void) {
    ble_connection_event_t event;

    if (s_ble_connection_event_queue == NULL) {
        return;
    }

    while (xQueueReceive(s_ble_connection_event_queue, &event, 0) == pdTRUE) {
        on_ble_connection_event(event.connected);
    }
}

static void on_ble_connection_event(bool connected) {
    s_last_ble_connected = connected;
    s_ble_connected_feedback_pending = connected && (active_app_is("ble.app") || active_app_is("provision.app"));
    settings_request_state_refresh();

    if (connected) {
        if (active_app_is("settings.app")) {
            provisioning_feedback_queue_sound(PROVISIONING_FEEDBACK_SOUND_BLE_CONNECTED);
        }
        transport_suspend_for_ble();
        return;
    }

    if (active_app_is("settings.app") && wifi_has_credentials() != 1) {
        provisioning_feedback_queue_sound(PROVISIONING_FEEDBACK_SOUND_BLE_DISCONNECTED);
    }
    ESP_LOGI(TAG, "BLE disconnected, allowing WiFi/WS recovery");
    transport_reset_cached_ws_resume_state();
    transport_schedule_retry(0);
    transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "ble disconnected");
}

static void transport_coordinator_tick(void) {
    bool ble_connected = ble_service_is_connected();

    ws_client_process_deferred_cleanup();

    if (s_app_resource_mode != WATCHER_APP_RESOURCE_WIFI_ONLY) {
        transport_cancel_discovery("wifi resource inactive");
        transport_stop_ws("wifi resource inactive");
        transport_reset_cached_ws_resume_state();
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "wifi resource inactive");
        return;
    }

    if (active_app_is_client_voice_host() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED) {
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "voice ws failed waiting retry");
        return;
    }

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
    if (s_transport_state == TRANSPORT_BLE_IDLE_NO_CREDENTIALS ||
        s_transport_state == TRANSPORT_BLE_IDLE_WIFI_STARTING ||
        s_transport_state == TRANSPORT_BLE_IDLE_WIFI_CONNECTING) {
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "wifi connected");
    }

    if (!s_discovery_initialized) {
        if (discovery_init() == 0) {
            s_discovery_initialized = true;
        } else {
            uint32_t retry_delay_ms = transport_note_discovery_failure_and_get_retry_delay();
            ESP_LOGW(TAG, "Discovery init failed, retrying later");
            transport_schedule_retry(retry_delay_ms);
            transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "discovery init failed");
            return;
        }
    }

    if (ws_client_is_session_ready()) {
        transport_cache_ws_url(ws_client_get_server_url(), "ws session ready");
        s_cached_ws_attempted_since_wifi_restore = false;
        s_cached_ws_connect_inflight = false;
        s_cached_ws_connect_started_us = 0;
        transport_reset_ws_connect_watchdog();
        ensure_cloud_runtime_started();
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_READY, "ws session ready");
        return;
    }

    if (ws_client_has_hello_rejected()) {
        ESP_LOGW(TAG, "WebSocket hello rejected; closing and retrying after server release window");
        transport_stop_ws("hello rejected");
        s_cached_ws_connect_inflight = false;
        s_cached_ws_connect_started_us = 0;
        transport_clear_cached_ws_url("hello rejected");
        transport_schedule_retry(1500);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "hello rejected retry");
        return;
    }

    if (s_cached_ws_connect_inflight) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_cached_ws_connect_started_us) / 1000LL;

        if (!(ws_client_is_connected() || ws_client_is_started())) {
            s_cached_ws_connect_inflight = false;
            s_cached_ws_connect_started_us = 0;
            transport_reset_ws_connect_watchdog();
            ESP_LOGW(TAG, "Cached WebSocket resume failed, falling back to discovery");
            transport_schedule_retry(0);
        } else if (elapsed_ms >= (int64_t)CACHED_WS_CONNECT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Cached WebSocket resume timed out after %lld ms, falling back to discovery", elapsed_ms);
            transport_recreate_ws_after_timeout("cached ws connect timeout", elapsed_ms);
            s_cached_ws_connect_inflight = false;
            s_cached_ws_connect_started_us = 0;
        } else {
            transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, "cached ws connecting");
            return;
        }
    }

    if (s_ws_connect_inflight && !(ws_client_is_connected() || ws_client_is_started())) {
        transport_reset_ws_connect_watchdog();
    }

    if (!s_ws_connect_inflight && (ws_client_is_connected() || ws_client_is_started())) {
        transport_arm_ws_connect_watchdog();
    }

    if (s_ws_connect_inflight && (ws_client_is_connected() || ws_client_is_started())) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_ws_connect_started_us) / 1000LL;

        if (elapsed_ms >= (int64_t)WS_SESSION_READY_TIMEOUT_MS) {
            transport_recreate_ws_after_timeout("ws session ready timeout", elapsed_ms);
        } else {
            transport_set_state(TRANSPORT_BLE_IDLE_WS_CONNECTING, "ws waiting for hello ack");
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
        uint32_t retry_delay_ms = transport_note_discovery_failure_and_get_retry_delay();
        transport_schedule_retry(retry_delay_ms);
        transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, "discovery launch failed");
    }
}

typedef struct {
    const char *id;
    const char *name;
    const void *default_img;
    const void *focused_img;
} launcher_entry_t;

static const launcher_entry_t s_launcher_entries[LAUNCHER_ENTRY_COUNT] = {
    {.id = CLIENT_APP_ID,
     .name = "Desktop\nLink",
     .default_img = &ui_img_currenttask_png,
     .focused_img = &ui_img_1158392298},
    {.id = PHONE_CONTROL_APP_ID,
     .name = "Phone\nControl",
     .default_img = &ui_img_phone_control_png,
     .focused_img = &ui_img_phone_control_focused_png},
    {.id = SDK_CONTROL_APP_ID,
     .name = "Python\nSDK",
     .default_img = &ui_img_python_sdk_png,
     .focused_img = &ui_img_python_sdk_focused_png},
    {.id = "agent.app",
     .name = "Agent",
     .default_img = &ui_img_agent_robot_png,
     .focused_img = &ui_img_agent_robot_focused_png},
    {.id = "settings.app",
     .name = "Settings",
     .default_img = &ui_img_setting_png,
     .focused_img = &ui_img_setting_f_png},
};

static const launcher_factory_home_entry_t s_launcher_home_entries[LAUNCHER_FACTORY_HOME_ENTRY_COUNT] = {
    {.title = "Desktop\nLink", .default_img = &ui_img_currenttask_png, .focused_img = &ui_img_1158392298},
    {.title = "Phone\nControl",
     .default_img = &ui_img_phone_control_png,
     .focused_img = &ui_img_phone_control_focused_png},
    {.title = "Python\nSDK", .default_img = &ui_img_python_sdk_png, .focused_img = &ui_img_python_sdk_focused_png},
    {.title = "Agent", .default_img = &ui_img_agent_robot_png, .focused_img = &ui_img_agent_robot_focused_png},
    {.title = "Settings", .default_img = &ui_img_setting_png, .focused_img = &ui_img_setting_f_png},
};

static int launcher_visible_entry_count(void) {
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    return LAUNCHER_PHONE_CONTROL_FIRMWARE_ENTRY_COUNT;
#else
    return LAUNCHER_ENTRY_COUNT;
#endif
}

static bool launcher_entry_index_is_visible(int index) {
    return index >= 0 && index < launcher_visible_entry_count();
}

static lv_obj_t *s_launcher_screen = NULL;
static lv_group_t *s_launcher_group = NULL;
static int s_launcher_selected = LAUNCHER_HOME_SELECTED_INDEX;
static int s_launcher_pending_open_index = -1;
static bool s_launcher_render_requested = false;
static volatile bool s_launcher_status_refresh_requested = false;
static int64_t s_launcher_next_status_update_us = 0;
static int64_t s_launcher_next_time_update_us = 0;
static int64_t s_launcher_diag_stable_1s_due_us = 0;
static int64_t s_launcher_diag_stable_3s_due_us = 0;
static bool s_launcher_screen_cached_for_foreground_app = false;
static char s_launcher_time_text[LAUNCHER_HOME_TIME_TEXT_LEN] = "--:--";
static bool s_launcher_status_cache_valid = false;
static char s_launcher_last_time_text[LAUNCHER_HOME_TIME_TEXT_LEN] = "";
static char s_launcher_last_battery_text[LAUNCHER_HOME_BATTERY_TEXT_LEN] = "";
static launcher_home_battery_state_t s_launcher_last_battery_state = LAUNCHER_HOME_BATTERY_UNKNOWN;
static bool s_launcher_last_wifi_connected = false;
static bool s_launcher_last_ble_connected = false;
static int64_t s_launcher_open_input_block_until_us = 0;

static void launcher_clamp_selected_to_visible_entry(void) {
    int visible_count = launcher_visible_entry_count();

    if (visible_count <= 0) {
        s_launcher_selected = 0;
    } else if (s_launcher_selected < 0 || s_launcher_selected >= visible_count) {
        s_launcher_selected = 0;
    }
}

static lv_obj_t *s_local_screen = NULL;
static lv_group_t *s_local_group = NULL;
static lv_obj_t *s_local_exit = NULL;
static lv_obj_t *s_local_focus_pad = NULL;
static int64_t s_local_exit_hide_at_us = 0;
static int64_t s_local_exit_show_since_us = 0;
static bool s_local_exit_visible = false;
static bool s_local_overlay_update_pending = false;
static bool s_local_overlay_region_valid = false;
static animation_overlay_region_t s_local_overlay_region = {0};
static sdk_control_ui_t s_sdk_control_ui = {0};
static lv_obj_t *s_sdk_control_screen = NULL;
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static volatile bool s_phone_control_firmware_reboot_to_launcher_pending = false;
#endif

static void shutdown_freeze_app_inputs(void) {
    watcher_input_router_global_clear_pending();
    s_local_return_to_launcher_pending = false;
    s_launcher_pending_open_index = -1;
    s_launcher_render_requested = false;
}

static void app_clear_group(lv_group_t **group) {
    if (group != NULL && *group != NULL) {
        lv_group_del(*group);
        *group = NULL;
    }
}

static void app_focus_obj(lv_group_t **group, lv_obj_t *obj) {
    lv_indev_t *indev = NULL;

    if (obj == NULL) {
        return;
    }

    if (*group == NULL) {
        *group = lv_group_create();
        if (*group == NULL) {
            return;
        }
    }

    lv_group_add_obj(*group, obj);
    lv_group_focus_obj(obj);
    lv_group_set_editing(*group, true);

    indev = lv_indev_get_next(NULL);
    while (indev != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(indev, *group);
        }
        indev = lv_indev_get_next(indev);
    }
}

static void app_replace_screen(lv_obj_t **screen, uint32_t bg_color) {
    lv_obj_t *old_screen = *screen;

    if (old_screen != NULL) {
        lv_obj_clean(old_screen);
        return;
    }

    lv_obj_t *old_boot_screen = boot_anim_get_screen();
    old_screen = lv_disp_get_scr_act(NULL);
    *screen = lv_obj_create(NULL);
    lv_obj_set_size(*screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(*screen, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(*screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(*screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(*screen, LV_SCROLLBAR_MODE_OFF);
    lv_disp_load_scr(*screen);
    if (old_screen != NULL && old_screen != *screen) {
        lv_obj_del(old_screen);
    }
    if (old_boot_screen != NULL && old_boot_screen != old_screen && old_boot_screen != *screen) {
        lv_obj_del(old_boot_screen);
    }
}

static void app_ui_diag_log_lvgl_locked(const char *stage) {
    lv_obj_t *active = lv_disp_get_scr_act(NULL);
    lv_disp_t *disp = lv_disp_get_default();
    lv_mem_monitor_t mem = {0};

    lv_mem_monitor(&mem);
    ESP_LOGW(UI_DIAG_TAG,
             "evt=app_ui_diag_core stage=%s screen_count=%lu active=%p active_children=%lu launcher_screen=%p "
             "launcher_children=%lu",
             stage != NULL ? stage : "unknown", disp != NULL ? (unsigned long)disp->screen_cnt : 0UL, active,
             active != NULL ? (unsigned long)lv_obj_get_child_cnt(active) : 0UL, s_launcher_screen,
             s_launcher_screen != NULL ? (unsigned long)lv_obj_get_child_cnt(s_launcher_screen) : 0UL);
    ESP_LOGW(UI_DIAG_TAG, "evt=app_ui_diag_refs stage=%s launcher_group=%p local_exit=%p local_focus_pad=%p",
             stage != NULL ? stage : "unknown", s_launcher_group, s_local_exit, s_local_focus_pad);
    ESP_LOGW(UI_DIAG_TAG, "evt=app_ui_diag_lv_mem stage=%s total=%lu free=%lu used_pct=%u frag_pct=%u free_biggest=%lu",
             stage != NULL ? stage : "unknown", (unsigned long)mem.total_size, (unsigned long)mem.free_size,
             (unsigned)mem.used_pct, (unsigned)mem.frag_pct, (unsigned long)mem.free_biggest_size);
    factory_settings_ui_log_debug_snapshot(stage);
}

static void app_ui_diag_log_snapshot(const char *stage) {
    LOG_HEAP_STATE(stage);
    if (!lvgl_port_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(UI_DIAG_TAG, "evt=app_ui_diag_lock_failed stage=%s", stage != NULL ? stage : "unknown");
        return;
    }
    app_ui_diag_log_lvgl_locked(stage);
    lvgl_port_unlock();
}

static void app_ui_diag_log_snapshot_locked(const char *stage) {
    LOG_HEAP_STATE(stage);
    app_ui_diag_log_lvgl_locked(stage);
}

static void launcher_diag_schedule_stable_samples(void) {
    int64_t now_us = esp_timer_get_time();

    s_launcher_diag_stable_1s_due_us = now_us + 1000000LL;
    s_launcher_diag_stable_3s_due_us = now_us + 3000000LL;
}

static void log_task_stack_hwm_entry(const char *stage, const char *task_name, size_t configured_bytes,
                                     size_t free_bytes) {
    if (free_bytes > configured_bytes) {
        ESP_LOGI("MEM_MON", "evt=task_stack_hwm stage=%s task=%s status=unavailable configured=%uB",
                 stage != NULL ? stage : "unknown", task_name != NULL ? task_name : "unknown",
                 (unsigned)configured_bytes);
        return;
    }
    const size_t used_peak_bytes = configured_bytes > free_bytes ? configured_bytes - free_bytes : 0U;
    const char *status = free_bytes < TASK_STACK_HWM_WARN_BYTES ? "warn" : "ok";

    ESP_LOGI("MEM_MON", "evt=task_stack_hwm stage=%s task=%s status=%s configured=%uB free=%uB used_peak=%uB",
             stage != NULL ? stage : "unknown", task_name != NULL ? task_name : "unknown", status,
             (unsigned)configured_bytes, (unsigned)free_bytes, (unsigned)used_peak_bytes);
}

static void log_task_stack_hwm_snapshot(const char *stage) {
    log_task_stack_hwm_entry(stage, "main", CONFIG_ESP_MAIN_TASK_STACK_SIZE, (size_t)uxTaskGetStackHighWaterMark(NULL));
    log_task_stack_hwm_entry(stage, "behavior_state", behavior_state_stack_size(),
                             behavior_state_stack_high_watermark());
    log_task_stack_hwm_entry(stage, "sfx_task", sfx_service_stack_size(), sfx_service_stack_high_watermark());
}

static void launcher_diag_log_stable_if_due(void) {
    int64_t now_us = esp_timer_get_time();

    if (s_launcher_diag_stable_1s_due_us > 0 && now_us >= s_launcher_diag_stable_1s_due_us) {
        s_launcher_diag_stable_1s_due_us = 0;
        app_ui_diag_log_snapshot("launcher_stable_1s");
    }
    if (s_launcher_diag_stable_3s_due_us > 0 && now_us >= s_launcher_diag_stable_3s_due_us) {
        s_launcher_diag_stable_3s_due_us = 0;
        app_ui_diag_log_snapshot("launcher_stable_3s");
        log_task_stack_hwm_snapshot("launcher_stable_3s");
    }
}

static void launcher_arm_open_input_settle(uint32_t duration_ms, const char *reason) {
    int64_t now_us = esp_timer_get_time();
    int64_t until_us = now_us + ((int64_t)duration_ms * 1000LL);

    if (until_us > s_launcher_open_input_block_until_us) {
        s_launcher_open_input_block_until_us = until_us;
    }
    ESP_LOGI(TAG, "Launcher open input settle armed duration_ms=%u reason=%s", (unsigned)duration_ms,
             reason != NULL ? reason : "(none)");
}

static bool launcher_should_ignore_open_input(const char *source, int index) {
    int64_t now_us = esp_timer_get_time();
    int64_t remaining_ms;
    const char *id = "(unknown)";

    if (s_launcher_open_input_block_until_us <= 0) {
        return false;
    }
    if (now_us >= s_launcher_open_input_block_until_us) {
        s_launcher_open_input_block_until_us = 0;
        return false;
    }

    if (launcher_entry_index_is_visible(index)) {
        id = s_launcher_entries[index].id;
    }
    remaining_ms = (s_launcher_open_input_block_until_us - now_us) / 1000LL;
    ESP_LOGW(TAG, "Launcher %s ignored during return settle index=%d id=%s remaining_ms=%lld",
             source != NULL ? source : "open", index, id, remaining_ms);
    return true;
}

static bool launcher_should_cache_screen_for_app(const char *id) {
    return id != NULL &&
           (app_id_is_client_voice_host(id) || strcmp(id, "agent.app") == 0 || strcmp(id, "settings.app") == 0);
}

static bool launcher_app_uses_behavior_display_ui(const char *id) {
    return id != NULL && (app_id_is_client_voice_host(id) || strcmp(id, "agent.app") == 0);
}

static bool launcher_prepare_screen_cache_for_app(const char *id) {
    bool should_cache = launcher_should_cache_screen_for_app(id);
    bool cached = false;
    const bool heavy_target = launcher_app_uses_behavior_display_ui(id);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    const launcher_screen_cache_limits_t limits = {
        .min_internal_free_bytes = CONFIG_WATCHER_LAUNCHER_CACHE_MIN_INTERNAL_FREE_KB * 1024U,
        .min_internal_largest_bytes = CONFIG_WATCHER_LAUNCHER_CACHE_MIN_INTERNAL_LARGEST_KB * 1024U,
        .min_dma_largest_bytes = CONFIG_WATCHER_LAUNCHER_CACHE_MIN_DMA_LARGEST_KB * 1024U,
        .target_internal_reserve_bytes = heavy_target ? 40U * 1024U : 0U,
        .target_largest_reserve_bytes = heavy_target ? 16U * 1024U : 0U,
    };

    should_cache =
        launcher_screen_cache_policy_allows(should_cache, internal_free, internal_largest, dma_largest, &limits);
    if (!should_cache || s_launcher_screen == NULL) {
        if (s_launcher_screen != NULL && launcher_should_cache_screen_for_app(id)) {
            ESP_LOGI(TAG,
                     "Launcher screen cache skipped for %s: int_free=%uKB int_largest=%uKB dma_largest=%uKB "
                     "reserve=%u/%uKB",
                     id != NULL ? id : "(unknown)", (unsigned)(internal_free / 1024U),
                     (unsigned)(internal_largest / 1024U), (unsigned)(dma_largest / 1024U),
                     (unsigned)(limits.target_internal_reserve_bytes / 1024U),
                     (unsigned)(limits.target_largest_reserve_bytes / 1024U));
        }
        s_launcher_screen_cached_for_foreground_app = false;
        return false;
    }

    if (!lvgl_port_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Launcher screen cache skipped for %s: LVGL lock failed", id);
        s_launcher_screen_cached_for_foreground_app = false;
        return false;
    }

    cached = lv_disp_get_scr_act(NULL) == s_launcher_screen;
    if (cached && launcher_app_uses_behavior_display_ui(id)) {
        hal_display_retain_previous_screen_once(s_launcher_screen);
    }
    lvgl_port_unlock();

    s_launcher_screen_cached_for_foreground_app = cached;
    if (cached) {
        ESP_LOGI(TAG, "Launcher screen cached for %s foreground handoff", id);
    }
    return cached;
}

static void launcher_request_open_selected(void) {
    if (s_shutdown_in_progress) {
        ESP_LOGI(TAG, "Launcher open ignored while shutdown is in progress");
        s_launcher_pending_open_index = -1;
        return;
    }
    if (!launcher_entry_index_is_visible(s_launcher_selected)) {
        ESP_LOGW(TAG, "Launcher open ignored for hidden index=%d visible_count=%d", s_launcher_selected,
                 launcher_visible_entry_count());
        s_launcher_pending_open_index = -1;
        return;
    }
    if (launcher_should_ignore_open_input("queued open", s_launcher_selected)) {
        s_launcher_pending_open_index = -1;
        return;
    }
    s_launcher_pending_open_index = s_launcher_selected;
    ESP_LOGW(TAG, "Launcher queued open index=%d id=%s", s_launcher_pending_open_index,
             s_launcher_entries[s_launcher_pending_open_index].id);
}

static void launcher_open_pending(void) {
    int index = s_launcher_pending_open_index;
    const char *id;
    bool cached_launcher_screen = false;

    if (!launcher_entry_index_is_visible(index)) {
        return;
    }
    if (s_shutdown_in_progress) {
        ESP_LOGI(TAG, "Launcher pending open ignored while shutdown is in progress");
        s_launcher_pending_open_index = -1;
        return;
    }
    if (launcher_should_ignore_open_input("pending open", index)) {
        s_launcher_pending_open_index = -1;
        return;
    }

    s_launcher_pending_open_index = -1;
    id = s_launcher_entries[index].id;
    cached_launcher_screen = launcher_prepare_screen_cache_for_app(id);
    if (watcher_app_open(id) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open launcher entry: %s", id);
        if (cached_launcher_screen) {
            s_launcher_screen_cached_for_foreground_app = false;
            hal_display_retain_previous_screen_once(NULL);
        }
    }
}

static void launcher_factory_focused_cb(int index, void *user_ctx) {
    (void)user_ctx;
    if (!launcher_entry_index_is_visible(index)) {
        return;
    }
    s_launcher_selected = index;
    ESP_LOGD(TAG, "Launcher focused index=%d id=%s", index, s_launcher_entries[index].id);
}

static void launcher_factory_clicked_cb(int index, void *user_ctx) {
    (void)user_ctx;
    if (s_shutdown_in_progress) {
        ESP_LOGI(TAG, "Launcher card click ignored while shutdown is in progress");
        return;
    }
    if (!launcher_entry_index_is_visible(index)) {
        return;
    }
    if (launcher_should_ignore_open_input("card click", index)) {
        return;
    }
    s_launcher_selected = index;
    ESP_LOGW(TAG, "Launcher card clicked index=%d id=%s", index, s_launcher_entries[index].id);
    launcher_request_open_selected();
}

static void launcher_request_status_refresh(void) {
    s_launcher_status_refresh_requested = true;
}

static void boot_prime_power_monitor_for_launcher(void) {
    esp_err_t ret = power_monitor_service_tick(true);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Boot power monitor prime failed: %s", esp_err_to_name(ret));
        return;
    }

    launcher_request_status_refresh();
}

static void on_time_synchronized(void) {
    launcher_request_status_refresh();
}

static bool launcher_time_is_valid(const struct tm *timeinfo) {
    int year;

    if (timeinfo == NULL) {
        return false;
    }

    year = timeinfo->tm_year + 1900;
    return year >= 2023 && year <= 2099 && timeinfo->tm_mon >= 0 && timeinfo->tm_mon <= 11 && timeinfo->tm_mday >= 1 &&
           timeinfo->tm_mday <= 31 && timeinfo->tm_hour >= 0 && timeinfo->tm_hour <= 23 && timeinfo->tm_min >= 0 &&
           timeinfo->tm_min <= 59 && timeinfo->tm_sec >= 0 && timeinfo->tm_sec <= 60;
}

static bool launcher_read_time(char out_time[LAUNCHER_HOME_TIME_TEXT_LEN], int *out_seconds_until_refresh) {
    struct tm timeinfo = {0};
    time_t now;

    if (out_time == NULL || out_seconds_until_refresh == NULL) {
        return false;
    }

    now = time(NULL);
    if (now > 1672531200 && localtime_r(&now, &timeinfo) != NULL && launcher_time_is_valid(&timeinfo) &&
        launcher_home_format_time_text(timeinfo.tm_hour, timeinfo.tm_min, out_time, LAUNCHER_HOME_TIME_TEXT_LEN)) {
        *out_seconds_until_refresh = 60 - timeinfo.tm_sec;
        return true;
    }

    memcpy(out_time, "--:--", LAUNCHER_HOME_TIME_TEXT_LEN);
    *out_seconds_until_refresh = LAUNCHER_TIME_RETRY_MS / 1000;
    return false;
}

static void launcher_update_status_locked(bool force) {
    int64_t now_us = esp_timer_get_time();
    int seconds_until_time_refresh = LAUNCHER_TIME_RETRY_MS / 1000;
    power_monitor_sample_t power_sample = {0};
    bool power_sample_valid = false;
    launcher_home_battery_state_t battery_state = LAUNCHER_HOME_BATTERY_UNKNOWN;
    char battery_text[LAUNCHER_HOME_BATTERY_TEXT_LEN] = "--";
    bool wifi_connected = false;
    bool ble_connected = false;
    bool status_changed = false;

    if (!force && s_launcher_next_status_update_us != 0 && now_us < s_launcher_next_status_update_us) {
        return;
    }

    if (force || s_launcher_next_time_update_us == 0 || now_us >= s_launcher_next_time_update_us) {
        (void)launcher_read_time(s_launcher_time_text, &seconds_until_time_refresh);
        if (seconds_until_time_refresh < 1) {
            seconds_until_time_refresh = 1;
        } else if (seconds_until_time_refresh > 60) {
            seconds_until_time_refresh = 60;
        }
        s_launcher_next_time_update_us = now_us + ((int64_t)seconds_until_time_refresh * 1000LL * 1000LL);
    }

    power_sample_valid = power_monitor_service_get_snapshot(&power_sample) == ESP_OK;
    battery_state = launcher_home_resolve_battery_state(
        power_sample_valid, power_sample.vbus_present, power_sample.charging, power_sample.battery_present,
        power_sample.battery_percent_valid, power_sample.battery_percent);
    if (power_sample_valid && power_sample.battery_percent_valid) {
        (void)launcher_home_format_battery_text(power_sample.battery_percent, battery_text, sizeof(battery_text));
    } else {
        (void)launcher_home_format_battery_text(-1, battery_text, sizeof(battery_text));
    }

    wifi_connected = wifi_is_connected() == 1;
    ble_connected = ble_service_is_connected();
    status_changed = !s_launcher_status_cache_valid || strcmp(s_launcher_last_time_text, s_launcher_time_text) != 0 ||
                     strcmp(s_launcher_last_battery_text, battery_text) != 0 ||
                     s_launcher_last_battery_state != battery_state ||
                     s_launcher_last_wifi_connected != wifi_connected || s_launcher_last_ble_connected != ble_connected;
    if (status_changed) {
        launcher_factory_home_update_status(s_launcher_time_text, battery_text, battery_state, wifi_connected,
                                            ble_connected);
        strncpy(s_launcher_last_time_text, s_launcher_time_text, sizeof(s_launcher_last_time_text) - 1);
        s_launcher_last_time_text[sizeof(s_launcher_last_time_text) - 1] = '\0';
        strncpy(s_launcher_last_battery_text, battery_text, sizeof(s_launcher_last_battery_text) - 1);
        s_launcher_last_battery_text[sizeof(s_launcher_last_battery_text) - 1] = '\0';
        s_launcher_last_battery_state = battery_state;
        s_launcher_last_wifi_connected = wifi_connected;
        s_launcher_last_ble_connected = ble_connected;
        s_launcher_status_cache_valid = true;
    }
    s_launcher_next_status_update_us = now_us + ((int64_t)LAUNCHER_STATUS_REFRESH_MS * 1000LL);
}

static void launcher_build_static_locked(void) {
    launcher_factory_home_callbacks_t callbacks = {
        .on_focused = launcher_factory_focused_cb,
        .on_clicked = launcher_factory_clicked_cb,
        .user_ctx = NULL,
    };

    launcher_clamp_selected_to_visible_entry();
    app_replace_screen(&s_launcher_screen, 0x000000);
    launcher_factory_home_build(s_launcher_screen, s_launcher_home_entries, (size_t)launcher_visible_entry_count(),
                                &callbacks);
    launcher_factory_home_bind_group(&s_launcher_group, s_launcher_selected);
}

static void launcher_update_focus_locked(bool animate) {
    launcher_clamp_selected_to_visible_entry();
    launcher_factory_home_focus_index(s_launcher_selected, animate);
}

static void launcher_update_locked(bool animate) {
    launcher_update_status_locked(true);
    launcher_update_focus_locked(animate);
}

static void launcher_render_locked(void) {
    bool created = s_launcher_screen == NULL;

    if (s_launcher_screen == NULL) {
        launcher_build_static_locked();
    } else if (lv_disp_get_scr_act(NULL) != s_launcher_screen) {
        lv_obj_t *old_active = lv_disp_get_scr_act(NULL);

        lv_disp_load_scr(s_launcher_screen);
        if (old_active != NULL && old_active != s_launcher_screen) {
            lv_obj_del(old_active);
        }
        s_launcher_screen_cached_for_foreground_app = false;
    }

    if (s_launcher_group == NULL) {
        launcher_factory_home_bind_group(&s_launcher_group, s_launcher_selected);
    }

    launcher_update_locked(!created);
}

static void launcher_on_open(void) {
    app_ui_diag_log_snapshot("launcher_on_open_before");
    app_animation_ui_deinit();
    s_launcher_render_requested = false;
    s_launcher_next_status_update_us = 0;
    s_launcher_next_time_update_us = 0;
    s_launcher_status_cache_valid = false;
    memcpy(s_launcher_time_text, "--:--", sizeof(s_launcher_time_text));
    lvgl_port_lock(0);
    launcher_render_locked();
    app_ui_diag_log_snapshot_locked("launcher_on_open_after");
    lvgl_port_unlock();
    launcher_diag_schedule_stable_samples();
}

static void launcher_on_tick(void) {
    if (s_launcher_status_refresh_requested) {
        s_launcher_status_refresh_requested = false;
        s_launcher_next_status_update_us = 0;
        s_launcher_next_time_update_us = 0;
        lvgl_port_lock(0);
        launcher_update_status_locked(true);
        lvgl_port_unlock();
    }

    if (s_launcher_pending_open_index >= 0) {
        launcher_open_pending();
        return;
    }

    if (s_launcher_screen != NULL && s_launcher_next_status_update_us != 0 &&
        esp_timer_get_time() >= s_launcher_next_status_update_us) {
        lvgl_port_lock(0);
        launcher_update_status_locked(false);
        lvgl_port_unlock();
    }

    launcher_diag_log_stable_if_due();

    if (!s_launcher_render_requested) {
        return;
    }
    s_launcher_render_requested = false;
    lvgl_port_lock(0);
    if (s_launcher_screen == NULL) {
        launcher_render_locked();
    } else {
        launcher_update_locked(true);
    }
    lvgl_port_unlock();
}

static void launcher_on_close(void) {
    bool preserve_screen = s_launcher_screen_cached_for_foreground_app && s_launcher_screen != NULL;

    s_launcher_diag_stable_1s_due_us = 0;
    s_launcher_diag_stable_3s_due_us = 0;
    lvgl_port_lock(0);
    app_clear_group(&s_launcher_group);
    s_launcher_pending_open_index = -1;
    s_launcher_next_status_update_us = 0;
    s_launcher_next_time_update_us = 0;
    s_launcher_status_cache_valid = false;
    s_launcher_open_input_block_until_us = 0;
    memcpy(s_launcher_time_text, "--:--", sizeof(s_launcher_time_text));
    if (!preserve_screen) {
        s_launcher_screen = NULL;
        launcher_factory_home_reset();
        s_launcher_screen_cached_for_foreground_app = false;
    }
    lvgl_port_unlock();
    if (preserve_screen) {
        ESP_LOGI(TAG, "Launcher screen retained for foreground app");
    }
}

static const watcher_app_t s_launcher_app = {
    .id = "launcher",
    .name = "Launcher",
    .icon = "launcher",
    .theme_color = 0x7DFFD6,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
    .input_context = WATCHER_INPUT_CONTEXT_LVGL_NAV,
    .on_open = launcher_on_open,
    .on_tick = launcher_on_tick,
    .on_close = launcher_on_close,
};

static bool local_exit_is_visible(void) {
    return s_local_exit_visible;
}

static void local_update_exit_anim_protection_locked(void) {
    lv_area_t area;

    if (s_local_exit == NULL || !s_local_exit_visible || lv_obj_has_flag(s_local_exit, LV_OBJ_FLAG_HIDDEN)) {
        s_local_overlay_region_valid = false;
        s_local_overlay_update_pending = true;
        return;
    }

    lv_obj_get_coords(s_local_exit, &area);
    s_local_overlay_region =
        (animation_overlay_region_t){.x1 = area.x1, .y1 = area.y1, .x2 = area.x2 + 1, .y2 = area.y2 + 1};
    s_local_overlay_region_valid = true;
    s_local_overlay_update_pending = true;
}

static void local_apply_exit_anim_protection(void) {
    if (!s_local_overlay_update_pending) {
        return;
    }
    s_local_overlay_update_pending = false;
    if (s_local_overlay_region_valid) {
        (void)animation_set_overlay_region(&s_local_overlay_region);
    } else {
        (void)animation_clear_overlay_region();
    }
}

static void local_show_exit(void) {
    int64_t now_us;

    if (s_local_exit == NULL) {
        ESP_LOGW(TAG, "Local show exit skipped: exit object is null");
        return;
    }
    now_us = esp_timer_get_time();
    lv_obj_move_foreground(s_local_exit);
    lv_obj_clear_flag(s_local_exit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_local_exit, lv_color_hex(0x0F8F68), 0);
    lv_obj_set_style_border_color(s_local_exit, lv_color_hex(0xA8FFE8), 0);
    lv_obj_set_style_shadow_width(s_local_exit, 18, 0);
    lv_obj_set_style_shadow_opa(s_local_exit, LV_OPA_50, 0);
    lv_group_focus_obj(s_local_exit);
    s_local_exit_show_since_us = now_us;
    s_local_exit_hide_at_us = now_us + (int64_t)LOCAL_EXIT_AUTO_HIDE_MS * 1000LL;
    s_local_exit_visible = true;
    local_update_exit_anim_protection_locked();
    ESP_LOGW(TAG, "Local exit show: visible=%d show_since=%lld hide_at=%lld", local_exit_is_visible() ? 1 : 0,
             (long long)s_local_exit_show_since_us, (long long)s_local_exit_hide_at_us);
}

static void local_hide_exit_internal(void) {
    if (s_local_exit != NULL) {
        lv_obj_add_flag(s_local_exit, LV_OBJ_FLAG_HIDDEN);
    }
    s_local_exit_visible = false;
    local_update_exit_anim_protection_locked();
    if (s_local_focus_pad != NULL) {
        lv_group_focus_obj(s_local_focus_pad);
    }
    ESP_LOGW(TAG, "Local exit hide: now=%lld show_since=%lld hide_at=%lld", (long long)esp_timer_get_time(),
             (long long)s_local_exit_show_since_us, (long long)s_local_exit_hide_at_us);
    s_local_exit_hide_at_us = 0;
    s_local_exit_show_since_us = 0;
}

static void local_hide_exit(void) {
    local_hide_exit_internal();
}

static void local_queue_return_to_launcher(void) {
    if (s_shutdown_in_progress) {
        ESP_LOGI(TAG, "Return-to-launcher ignored while shutdown is in progress");
        s_local_return_to_launcher_pending = false;
        return;
    }
    s_local_return_to_launcher_pending = true;
}

#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static bool phone_control_firmware_should_reboot_on_local_exit(void) {
    return active_app_is(PHONE_CONTROL_APP_ID);
}

static void phone_control_firmware_request_reboot_to_launcher_partition(void) {
    s_local_return_to_launcher_pending = false;
    s_phone_control_firmware_reboot_to_launcher_pending = true;
}

static void phone_control_firmware_reboot_to_launcher_partition(void) {
    s_phone_control_firmware_reboot_to_launcher_pending = false;
    ESP_LOGW(TAG, "Phone Control exit requested; rebooting to Launcher partition");
    arm_firmware_app_return_to_launcher_on_next_boot("Phone Control exit");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

static void phone_control_firmware_process_reboot_to_launcher_partition(void) {
    if (!s_phone_control_firmware_reboot_to_launcher_pending) {
        return;
    }
    phone_control_firmware_reboot_to_launcher_partition();
}
#endif

static void local_return_to_launcher(void) {
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    if (phone_control_firmware_should_reboot_on_local_exit()) {
        phone_control_firmware_request_reboot_to_launcher_partition();
        return;
    }
#endif
    lvgl_port_lock(0);
    local_hide_exit_internal();
    lvgl_port_unlock();
    local_queue_return_to_launcher();
}

static void local_return_to_launcher_from_lvgl_event(bool settle_pointer_input) {
    if (s_shutdown_in_progress) {
        return;
    }
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    if (phone_control_firmware_should_reboot_on_local_exit()) {
        phone_control_firmware_request_reboot_to_launcher_partition();
        return;
    }
#endif
    if (settle_pointer_input) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev != NULL && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_wait_release(indev);
            launcher_arm_open_input_settle(LAUNCHER_RETURN_TOUCH_SETTLE_MS, "local exit touch");
        }
    }
    local_hide_exit_internal();
    local_queue_return_to_launcher();
}

static void process_pending_app_navigation(void) {
    if (!s_local_return_to_launcher_pending) {
        return;
    }
    if (s_shutdown_in_progress) {
        s_local_return_to_launcher_pending = false;
        return;
    }

    s_local_return_to_launcher_pending = false;
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    if (phone_control_firmware_should_reboot_on_local_exit()) {
        phone_control_firmware_request_reboot_to_launcher_partition();
        phone_control_firmware_process_reboot_to_launcher_partition();
        return;
    }
#endif
    if (watcher_app_open("launcher") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to return to launcher");
    }
}

static void maybe_update_local_ble_connection_feedback(void) {
    bool connected;

    if (!active_app_is("ble.app") && !active_app_is("provision.app")) {
        return;
    }

    connected = ble_service_is_connected();
    if (connected == s_last_ble_connected) {
        return;
    }

    s_last_ble_connected = connected;
    s_ble_connected_feedback_pending = connected;
    if (!connected) {
        return;
    }

    ESP_LOGI(TAG, "Local BLE app connection detected; feedback pending");
}

static void local_exit_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED) {
        if (!local_exit_is_visible()) {
            return;
        }
        local_return_to_launcher_from_lvgl_event(true);
    }
}

static void local_touch_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    const watcher_app_t *active_app = NULL;
    lv_indev_t *indev = NULL;
    lv_point_t point = {0};

    if (code != LV_EVENT_SHORT_CLICKED) {
        return;
    }

    if (local_exit_is_visible()) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    if (active_app_is_client_voice_host() && voice_app_handle_connect_action("screen-touch")) {
        ESP_LOGI(TAG, "Desktop Link connect action handled by screen touch x=%d y=%d", (int)point.x, (int)point.y);
        return;
    }

    active_app = watcher_app_get_active();
    if (active_app == NULL || active_app->on_touch == NULL) {
        return;
    }

    active_app->on_touch(point.x, point.y);
}

static void local_key_event_cb(lv_event_t *event) {
    uint32_t key;

    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    key = *((uint32_t *)lv_event_get_param(event));
    if (key == LV_KEY_ENTER && local_exit_is_visible()) {
        local_return_to_launcher_from_lvgl_event(false);
        return;
    }

    if (key == LV_KEY_NEXT || key == LV_KEY_PREV || key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP ||
        key == LV_KEY_DOWN) {
        local_show_exit();
    }
}

static void local_gesture_event_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev == NULL) {
            return;
        }
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        ESP_LOGW(TAG, "Local LVGL gesture dir=%d", (int)dir);
        if (dir == LV_DIR_TOP) {
            local_show_exit();
        }
    }
}

static void local_exit_attach_locked(void) {
    lv_obj_t *screen = lv_disp_get_scr_act(NULL);
    lv_obj_t *overlay_parent = lv_layer_top();

    app_clear_group(&s_local_group);
    s_local_screen = screen;
    s_local_exit = NULL;
    s_local_focus_pad = NULL;
    s_local_exit_hide_at_us = 0;
    s_local_exit_show_since_us = 0;
    s_local_exit_visible = false;

    if (screen == NULL) {
        return;
    }

    lv_obj_remove_event_cb(screen, local_touch_event_cb);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, local_touch_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(screen, local_gesture_event_cb, LV_EVENT_GESTURE, NULL);

    if (overlay_parent == NULL) {
        overlay_parent = screen;
    }

    s_local_focus_pad = lv_btn_create(overlay_parent);
    lv_obj_set_size(s_local_focus_pad, 1, 1);
    lv_obj_align(s_local_focus_pad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_local_focus_pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_local_focus_pad, 0, 0);
    lv_obj_add_event_cb(s_local_focus_pad, local_key_event_cb, LV_EVENT_KEY, NULL);
    app_focus_obj(&s_local_group, s_local_focus_pad);

    s_local_exit = lv_btn_create(overlay_parent);
    lv_obj_set_size(s_local_exit, 160, 64);
    lv_obj_align(s_local_exit, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(s_local_exit, 24, 0);
    lv_obj_set_style_bg_color(s_local_exit, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_border_width(s_local_exit, 2, 0);
    lv_obj_set_style_border_color(s_local_exit, lv_color_hex(0x7DFFD6), 0);
    lv_obj_set_style_shadow_color(s_local_exit, lv_color_hex(0x7DFFD6), 0);
    lv_obj_add_flag(s_local_exit, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_local_exit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_local_exit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_local_exit, 16);
    lv_obj_add_event_cb(s_local_exit, local_exit_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_local_exit, local_exit_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_local_exit, local_key_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(s_local_group, s_local_exit);

    lv_obj_t *exit_label = lv_label_create(s_local_exit);
    lv_label_set_text(exit_label, "Exit");
    lv_obj_set_style_text_color(exit_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(exit_label);
    local_hide_exit_internal();
}

static void local_app_cleanup(void) {
    lvgl_port_lock(0);
    s_local_overlay_region_valid = false;
    s_local_overlay_update_pending = true;
    app_clear_group(&s_local_group);
    if (s_local_exit != NULL) {
        lv_obj_del(s_local_exit);
    }
    if (s_local_focus_pad != NULL) {
        lv_obj_del(s_local_focus_pad);
    }
    s_local_screen = NULL;
    s_local_exit = NULL;
    s_local_focus_pad = NULL;
    s_local_exit_show_since_us = 0;
    s_local_exit_hide_at_us = 0;
    s_local_exit_visible = false;
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
}

static void local_behavior_app_cleanup(void) {
    local_app_cleanup();
    app_animation_ui_deinit();
    esp_err_t sfx_ret = sfx_service_deinit();
    if (sfx_ret != ESP_OK) {
        ESP_LOGW(TAG, "SFX service deinit during local app cleanup failed: %s", esp_err_to_name(sfx_ret));
    }
}

static void app_center_release_after_launch_if_available(void) {
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    app_center_release_after_launch();
#endif
}

static void local_behavior_ui_open_ex(const char *state_id, const char *text, const char *anim_id, const char *sound_id,
                                      bool enable_animation) {
    LOG_HEAP_STATE("before_local_behavior_ui");
    const app_ui_mode_t required_mode = enable_animation ? APP_UI_MODE_ANIMATION : APP_UI_MODE_TEXT_ONLY;
    if (app_ui_mode_core_get(&s_app_ui_mode) != required_mode ||
        (enable_animation && !app_ui_mode_core_surface_bound(&s_app_ui_mode))) {
        const int ui_result =
            enable_animation ? app_animation_ui_init_with_text(text) : app_animation_ui_init_text_only(text);
        if (ui_result != 0) {
            ESP_LOGE(TAG, "Behavior UI request rejected: state=%s animation=%d UI unavailable",
                     state_id != NULL ? state_id : "<null>", enable_animation ? 1 : 0);
            return;
        }
        app_center_release_after_launch_if_available();
    }
    init_runtime_inputs_and_restart_path();
    reset_ready_idle_rotation();
    if (anim_id != NULL || sound_id != NULL) {
        (void)behavior_state_set_with_resources(state_id, text, 0, anim_id, sound_id);
    } else if (text != NULL) {
        (void)behavior_state_set_with_text_style(state_id, text, 0, false);
    } else {
        (void)behavior_state_set(state_id);
    }
    lvgl_port_lock(0);
    local_exit_attach_locked();
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
    LOG_HEAP_STATE("after_local_behavior_ui");
}

static void local_behavior_ui_open(const char *state_id, const char *text, const char *anim_id, const char *sound_id) {
    local_behavior_ui_open_ex(state_id, text, anim_id, sound_id, true);
}

static void local_app_tick(void) {
    int64_t now_us = esp_timer_get_time();

    local_apply_exit_anim_protection();

    if (!s_local_exit_visible) {
        return;
    }

    if (s_local_exit_hide_at_us <= 0) {
        s_local_exit_hide_at_us = now_us + (int64_t)LOCAL_EXIT_AUTO_HIDE_MS * 1000LL;
        return;
    }

    if (s_local_exit_show_since_us <= 0) {
        s_local_exit_show_since_us = now_us;
    }

    if (now_us < s_local_exit_hide_at_us ||
        now_us < s_local_exit_show_since_us + (int64_t)LOCAL_EXIT_AUTO_HIDE_MS * 1000LL) {
        return;
    }

    lvgl_port_lock(0);
    ESP_LOGW(TAG, "Local exit auto-hide timeout reached");
    local_hide_exit();
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
}

static void local_app_on_button(void) {
    if (local_exit_is_visible()) {
        local_return_to_launcher();
        return;
    }

    lvgl_port_lock(0);
    local_show_exit();
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
}

static void ble_app_prepare_waiting_session(const char *app_name) {
    esp_err_t ret;

    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;

    ret = ble_service_reset_session();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "%s BLE session reset failed on open: %s", app_name != NULL ? app_name : "App",
                 esp_err_to_name(ret));
    }
}

static void ble_app_show_waiting_text(const char *text) {
    const char *safe_text = (text != NULL && text[0] != '\0') ? text : "Waiting BLE";

    local_behavior_ui_open_ex("standby", safe_text, "", "", false);
    /* Repeated standby requests can be coalesced by behavior_state_service, so
     * keep the BLE entry label deterministic even when reopening the app.
     */
    (void)behavior_state_set_text_style(safe_text, 0, false);
    (void)hal_display_set_text_with_style(safe_text, 0, false);
}

static void phone_control_on_network_back(void *user_ctx) {
    (void)user_ctx;
    local_queue_return_to_launcher();
}

static void phone_control_fill_network_state(factory_settings_state_t *state) {
    if (state == NULL) {
        return;
    }

    if (ble_service_get_local_mac(s_phone_control_ble_mac_text, sizeof(s_phone_control_ble_mac_text)) != ESP_OK) {
        (void)snprintf(s_phone_control_ble_mac_text, sizeof(s_phone_control_ble_mac_text), "-");
    }

    memset(state, 0, sizeof(*state));
    state->sound_percent = s_settings_volume_percent;
    state->brightness_percent = s_settings_brightness_percent;
    state->rgb_enabled = s_settings_rgb_enabled;
    state->wifi_status = "Setup";
    state->wifi_ssid = "-";
    state->wifi_ip = "-";
    state->wifi_connected = false;
    state->wifi_configured = false;
    state->ble_mac = s_phone_control_ble_mac_text;
    state->ble_connected = ble_service_is_connected();
    state->ble_advertising = ble_service_is_advertising_enabled();
    state->bluetooth_status = state->ble_connected ? "Connected" : (state->ble_advertising ? "Open" : "Closed");
    state->network_wait_title = "Phone Control over Bluetooth";
    state->network_wait_hint = "Open the app and select this BLE MAC to control.";
    state->network_connected_title = "Phone connected";
    state->network_connected_hint = "Phone Control ready.";
    state->network_use_ble_icon = true;
    state->network_ble_mac_color = 0x5AA2FF;
}

static void ble_app_on_open(void) {
    ble_app_prepare_waiting_session("BLE app");
    ble_app_show_waiting_text("Waiting BLE");
}

static void phone_control_update_waiting_network_ui(void) {
    factory_settings_state_t state;

    if (!s_phone_control_network_ui_active) {
        return;
    }

    phone_control_fill_network_state(&state);
    lvgl_port_lock(0);
    factory_settings_ui_update_state(&state);
    lvgl_port_unlock();
    s_phone_control_network_refresh_due_us =
        esp_timer_get_time() + ((int64_t)PHONE_CONTROL_NETWORK_REFRESH_MS * 1000LL);
}

static void phone_control_close_network_ui(void) {
    if (!s_phone_control_network_ui_active) {
        return;
    }

    lvgl_port_lock(0);
    app_clear_group(&s_phone_control_group);
    factory_settings_ui_reset();
    lvgl_port_unlock();
    s_phone_control_network_ui_active = false;
    s_phone_control_network_refresh_due_us = 0;
}

static void phone_control_show_waiting_network_ui(void) {
    factory_settings_state_t state;
    const factory_settings_callbacks_t callbacks = {
        .on_back = phone_control_on_network_back,
        .user_ctx = NULL,
    };

    if (s_phone_control_sleep_ui_active) {
        local_behavior_app_cleanup();
        s_phone_control_sleep_ui_active = false;
    } else {
        app_animation_ui_deinit();
    }

    phone_control_fill_network_state(&state);
    if (s_phone_control_network_ui_active) {
        phone_control_update_waiting_network_ui();
        return;
    }

    lvgl_port_lock(0);
    app_clear_group(&s_phone_control_group);
    factory_settings_ui_reset();
    factory_settings_ui_build_network_page(NULL, &state, &callbacks, &s_phone_control_group, true);
    lvgl_port_unlock();
    s_phone_control_network_ui_active = true;
    s_phone_control_network_refresh_due_us =
        esp_timer_get_time() + ((int64_t)PHONE_CONTROL_NETWORK_REFRESH_MS * 1000LL);
    ESP_LOGI(TAG, "Phone Control waiting for BLE with Settings network UI");
}

static void phone_control_show_connected_sleep_ui(const char *reason) {
    if (s_phone_control_sleep_ui_active) {
        return;
    }

    phone_control_close_network_ui();
    display_ui_set_text_suppressed(true);
    local_behavior_ui_open_ex("standby_entry", "", NULL, "", true);
    s_phone_control_sleep_ui_active = true;
    ESP_LOGI(TAG, "Phone Control connected sleep UI entered: reason=%s", reason != NULL ? reason : "none");
}

static void phone_control_app_on_open(void) {
    ble_app_prepare_waiting_session("Phone Control");
    display_ui_set_text_suppressed(true);
    s_phone_control_network_ui_active = false;
    s_phone_control_sleep_ui_active = false;
    s_phone_control_network_refresh_due_us = 0;

    if (ble_service_is_connected()) {
        phone_control_show_connected_sleep_ui("phone control open connected");
    } else {
        phone_control_show_waiting_network_ui();
    }
}

static void phone_control_app_on_tick(void) {
    const int64_t now_us = esp_timer_get_time();

    if (ble_service_is_connected()) {
        if (!s_phone_control_sleep_ui_active) {
            phone_control_show_connected_sleep_ui("phone control ble connected");
        }
        local_app_tick();
        return;
    }

    if (!s_phone_control_network_ui_active) {
        phone_control_show_waiting_network_ui();
        return;
    }

    if (s_phone_control_network_refresh_due_us <= 0 || now_us >= s_phone_control_network_refresh_due_us) {
        phone_control_update_waiting_network_ui();
    }
}

static void phone_control_app_on_button(void) {
    local_app_on_button();
}

static void phone_control_app_on_close(void) {
    (void)control_ingress_stop_manual(CONTROL_MOTION_SOURCE_BLE);
    display_ui_set_text_suppressed(false);
    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;
    phone_control_close_network_ui();
    if (s_phone_control_sleep_ui_active) {
        local_behavior_app_cleanup();
    }
    s_phone_control_sleep_ui_active = false;
    s_phone_control_network_refresh_due_us = 0;
}

static void ble_app_on_close(void) {
    (void)control_ingress_stop_manual(CONTROL_MOTION_SOURCE_BLE);
    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;
    local_behavior_app_cleanup();
}

static void voice_app_on_open(void) {
    const char *open_reason = active_app_is(CLIENT_APP_ID) ? "client.app open" : "voice.app open";

    s_voice_ready_ui_shown = false;
    s_voice_last_touch_tap_us = 0;
    s_app_connect_action_click_pending = false;
    hal_display_voice_connect_status_set_action_callback(app_connect_action_clicked, NULL);
    voice_app_reset_connect_ui_state();
    sfx_service_set_voice_audio_busy(true);
    sfx_service_stop();
    voice_recorder_reset_transport();
    ws_client_set_behavior_feedback_enabled(true);
    voice_recorder_init();
    voice_runtime_request_start(open_reason);
    s_boot_completed = true;
    transport_schedule_retry(0);
    local_behavior_ui_open_ex("standby", "", "", "", false);
    voice_app_update_connect_ui_if_needed();
    (void)voice_app_enter_ready_ui_if_needed(open_reason);
    voice_diag_schedule_stable_samples();
}

static void voice_app_on_button(void) {
    if (local_exit_is_visible()) {
        local_return_to_launcher();
        return;
    }

    if (voice_app_handle_connect_action("button")) {
        return;
    }

    lvgl_port_lock(0);
    local_show_exit();
    lvgl_port_unlock();
}

static void voice_app_on_touch(int16_t x, int16_t y) {
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_ms = s_voice_last_touch_tap_us > 0 ? (now_us - s_voice_last_touch_tap_us) / 1000LL : 0;
    esp_err_t ret;

    if (local_exit_is_visible()) {
        s_voice_last_touch_tap_us = 0;
        return;
    }

    if (elapsed_ms <= 0 || elapsed_ms > VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS) {
        s_voice_last_touch_tap_us = now_us;
        ESP_LOGI(TAG, "Voice touch tap armed x=%d y=%d", (int)x, (int)y);
        return;
    }

    s_voice_last_touch_tap_us = 0;

    if (voice_recorder_get_state() == VOICE_STATE_RECORDING) {
        ret = voice_recorder_request_close();
        ESP_LOGI(TAG, "Voice touch double-tap stop requested x=%d y=%d ret=%s", (int)x, (int)y, esp_err_to_name(ret));
        return;
    }

    if (s_voice_runtime_stage != VOICE_RUNTIME_STAGE_READY) {
        ESP_LOGI(TAG, "Voice touch double-tap ignored while runtime=%s x=%d y=%d",
                 voice_runtime_stage_to_string(s_voice_runtime_stage), (int)x, (int)y);
        voice_app_update_connect_ui_if_needed();
        return;
    }

    ret = voice_recorder_request_open();
    ESP_LOGI(TAG, "Voice touch double-tap start requested x=%d y=%d ret=%s", (int)x, (int)y, esp_err_to_name(ret));
}

static void voice_app_stop_transport(const char *reason) {
    const char *safe_reason = reason != NULL ? reason : "voice app close";

    ESP_LOGI(TAG, "Stopping Voice app cloud transport: %s", safe_reason);
    ws_client_set_behavior_feedback_enabled(true);
    transport_abort_discovery_request(safe_reason);
    if (!transport_wait_for_discovery_idle(CLOUD_DISCOVERY_CANCEL_WAIT_MS)) {
        ESP_LOGW(TAG, "Discovery task still exiting while stopping Voice app transport (%s)", safe_reason);
    }
    transport_stop_ws_for_resource_release(safe_reason);
    if (s_ws_stack_ready) {
        (void)transport_deinit_ws_stack(safe_reason);
    }
    voice_recorder_suspend_cloud_audio();
    voice_runtime_reset(safe_reason);
    transport_reset_cached_ws_resume_state();
    transport_clear_cached_ws_url(safe_reason);
    transport_schedule_retry(CLOUD_RETRY_DELAY_MS);
    s_transport_state = TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED;
    LOG_HEAP_STATE("after_voice_transport_stop");
}

static void voice_app_on_close(void) {
    const char *close_reason = active_app_is(CLIENT_APP_ID) ? "client.app close" : "voice.app close";

    voice_diag_cancel_stable_samples();
    s_app_connect_action_click_pending = false;
    hal_display_voice_connect_status_set_action_callback(NULL, NULL);
    hal_display_voice_connect_status_clear();
    voice_app_stop_transport(close_reason);
    reset_ready_idle_rotation();
    voice_recorder_close();
    sfx_service_set_voice_audio_busy(false);
    local_behavior_app_cleanup();
    LOG_HEAP_STATE("after_voice_app_close");
}

static uint64_t agent_app_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static void agent_app_reset_connect_ui_state(void) {
    s_agent_connect_view = VOICE_CONNECT_VIEW_NONE;
    s_agent_connect_action = APP_CONNECT_ACTION_NONE;
}

static voice_connect_view_t agent_app_select_connect_view(agent_runtime_stage_t stage, agent_runtime_error_t error) {
    if (error == AGENT_RUNTIME_ERROR_WIFI_NOT_READY) {
        return wifi_has_credentials() == 1 ? VOICE_CONNECT_VIEW_WIFI_FAILED : VOICE_CONNECT_VIEW_WIFI_SETUP;
    }

    switch (error) {
    case AGENT_RUNTIME_ERROR_LOW_MEMORY:
    case AGENT_RUNTIME_ERROR_AUDIO_PLAYER_START_FAILED:
    case AGENT_RUNTIME_ERROR_MIC_START_FAILED:
    case AGENT_RUNTIME_ERROR_AUDIO_QUEUE_FULL:
        return VOICE_CONNECT_VIEW_RUNTIME_DEGRADED;
    case AGENT_RUNTIME_ERROR_REALTIME_START_FAILED:
    case AGENT_RUNTIME_ERROR_REALTIME_TIMEOUT:
    case AGENT_RUNTIME_ERROR_REALTIME_CLOSED:
    case AGENT_RUNTIME_ERROR_PROTOCOL:
    case AGENT_RUNTIME_ERROR_PIPELINE_BUSY:
        return VOICE_CONNECT_VIEW_WS_FAILED;
    case AGENT_RUNTIME_ERROR_NONE:
    default:
        break;
    }

    if (wifi_has_credentials() != 1) {
        return VOICE_CONNECT_VIEW_WIFI_SETUP;
    }
    if (wifi_is_connected() != 1) {
        return s_wifi_failed_since_last_success ? VOICE_CONNECT_VIEW_WIFI_FAILED : VOICE_CONNECT_VIEW_WIFI_CONNECTING;
    }
    if (stage == AGENT_RUNTIME_STAGE_FAILED || stage == AGENT_RUNTIME_STAGE_DEGRADED) {
        return VOICE_CONNECT_VIEW_WS_FAILED;
    }
    return VOICE_CONNECT_VIEW_WS_CONNECTING;
}

static void agent_app_apply_connect_view(voice_connect_view_t view) {
    const char *title = "Connecting WebSocket";
    const char *detail = "Preparing cloud session";
    const char *action = "";
    bool show_spinner = true;
    bool alert = false;
    app_connect_action_t connect_action = APP_CONNECT_ACTION_NONE;

    switch (view) {
    case VOICE_CONNECT_VIEW_WIFI_SETUP:
        title = "Wi-Fi required";
        detail = "Configure Wi-Fi in Settings";
        action = "Button: Open Settings";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_SETTINGS;
        break;

    case VOICE_CONNECT_VIEW_WIFI_CONNECTING:
        title = "Connecting Wi-Fi";
        detail = "Waiting for network before session";
        break;

    case VOICE_CONNECT_VIEW_WIFI_FAILED:
        title = "Check Wi-Fi";
        detail = "Saved Wi-Fi is not connected";
        action = "Button: Open Settings";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_SETTINGS;
        break;

    case VOICE_CONNECT_VIEW_WS_FAILED:
        title = "WebSocket failed";
        detail = "Cloud session did not become ready";
        action = "Button: Retry";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_RETRY;
        break;

    case VOICE_CONNECT_VIEW_RUNTIME_DEGRADED:
        title = "Session unavailable";
        detail = "Runtime failed to start";
        action = "Button: Retry";
        show_spinner = false;
        alert = true;
        connect_action = APP_CONNECT_ACTION_RETRY;
        break;

    case VOICE_CONNECT_VIEW_WS_CONNECTING:
    case VOICE_CONNECT_VIEW_NONE:
    default:
        break;
    }

    if (view == s_agent_connect_view && connect_action == s_agent_connect_action) {
        return;
    }

    if (hal_display_voice_connect_status_set(title, detail, action, show_spinner, alert) == 0) {
        s_agent_connect_view = view;
        s_agent_connect_action = connect_action;
        ESP_LOGI(TAG, "Agent connect UI view=%d action=%d", (int)view, (int)connect_action);
    }
}

static void agent_app_show_connect_view(agent_runtime_stage_t stage, agent_runtime_error_t error) {
    if (!active_app_is("agent.app") || !s_ui_ready || local_exit_is_visible()) {
        return;
    }
    agent_app_apply_connect_view(agent_app_select_connect_view(stage, error));
}

static const char *agent_app_choose_ready_idle_anim(void) {
    int available[READY_IDLE_VARIANT_COUNT] = {0};
    int available_count = collect_ready_idle_variants(available);
    int filtered[READY_IDLE_VARIANT_COUNT] = {0};
    int filtered_count = 0;
    int selected;

    if (available_count <= 0) {
        s_agent_ready_idle_last_variant_index = -1;
        return "standby";
    }

    for (int i = 0; i < available_count; ++i) {
        if (available_count > 1 && available[i] == s_agent_ready_idle_last_variant_index) {
            continue;
        }
        filtered[filtered_count++] = available[i];
    }

    selected = filtered_count > 0 ? filtered[esp_random() % (uint32_t)filtered_count]
                                  : available[esp_random() % (uint32_t)available_count];
    (void)animation_prefetch_hint(ready_idle_variant_type(selected));
    s_agent_ready_idle_last_variant_index = selected;
    return ready_idle_variant_name(selected);
}

static void agent_app_schedule_ready_idle_variant_switch(void) {
    s_agent_ready_idle_next_switch_us = esp_timer_get_time() + (int64_t)READY_IDLE_STANDBY_TIMEOUT_MS * 1000LL;
}

static void agent_app_prefetch_listening_anim(const char *reason) {
    if (animation_prefetch_hint(EMOJI_ANIM_LISTENING) == ANIMATION_SERVICE_OK) {
        ESP_LOGI(TAG, "Agent listening animation prefetch requested: reason=%s", reason != NULL ? reason : "none");
    }
}

static void agent_app_prefetch_wake_anim(const char *reason) {
    if (animation_prefetch_hint(EMOJI_ANIM_STANDBY_END) == ANIMATION_SERVICE_OK) {
        ESP_LOGI(TAG, "Agent wake animation prefetch requested: reason=%s", reason != NULL ? reason : "none");
    }
}

static bool agent_app_apply_ready_idle_sleep(void) {
    esp_err_t ret = behavior_state_set_with_resources("standby_start", "", 0, NULL, "");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply agent ready sleep transition: %s", esp_err_to_name(ret));
        agent_app_schedule_ready_idle_variant_switch();
        return false;
    }

    agent_animation_flow_core_init(&s_agent_animation_flow);
    (void)agent_animation_flow_on_ready(&s_agent_animation_flow);

    s_agent_ready_idle_sleeping = false;
    s_agent_ready_idle_next_switch_us = 0;
    ESP_LOGI(TAG, "Agent ready sleep transition applied after %ums standby timeout",
             (unsigned)READY_IDLE_STANDBY_TIMEOUT_MS);
    return true;
}

static bool agent_app_enter_ready_ui_if_needed(const char *reason) {
    esp_err_t ret;
    const char *anim_id;

    if (s_local_return_to_launcher_pending || !active_app_is("agent.app") || !s_ui_ready || s_agent_ready_ui_shown) {
        return false;
    }

    if (app_animation_ui_init_with_text("") != 0) {
        ESP_LOGE(TAG, "Agent ready UI cannot start without an animation surface");
        return false;
    }

    hal_display_voice_connect_status_clear();
    if (!s_agent_activity_seen) {
        if (agent_animation_flow_on_ready(&s_agent_animation_flow) != AGENT_ANIMATION_ACTION_STANDBY_ENTRY) {
            ESP_LOGE(TAG, "Agent animation flow rejected standby entry");
            return false;
        }
        ret = behavior_state_set_with_resources("standby_entry", "", 0, NULL, "");
    } else {
        anim_id = agent_app_choose_ready_idle_anim();
        ret = behavior_state_set_with_resources("standby", "", 0, anim_id, "");
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enter agent ready UI: %s", esp_err_to_name(ret));
        return false;
    }
    (void)hal_display_set_text_with_style("", 0, false);
    s_agent_ready_ui_shown = true;
    agent_app_reset_connect_ui_state();
    reset_ready_idle_rotation();
    if (s_agent_activity_seen) {
        s_agent_ready_idle_sleeping = false;
        agent_app_schedule_ready_idle_variant_switch();
        agent_app_prefetch_listening_anim("agent ready idle");
    } else {
        s_agent_ready_idle_sleeping = false;
        s_agent_ready_idle_next_switch_us = 0;
    }
    ESP_LOGI(TAG, "Agent ready UI entered: reason=%s", reason != NULL ? reason : "none");
    return true;
}

static void agent_app_submit_sleep_out(void) {
    esp_err_t result = behavior_state_set_with_resources("listening_wake", "", 0, NULL, "");
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Agent sleep-out submit failed: %s", esp_err_to_name(result));
        (void)agent_animation_flow_on_event(&s_agent_animation_flow, AGENT_ANIMATION_CLIP_STANDBY_END,
                                            AGENT_ANIMATION_EVENT_FAILED);
        agent_runtime_fail_wake("Agent sleep-out submit failed");
    }
}

static bool sdk_control_ui_is_active_locked(void) {
    lv_obj_t *active_screen = lv_disp_get_scr_act(NULL);

    return s_sdk_control_screen != NULL && active_screen == s_sdk_control_screen &&
           sdk_control_ui_is_attached(&s_sdk_control_ui, s_sdk_control_screen);
}

static bool sdk_control_ensure_control_ui(void) {
    bool active;
    bool built = true;

    lvgl_port_lock(0);
    active = sdk_control_ui_is_active_locked();
    lvgl_port_unlock();
    if (active) {
        return true;
    }

    local_app_cleanup();
    app_animation_ui_deinit();
    display_ui_set_text_suppressed(true);
    lvgl_port_lock(0);
    sdk_control_ui_reset(&s_sdk_control_ui);
    s_sdk_control_screen = NULL;
    app_replace_screen(&s_sdk_control_screen, 0x030504);
    if (s_sdk_control_screen == NULL || !sdk_control_ui_build(&s_sdk_control_ui, s_sdk_control_screen)) {
        built = false;
    } else {
        local_exit_attach_locked();
    }
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
    if (built) {
        app_center_release_after_launch_if_available();
        init_runtime_inputs_and_restart_path();
    }
    return built;
}

static void sdk_control_show_pairing_code(const char *pairing_code, bool reconnecting) {
    if (!sdk_control_ensure_control_ui()) {
        ESP_LOGE(TAG, "Python SDK pairing UI could not be created");
        return;
    }
    lvgl_port_lock(0);
    sdk_control_ui_show_pairing(&s_sdk_control_ui, pairing_code, reconnecting);
    lvgl_port_unlock();
}

static void sdk_control_show_connected(void) {
    if (!sdk_control_ensure_control_ui()) {
        ESP_LOGE(TAG, "Python SDK connected UI could not be created");
        return;
    }
    lvgl_port_lock(0);
    sdk_control_ui_show_connected(&s_sdk_control_ui);
    lvgl_port_unlock();
}

static void sdk_control_show_error(sdk_control_app_ui_error_t error) {
    const char *headline = "Unable to start";
    const char *detail = "Please reopen the app";

    if (error == SDK_CONTROL_APP_UI_ERROR_STOPPING) {
        headline = "Still stopping";
        detail = "Wait a moment, then reopen the app";
    }
    if (!sdk_control_ensure_control_ui()) {
        ESP_LOGE(TAG, "Python SDK error UI could not be created");
        return;
    }
    lvgl_port_lock(0);
    sdk_control_ui_show_error(&s_sdk_control_ui, headline, detail);
    lvgl_port_unlock();
}

static void sdk_control_restore_control_ui(void) {
    sdk_control_show_connected();
}

static bool sdk_control_prepare_animation(void) {
    local_app_cleanup();
    display_ui_set_text_suppressed(false);
    sdk_control_ui_reset(&s_sdk_control_ui);
    s_sdk_control_screen = NULL;
    if (app_animation_ui_init_with_text("") != 0) {
        sdk_control_restore_control_ui();
        return false;
    }
    app_center_release_after_launch_if_available();
    init_runtime_inputs_and_restart_path();
    lvgl_port_lock(0);
    local_exit_attach_locked();
    lvgl_port_unlock();
    local_apply_exit_anim_protection();
    return app_ui_mode_core_surface_bound(&s_app_ui_mode);
}

static void sdk_control_close_ui(void) {
    display_ui_set_text_suppressed(false);
    local_behavior_app_cleanup();
    sdk_control_ui_reset(&s_sdk_control_ui);
    s_sdk_control_screen = NULL;
}

static void configure_sdk_control_app(void) {
    const sdk_control_app_ui_t ui = {
        .show_pairing_code = sdk_control_show_pairing_code,
        .show_connected = sdk_control_show_connected,
        .show_error = sdk_control_show_error,
        .prepare_animation = sdk_control_prepare_animation,
        .restore_control_ui = sdk_control_restore_control_ui,
        .close_ui = sdk_control_close_ui,
        .tick_ui = local_app_tick,
        .on_button = local_app_on_button,
    };
    sdk_control_app_configure(&ui);
}

static void agent_app_reconcile_listening_commit(void) {
    animation_snapshot_t snapshot;
    const char *current_state;

    if (agent_animation_flow_phase(&s_agent_animation_flow) != AGENT_ANIMATION_PHASE_LISTENING_WAIT_COMMIT) {
        return;
    }
    current_state = behavior_state_get_current();
    if (current_state == NULL || strcmp(current_state, "listening") != 0 ||
        animation_get_snapshot(&snapshot) != ANIMATION_SERVICE_OK || snapshot.visible_type != EMOJI_ANIM_LISTENING) {
        return;
    }

    agent_animation_action_t action = agent_animation_flow_on_event(
        &s_agent_animation_flow, AGENT_ANIMATION_CLIP_LISTENING, AGENT_ANIMATION_EVENT_COMMITTED);
    if (action == AGENT_ANIMATION_ACTION_COMPLETE_WAKE) {
        ESP_LOGW(TAG, "Agent wake reconciled from visible listening snapshot");
        agent_runtime_complete_wake(agent_app_now_ms());
    }
}

static void agent_app_animation_flow_tick(void) {
    behavior_animation_event_t observed;

    while (behavior_state_poll_animation_event(&observed)) {
        agent_animation_clip_t clip;
        agent_animation_event_t event;
        agent_animation_action_t action;

        if (strcmp(observed.state_id, "standby_entry") == 0 || strcmp(observed.state_id, "standby_start") == 0) {
            clip = AGENT_ANIMATION_CLIP_STANDBY_ENTRY;
        } else if (strcmp(observed.state_id, "standby_loop") == 0) {
            clip = AGENT_ANIMATION_CLIP_STANDBY_LOOP;
        } else if (strcmp(observed.state_id, "listening_wake") == 0) {
            clip = AGENT_ANIMATION_CLIP_STANDBY_END;
        } else if (strcmp(observed.state_id, "listening") == 0) {
            clip = AGENT_ANIMATION_CLIP_LISTENING;
        } else {
            continue;
        }

        switch (observed.event.type) {
        case ANIMATION_EVENT_COMMITTED:
            event = AGENT_ANIMATION_EVENT_COMMITTED;
            break;
        case ANIMATION_EVENT_COMPLETED:
            event = AGENT_ANIMATION_EVENT_COMPLETED;
            break;
        case ANIMATION_EVENT_PREEMPTED:
        case ANIMATION_EVENT_CANCELLED:
        case ANIMATION_EVENT_FAILED:
            event = AGENT_ANIMATION_EVENT_FAILED;
            break;
        case ANIMATION_EVENT_ACCEPTED:
        case ANIMATION_EVENT_PREPARING:
        case ANIMATION_EVENT_CYCLE_COMPLETED:
        default:
            ESP_LOGD(TAG, "Ignoring non-observable Agent animation event type=%d ticket=%lu", (int)observed.event.type,
                     (unsigned long)observed.event.ticket);
            continue;
        }
        action = agent_animation_flow_on_event(&s_agent_animation_flow, clip, event);
        if (action == AGENT_ANIMATION_ACTION_STANDBY_END) {
            agent_app_submit_sleep_out();
        } else if (action == AGENT_ANIMATION_ACTION_SLEEPING) {
            agent_runtime_mark_sleeping();
            s_agent_ready_idle_sleeping = true;
            agent_app_prefetch_wake_anim("agent sleep loop committed");
        } else if (action == AGENT_ANIMATION_ACTION_COMPLETE_WAKE) {
            agent_runtime_complete_wake(agent_app_now_ms());
        } else if (action == AGENT_ANIMATION_ACTION_ERROR) {
            (void)behavior_state_set("error");
            if (agent_runtime_get_stage() == AGENT_RUNTIME_STAGE_WAKING) {
                agent_runtime_fail_wake("Agent animation transition failed");
            }
        }
    }
    agent_app_reconcile_listening_commit();
}

static void agent_app_update_ready_idle_variant(void) {
    const char *current_state;
    const char *anim_id;
    int64_t now_us;

    if (!active_app_is("agent.app") || local_exit_is_visible() || !s_agent_ready_ui_shown || !s_agent_activity_seen ||
        agent_runtime_get_stage() != AGENT_RUNTIME_STAGE_READY) {
        return;
    }
    if (s_agent_ready_idle_sleeping) {
        return;
    }

    now_us = esp_timer_get_time();
    if (s_agent_ready_idle_next_switch_us == 0) {
        agent_app_schedule_ready_idle_variant_switch();
        return;
    }
    if (now_us < s_agent_ready_idle_next_switch_us) {
        return;
    }

    current_state = behavior_state_get_current();
    if (current_state != NULL && strcmp(current_state, "standby") != 0) {
        agent_app_schedule_ready_idle_variant_switch();
        return;
    }

    (void)anim_id;
    agent_app_prefetch_listening_anim("agent ready idle variant");
    (void)agent_app_apply_ready_idle_sleep();
}

static void agent_app_show_activity(const char *state_id, const char *anim_id) {
    if (!active_app_is("agent.app")) {
        return;
    }

    hal_display_voice_connect_status_clear();
    agent_app_reset_connect_ui_state();
    s_agent_activity_seen = true;
    s_agent_ready_ui_shown = false;
    s_agent_ready_idle_next_switch_us = 0;
    s_agent_ready_idle_sleeping = false;
    (void)behavior_state_set_with_resources(state_id, "", 0, anim_id, "");
}

static void agent_app_clear_activity_on_failure(void) {
    if (!active_app_is("agent.app")) {
        return;
    }

    agent_audio_player_abort();
    (void)behavior_state_set_with_resources("standby", "", 0, "standby", "");
}

static void agent_app_retry_connect(const char *reason) {
    if (!active_app_is("agent.app")) {
        return;
    }

    ESP_LOGI(TAG, "Agent connect retry requested: %s", reason != NULL ? reason : "none");
    s_agent_activity_seen = false;
    s_agent_ready_ui_shown = false;
    s_agent_ready_idle_next_switch_us = 0;
    s_agent_ready_idle_sleeping = false;
    agent_app_reset_connect_ui_state();
    agent_runtime_retry(agent_app_now_ms(), "agent connect retry");
    agent_app_show_connect_view(AGENT_RUNTIME_STAGE_PENDING, AGENT_RUNTIME_ERROR_NONE);
}

static bool agent_app_handle_connect_action(const char *reason) {
    if (s_agent_connect_action == APP_CONNECT_ACTION_RETRY) {
        agent_app_retry_connect(reason);
        return true;
    }

    if (s_agent_connect_action == APP_CONNECT_ACTION_SETTINGS) {
        if (watcher_app_open("settings.app") != ESP_OK) {
            ESP_LOGW(TAG, "Agent connect UI failed to open Settings");
        }
        return true;
    }

    return false;
}

static void agent_app_connect_action_clicked(void *user_ctx) {
    (void)user_ctx;
    s_agent_connect_action_click_pending = true;
}

static void agent_app_process_connect_action_click(void) {
    if (!s_agent_connect_action_click_pending) {
        return;
    }

    s_agent_connect_action_click_pending = false;
    if (!active_app_is("agent.app")) {
        return;
    }

    (void)agent_app_handle_connect_action("touch");
}

static void agent_app_set_local_sfx_suppressed(bool suppressed, const char *reason) {
    sfx_service_set_cloud_audio_busy(suppressed);
    ESP_LOGI(TAG, "Agent local sfx %s: reason=%s", suppressed ? "suppressed" : "resumed",
             reason != NULL ? reason : "none");
}

static void agent_app_stage_changed(agent_runtime_stage_t stage, agent_runtime_error_t error, const char *text,
                                    void *user_ctx) {
    (void)user_ctx;
    (void)text;
    ESP_LOGI(TAG, "Agent runtime stage=%s error=%s text=%s", agent_runtime_stage_name(stage),
             agent_runtime_error_name(error), text != NULL ? text : "");
    if (stage == AGENT_RUNTIME_STAGE_LISTENING || stage == AGENT_RUNTIME_STAGE_STOPPED) {
        voice_recorder_set_recording_permitted(true);
    } else {
        voice_recorder_set_recording_permitted(false);
    }
    switch (stage) {
    case AGENT_RUNTIME_STAGE_PENDING:
    case AGENT_RUNTIME_STAGE_REALTIME_CONNECTING:
        agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));
        agent_app_show_connect_view(stage, error);
        break;
    case AGENT_RUNTIME_STAGE_AUDIO_STARTING:
        agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));
        agent_app_show_connect_view(stage, error);
        break;
    case AGENT_RUNTIME_STAGE_READY:
        agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));
        (void)agent_app_enter_ready_ui_if_needed("agent runtime ready");
        break;
    case AGENT_RUNTIME_STAGE_SLEEPING:
        agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));
        break;
    case AGENT_RUNTIME_STAGE_WAKING: {
        agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));
        const agent_animation_action_t action = agent_animation_flow_on_wake(&s_agent_animation_flow);
        if (action == AGENT_ANIMATION_ACTION_STANDBY_END) {
            agent_app_submit_sleep_out();
        } else if (action == AGENT_ANIMATION_ACTION_COMPLETE_WAKE) {
            agent_runtime_complete_wake(agent_app_now_ms());
        }
        break;
    }
    case AGENT_RUNTIME_STAGE_LISTENING:
        agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));
        if (behavior_state_get_current() == NULL || strcmp(behavior_state_get_current(), "listening") != 0) {
            agent_app_show_activity("listening", "listening");
        }
        break;
    case AGENT_RUNTIME_STAGE_THINKING:
        agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));
        ESP_LOGI(TAG, "Agent waiting for a response; preserving current behavior state");
        break;
    case AGENT_RUNTIME_STAGE_SPEAKING:
        agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));
        agent_app_show_activity("speaking", "speaking");
        break;
    case AGENT_RUNTIME_STAGE_FAILED:
    case AGENT_RUNTIME_STAGE_DEGRADED:
        agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));
        if (error != AGENT_RUNTIME_ERROR_ANIMATION_TRANSITION_FAILED) {
            agent_app_clear_activity_on_failure();
        }
        s_agent_ready_ui_shown = false;
        agent_app_show_connect_view(stage, error);
        break;
    case AGENT_RUNTIME_STAGE_STOPPED:
        agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));
        break;
    default:
        break;
    }
}

static bool agent_voice_transport_ready(void *user_ctx) {
    (void)user_ctx;
    return realtime_client_is_ready();
}

static void agent_voice_transport_abort(void *user_ctx) {
    (void)user_ctx;
    agent_audio_player_abort();
    (void)realtime_client_cancel_response("agent barge-in");
}

static int agent_voice_transport_send_audio(const uint8_t *data, int len, void *user_ctx) {
    (void)user_ctx;
    return realtime_client_send_audio_pcm(data, (size_t)len) == ESP_OK ? 0 : -1;
}

static int agent_voice_transport_send_audio_end(void *user_ctx) {
    (void)user_ctx;
    return realtime_client_finish_turn() == ESP_OK ? 0 : -1;
}

static int agent_voice_transport_cancel_audio(void *user_ctx) {
    (void)user_ctx;
    return realtime_client_cancel_response("audio upload failed") == ESP_OK ? 0 : -1;
}

static const voice_transport_t s_agent_voice_transport = {
    .is_ready = agent_voice_transport_ready,
    .abort_playback = agent_voice_transport_abort,
    .send_audio = agent_voice_transport_send_audio,
    .send_audio_end = agent_voice_transport_send_audio_end,
    .cancel_audio = agent_voice_transport_cancel_audio,
    .user_ctx = NULL,
};

static bool agent_voice_close_transport_ready(void *user_ctx) {
    (void)user_ctx;
    return false;
}

static void agent_voice_close_transport_abort(void *user_ctx) {
    (void)user_ctx;
}

static int agent_voice_close_transport_send_audio(const uint8_t *data, int len, void *user_ctx) {
    (void)data;
    (void)len;
    (void)user_ctx;
    return -1;
}

static int agent_voice_close_transport_send_audio_end(void *user_ctx) {
    (void)user_ctx;
    return -1;
}

static int agent_voice_close_transport_cancel_audio(void *user_ctx) {
    (void)user_ctx;
    return -1;
}

static const voice_transport_t s_agent_voice_close_transport = {
    .is_ready = agent_voice_close_transport_ready,
    .abort_playback = agent_voice_close_transport_abort,
    .send_audio = agent_voice_close_transport_send_audio,
    .send_audio_end = agent_voice_close_transport_send_audio_end,
    .cancel_audio = agent_voice_close_transport_cancel_audio,
    .user_ctx = NULL,
};

static void agent_audio_done(void *user_ctx) {
    (void)user_ctx;
    if (!active_app_is("agent.app")) {
        return;
    }
    agent_runtime_on_audio_playback_done();
}

static void agent_audio_started(void *user_ctx) {
    (void)user_ctx;
    if (!active_app_is("agent.app")) {
        return;
    }
    agent_runtime_on_audio_playback_started();
}

static void agent_realtime_ready(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_ready();
}

static void agent_realtime_transcript(const char *text, bool is_final, void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_transcript(text, is_final);
}

static void agent_realtime_assistant_text(const char *text, void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_assistant_text(text);
}

static void agent_realtime_audio(const uint8_t *pcm, size_t len, void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_audio(pcm, len);
}

static void agent_realtime_audio_done(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_audio_done();
}

static void agent_realtime_response_done(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_response_done();
}

static void agent_realtime_speech_started(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_speech_started();
}

static void agent_realtime_speech_stopped(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_speech_stopped();
}

static void agent_realtime_error(const char *message, void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_error(message);
}

static void agent_realtime_closed(void *user_ctx) {
    (void)user_ctx;
    agent_runtime_on_realtime_closed();
}

static esp_err_t agent_runtime_realtime_start(void *user_ctx) {
    (void)user_ctx;
    const realtime_client_config_t config = {
        .url = CONFIG_WATCHER_AGENT_REALTIME_URL,
        .api_key = CONFIG_WATCHER_AGENT_API_KEY,
        .voice = CONFIG_WATCHER_AGENT_VOICE,
        .instructions = "You are WatcheRobot's local voice agent. Answer briefly, warmly, and naturally.",
        .connect_timeout_ms = CONFIG_WATCHER_AGENT_CONNECT_TIMEOUT_MS,
        .response_timeout_ms = CONFIG_WATCHER_AGENT_RESPONSE_TIMEOUT_MS,
    };
    const realtime_client_callbacks_t callbacks = {
        .on_ready = agent_realtime_ready,
        .on_transcript = agent_realtime_transcript,
        .on_assistant_text = agent_realtime_assistant_text,
        .on_audio = agent_realtime_audio,
        .on_audio_done = agent_realtime_audio_done,
        .on_response_done = agent_realtime_response_done,
        .on_speech_started = agent_realtime_speech_started,
        .on_speech_stopped = agent_realtime_speech_stopped,
        .on_error = agent_realtime_error,
        .on_closed = agent_realtime_closed,
        .user_ctx = NULL,
    };

    ESP_LOGI(TAG, "Starting Agent Realtime client");
    return realtime_client_start(&config, &callbacks);
}

static bool agent_runtime_wifi_ready(void *user_ctx) {
    (void)user_ctx;
    return wifi_is_connected() == 1;
}

static bool agent_runtime_has_headroom(void *user_ctx) {
    (void)user_ctx;
    return transport_has_cloud_runtime_headroom();
}

static bool agent_runtime_is_recording(void *user_ctx) {
    (void)user_ctx;
    return voice_recorder_get_state() == VOICE_STATE_RECORDING;
}

static void agent_runtime_configure_transport(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_set_transport(&s_agent_voice_transport);
}

static void agent_runtime_reset_transport_op(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_reset_transport();
}

static void agent_runtime_prepare_transport_for_close(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_set_transport(&s_agent_voice_close_transport);
}

static void agent_runtime_set_behavior_feedback(bool enabled, void *user_ctx) {
    (void)user_ctx;
    ws_client_set_behavior_feedback_enabled(enabled);
    voice_recorder_set_behavior_feedback_enabled(enabled);
}

static void agent_runtime_recorder_init(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_init();
}

static int agent_runtime_recorder_start(void *user_ctx) {
    (void)user_ctx;
    return voice_recorder_start();
}

static void agent_runtime_recorder_close(void *user_ctx) {
    (void)user_ctx;
    (void)voice_recorder_close();
}

static esp_err_t agent_runtime_recorder_request_open(void *user_ctx) {
    (void)user_ctx;
    return voice_recorder_request_open();
}

static esp_err_t agent_runtime_recorder_request_close(void *user_ctx) {
    (void)user_ctx;
    return voice_recorder_request_close();
}

static void agent_runtime_recorder_pause_wake_word(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_pause_wake_word();
}

static void agent_runtime_recorder_resume_wake_word(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_resume_wake_word_for_sleep();
}

static void agent_runtime_recorder_suspend_cloud_audio(void *user_ctx) {
    (void)user_ctx;
    voice_recorder_suspend_cloud_audio();
}

static esp_err_t agent_runtime_audio_player_start(void *user_ctx) {
    (void)user_ctx;
    esp_err_t ret = agent_audio_player_start(agent_audio_done, NULL);
    if (ret == ESP_OK) {
        agent_audio_player_set_playback_started_callback(agent_audio_started, NULL);
    }
    return ret;
}

static void agent_runtime_audio_player_stop(void *user_ctx) {
    (void)user_ctx;
    agent_audio_player_stop();
}

static void agent_runtime_audio_player_abort(void *user_ctx) {
    (void)user_ctx;
    agent_audio_player_abort();
}

static esp_err_t agent_runtime_audio_player_enqueue(const uint8_t *pcm, size_t len, void *user_ctx) {
    (void)user_ctx;
    return agent_audio_player_enqueue(pcm, len);
}

static void agent_runtime_audio_player_mark_done(void *user_ctx) {
    (void)user_ctx;
    agent_audio_player_mark_stream_done();
}

static void agent_runtime_realtime_stop(const char *reason, void *user_ctx) {
    (void)user_ctx;
    realtime_client_stop(reason);
}

static void agent_runtime_realtime_tick(void *user_ctx) {
    (void)user_ctx;
    realtime_client_tick();
}

static bool agent_runtime_realtime_is_ready(void *user_ctx) {
    (void)user_ctx;
    return realtime_client_is_ready();
}

static esp_err_t agent_runtime_realtime_cancel(const char *reason, void *user_ctx) {
    (void)user_ctx;
    return realtime_client_cancel_response(reason);
}

static void agent_app_on_open(void) {
    const agent_runtime_config_t config = {
        .start_defer_ms = VOICE_RUNTIME_START_DEFER_MS,
        .connect_timeout_ms = CONFIG_WATCHER_AGENT_CONNECT_TIMEOUT_MS,
    };
    const agent_runtime_ops_t ops = {
        .is_wifi_ready = agent_runtime_wifi_ready,
        .has_runtime_headroom = agent_runtime_has_headroom,
        .is_recording = agent_runtime_is_recording,
        .configure_transport = agent_runtime_configure_transport,
        .reset_transport = agent_runtime_reset_transport_op,
        .prepare_transport_for_close = agent_runtime_prepare_transport_for_close,
        .set_behavior_feedback_enabled = agent_runtime_set_behavior_feedback,
        .recorder_init = agent_runtime_recorder_init,
        .recorder_start = agent_runtime_recorder_start,
        .recorder_close = agent_runtime_recorder_close,
        .recorder_request_open = agent_runtime_recorder_request_open,
        .recorder_request_close = agent_runtime_recorder_request_close,
        .recorder_pause_wake_word = agent_runtime_recorder_pause_wake_word,
        .recorder_resume_wake_word_for_sleep = agent_runtime_recorder_resume_wake_word,
        .recorder_suspend_cloud_audio = agent_runtime_recorder_suspend_cloud_audio,
        .audio_player_start = agent_runtime_audio_player_start,
        .audio_player_stop = agent_runtime_audio_player_stop,
        .audio_player_abort = agent_runtime_audio_player_abort,
        .audio_player_enqueue = agent_runtime_audio_player_enqueue,
        .audio_player_mark_stream_done = agent_runtime_audio_player_mark_done,
        .realtime_start = agent_runtime_realtime_start,
        .realtime_stop = agent_runtime_realtime_stop,
        .realtime_tick = agent_runtime_realtime_tick,
        .realtime_is_ready = agent_runtime_realtime_is_ready,
        .realtime_cancel_response = agent_runtime_realtime_cancel,
        .user_ctx = NULL,
    };
    const agent_runtime_callbacks_t callbacks = {
        .on_stage_changed = agent_app_stage_changed,
        .user_ctx = NULL,
    };

    s_boot_completed = true;
    s_agent_activity_seen = false;
    s_agent_ready_ui_shown = false;
    s_agent_connect_action_click_pending = false;
    s_agent_ready_idle_next_switch_us = 0;
    s_agent_last_touch_tap_us = 0;
    s_agent_ready_idle_sleeping = false;
    behavior_animation_event_t stale_animation_event;
    while (behavior_state_poll_animation_event(&stale_animation_event)) {
    }
    agent_animation_flow_core_init(&s_agent_animation_flow);
    hal_display_voice_connect_status_set_action_callback(agent_app_connect_action_clicked, NULL);
    agent_app_reset_connect_ui_state();
    local_behavior_ui_open_ex("standby", "", "", "", false);
    agent_runtime_init(&config, &ops, &callbacks);
    agent_runtime_open(agent_app_now_ms(), "agent.app open");
}

static void agent_app_on_tick(void) {
    local_app_tick();
    agent_app_process_connect_action_click();
    agent_runtime_tick(agent_app_now_ms());
    agent_app_animation_flow_tick();
    agent_app_update_ready_idle_variant();
}

static void agent_app_on_close(void) {
    s_agent_connect_action_click_pending = false;
    hal_display_voice_connect_status_set_action_callback(NULL, NULL);
    hal_display_voice_connect_status_clear();
    agent_runtime_close("agent.app close");
    agent_animation_flow_core_close(&s_agent_animation_flow);
    agent_app_set_local_sfx_suppressed(false, "agent.app close");
    s_agent_activity_seen = false;
    s_agent_ready_ui_shown = false;
    s_agent_ready_idle_next_switch_us = 0;
    s_agent_ready_idle_sleeping = false;
    agent_app_reset_connect_ui_state();
    local_behavior_app_cleanup();
    LOG_HEAP_STATE("after_agent_app_close");
}

static void agent_app_on_button(void) {
    if (local_exit_is_visible()) {
        local_return_to_launcher();
        return;
    }
    if (agent_app_handle_connect_action("button")) {
        return;
    }
    agent_runtime_handle_button(agent_app_now_ms());
}

static void agent_app_on_touch(int16_t x, int16_t y) {
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_ms = s_agent_last_touch_tap_us > 0 ? (now_us - s_agent_last_touch_tap_us) / 1000LL : 0;

    if (local_exit_is_visible()) {
        s_agent_last_touch_tap_us = 0;
        return;
    }

    if (elapsed_ms <= 0 || elapsed_ms > VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS) {
        s_agent_last_touch_tap_us = now_us;
        ESP_LOGI(TAG, "Agent touch tap armed x=%d y=%d", (int)x, (int)y);
        return;
    }

    s_agent_last_touch_tap_us = 0;
    if (agent_app_handle_connect_action("touch")) {
        return;
    }

    ESP_LOGI(TAG, "Agent touch double-tap handled x=%d y=%d", (int)x, (int)y);
    agent_runtime_handle_button(agent_app_now_ms());
}

static void provision_app_on_open(void) {
    provision_snapshot_t snapshot;
    const char *status_text = "Waiting BLE";

    if (provision_manager_snapshot(&s_provision_manager).generation == 0U) {
        provision_manager_init(&s_provision_manager, wifi_has_credentials() == 1, wifi_is_connected() == 1);
    }
    (void)provision_manager_resume(&s_provision_manager);
    snapshot = provision_manager_snapshot(&s_provision_manager);
    if (snapshot.state == PROVISION_STATE_READY) {
        status_text = "WiFi connected";
    } else if (snapshot.state == PROVISION_STATE_WIFI_CONFIGURING ||
               snapshot.state == PROVISION_STATE_WIFI_VALIDATING) {
        status_text = "Waiting WiFi";
    } else if (snapshot.state == PROVISION_STATE_FAILED_RETRYABLE) {
        status_text = "WiFi failed";
    } else if (snapshot.state == PROVISION_STATE_REPAIR_REQUIRED) {
        status_text = "Repair required";
    }

    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;
    s_waiting_for_wifi_provision = snapshot.state != PROVISION_STATE_READY;
    local_behavior_ui_open_ex("standby", status_text, "", "", false);
    (void)behavior_state_set_text_style(status_text, 0, false);
    (void)hal_display_set_text_with_style(status_text, 0, false);
}

static void provision_app_on_close(void) {
    (void)provision_manager_suspend(&s_provision_manager);
    s_last_ble_connected = false;
    s_ble_connected_feedback_pending = false;
    s_waiting_for_wifi_provision = false;
    local_behavior_app_cleanup();
}

static const watcher_app_t s_ble_app = {
    .id = "ble.app",
    .name = "Bluetooth",
    .icon = "ble",
    .theme_color = 0x145C65,
    .resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_BLE,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = ble_app_on_open,
    .on_tick = local_app_tick,
    .on_close = ble_app_on_close,
    .on_button = local_app_on_button,
};

static watcher_input_context_t phone_control_input_context(void) {
    return s_phone_control_network_ui_active ? WATCHER_INPUT_CONTEXT_LVGL_NAV : WATCHER_INPUT_CONTEXT_APP_ACTION;
}

static const watcher_app_t s_phone_control_app = {
    .id = PHONE_CONTROL_APP_ID,
    .name = "Phone Control",
    .icon = "phone-control",
    .theme_color = 0x145C65,
    .resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,
    .resources =
        WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_MCU_RUNTIME,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .get_input_context = phone_control_input_context,
    .on_open = phone_control_app_on_open,
    .on_tick = phone_control_app_on_tick,
    .on_close = phone_control_app_on_close,
    .on_button = phone_control_app_on_button,
};

static const watcher_app_t s_client_voice_app = {
    .id = CLIENT_APP_ID,
    .name = "Desktop Link",
    .icon = "voice",
    .theme_color = 0x5B3F8C,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD | WATCHER_APP_RESOURCE_SET_AUDIO |
                 WATCHER_APP_RESOURCE_SET_MCU_RUNTIME,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = voice_app_on_open,
    .on_tick = local_app_tick,
    .on_close = voice_app_on_close,
    .on_button = voice_app_on_button,
};

static const watcher_app_t s_voice_app = {
    .id = VOICE_APP_ID,
    .name = "Voice",
    .icon = "voice",
    .theme_color = 0x5B3F8C,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD | WATCHER_APP_RESOURCE_SET_AUDIO,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = voice_app_on_open,
    .on_tick = local_app_tick,
    .on_close = voice_app_on_close,
    .on_button = voice_app_on_button,
    .on_touch = voice_app_on_touch,
};

static const watcher_app_t s_agent_app = {
    .id = "agent.app",
    .name = "Agent",
    .icon = "agent",
    .theme_color = 0x1B6F68,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_AUDIO,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = agent_app_on_open,
    .on_tick = agent_app_on_tick,
    .on_close = agent_app_on_close,
    .on_button = agent_app_on_button,
    .on_touch = agent_app_on_touch,
};

static const watcher_app_t s_provision_app = {
    .id = "provision.app",
    .name = "Provision",
    .icon = "provision",
    .theme_color = 0x4E6B22,
    .resource_mode = WATCHER_APP_RESOURCE_PROVISIONING,
    .resources =
        WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_PROVISIONING,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = provision_app_on_open,
    .on_tick = local_app_tick,
    .on_close = provision_app_on_close,
    .on_button = local_app_on_button,
};

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_KEY_VOLUME "volume"
#define SETTINGS_NVS_KEY_BRIGHTNESS "brightness"
#define SETTINGS_NVS_KEY_RGB "rgb"
#define SETTINGS_SAVE_DEBOUNCE_MS 800
#define SETTINGS_OPEN_ID_MAX 32
#define SETTINGS_APP_CENTER_PACKAGE_DIR "/spiffs/app_center"
#define SETTINGS_UI_REFRESH_MS 1000
#define SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS 350U
#define SETTINGS_WIFI_DETAIL_BLE_RETRY_MS 750U
#define SETTINGS_WIFI_BLE_MIN_INTERNAL_FREE_BYTES (48U * 1024U)
#define SETTINGS_WIFI_BLE_MIN_INTERNAL_LARGEST_BYTES (24U * 1024U)
#define SETTINGS_RESOURCE_MANIFEST_PATH "/sdcard/resource_manifest.json"
#define SETTINGS_RESOURCE_BUNDLE_FALLBACK "Legacy / Unversioned"
#define SETTINGS_RESOURCE_MANIFEST_MAX_BYTES 4096
#define SETTINGS_LCD_SPI_TRANSFER_BYTES                                                                                \
    ((DRV_LCD_H_RES * DRV_LCD_V_RES * DRV_LCD_BITS_PER_PIXEL / 8U) / CONFIG_BSP_LCD_SPI_DMA_SIZE_DIV)
#define SETTINGS_WIFI_BLE_MIN_DMA_LARGEST_BYTES (SETTINGS_LCD_SPI_TRANSFER_BYTES + (8U * 1024U))

static lv_obj_t *s_settings_screen = NULL;
static lv_group_t *s_settings_group = NULL;
static volatile bool s_settings_pending_reboot = false;
static volatile bool s_settings_pending_factory_reset = false;
static volatile bool s_settings_pending_wifi_disconnect = false;
static volatile bool s_settings_pending_state_refresh = false;
static volatile bool s_settings_pending_wifi_detail_start = false;
static volatile bool s_settings_pending_wifi_detail_close = false;
static volatile bool s_settings_pending_ble_switch = false;
static bool s_settings_pending_ble_switch_enabled = false;
static bool s_settings_open_wifi_detail_on_open = false;
static bool s_settings_wifi_detail_active = false;
static bool s_settings_wifi_ble_provisioning_active = false;
static bool s_settings_screen_dimmed = false;
static bool s_settings_dirty = false;
static int64_t s_settings_save_due_us = 0;
static int64_t s_settings_refresh_due_us = 0;
static int64_t s_settings_wifi_detail_ble_start_due_us = 0;
static int64_t s_settings_diag_stable_1s_due_us = 0;
static int64_t s_settings_diag_stable_3s_due_us = 0;
static char s_settings_pending_open_id[SETTINGS_OPEN_ID_MAX] = "";
static char s_settings_wifi_status_text[24] = "Setup";
static char s_settings_wifi_ssid_text[64] = "-";
static char s_settings_wifi_ip_text[24] = "-";
static char s_settings_bluetooth_status_text[24] = "Open";
static char s_settings_device_name_text[48] = "WatcheRobot";
static char s_settings_firmware_text[48] = "-";
static char s_settings_idf_text[48] = "-";
static char s_settings_esp32_git_text[96] = "-";
static char s_settings_stm32_git_text[96] = "waiting";
static char s_settings_resource_bundle_text[64] = SETTINGS_RESOURCE_BUNDLE_FALLBACK;
static char s_settings_ble_mac_text[24] = "-";
static char s_settings_wifi_mac_text[24] = "-";
static bool s_settings_resource_bundle_checked = false;

static int settings_clamp_percent(int value, int min_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static void settings_format_mac(const uint8_t mac[6], char *out, size_t out_size) {
    if (out == NULL || out_size == 0 || mac == NULL) {
        return;
    }
    (void)snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void settings_format_git_ref(char *out, size_t out_size, const char *branch, const char *commit, bool dirty) {
    const char *safe_branch = (branch != NULL && branch[0] != '\0') ? branch : "unknown";
    const char *safe_commit = (commit != NULL && commit[0] != '\0') ? commit : "";
    const char *dirty_suffix = dirty ? "+dirty" : "";

    if (out == NULL || out_size == 0) {
        return;
    }

    if (safe_commit[0] == '\0' || strcmp(safe_commit, "unknown") == 0) {
        (void)snprintf(out, out_size, "%s%s", safe_branch, dirty_suffix);
        return;
    }

    (void)snprintf(out, out_size, "%s%s @ %s", safe_branch, dirty_suffix, safe_commit);
}

static void settings_update_stm32_git_ref_from_link(mcu_link_t *link) {
    mcu_link_peer_info_t info = {0};

    if (link == NULL || mcu_link_copy_peer_info(link, &info) != ESP_OK) {
        return;
    }

    if (!info.git_valid) {
        if (info.version_valid) {
            (void)snprintf(s_settings_stm32_git_text, sizeof(s_settings_stm32_git_text), "unavailable");
        }
        return;
    }

    settings_format_git_ref(s_settings_stm32_git_text, sizeof(s_settings_stm32_git_text), info.git_branch,
                            info.git_commit, info.git_dirty);
    ESP_LOGI(MCU_OBS_TAG, "evt=stm32_build_info fw=%u.%u.%u hw=%u branch=%s commit=%s dirty=%d",
             (unsigned)info.fw_major, (unsigned)info.fw_minor, (unsigned)info.fw_patch, (unsigned)info.hw_version,
             info.git_branch, info.git_commit[0] != '\0' ? info.git_commit : "<none>", info.git_dirty ? 1 : 0);
}

static void settings_load_resource_bundle_version_once(void) {
    FILE *file = NULL;
    long file_size = 0;
    char *json_text = NULL;
    cJSON *root = NULL;
    cJSON *version = NULL;
    cJSON *compatibility = NULL;
    cJSON *registry = NULL;
    cJSON *registry_count = NULL;
    cJSON *registry_fingerprint = NULL;

    if (s_settings_resource_bundle_checked) {
        return;
    }
    s_settings_resource_bundle_checked = true;
    (void)snprintf(s_settings_resource_bundle_text, sizeof(s_settings_resource_bundle_text), "%s",
                   SETTINGS_RESOURCE_BUNDLE_FALLBACK);

    file = fopen(SETTINGS_RESOURCE_MANIFEST_PATH, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "SD resource manifest unavailable: %s", SETTINGS_RESOURCE_MANIFEST_PATH);
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        ESP_LOGW(TAG, "SD resource manifest seek failed: %s", SETTINGS_RESOURCE_MANIFEST_PATH);
        return;
    }
    file_size = ftell(file);
    if (file_size <= 0 || file_size > SETTINGS_RESOURCE_MANIFEST_MAX_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        ESP_LOGW(TAG, "SD resource manifest size invalid: %ld", file_size);
        return;
    }

    json_text = (char *)malloc((size_t)file_size + 1U);
    if (json_text == NULL) {
        fclose(file);
        ESP_LOGW(TAG, "SD resource manifest allocation failed: %ld", file_size);
        return;
    }
    if (fread(json_text, 1U, (size_t)file_size, file) != (size_t)file_size) {
        free(json_text);
        fclose(file);
        ESP_LOGW(TAG, "SD resource manifest read failed: %s", SETTINGS_RESOURCE_MANIFEST_PATH);
        return;
    }
    fclose(file);
    json_text[file_size] = '\0';

    root = cJSON_Parse(json_text);
    free(json_text);
    if (root == NULL) {
        ESP_LOGW(TAG, "SD resource manifest JSON parse failed: %s", SETTINGS_RESOURCE_MANIFEST_PATH);
        return;
    }

    version = cJSON_GetObjectItem(root, "bundle_version");
    if (cJSON_IsString(version) && version->valuestring != NULL && version->valuestring[0] != '\0') {
        (void)snprintf(s_settings_resource_bundle_text, sizeof(s_settings_resource_bundle_text), "%s",
                       version->valuestring);
        ESP_LOGI(TAG, "SD resource bundle version: %s", s_settings_resource_bundle_text);
    } else {
        ESP_LOGW(TAG, "SD resource manifest missing bundle_version: %s", SETTINGS_RESOURCE_MANIFEST_PATH);
    }

    compatibility = cJSON_GetObjectItem(root, "compatibility");
    registry = cJSON_IsObject(compatibility) ? cJSON_GetObjectItem(compatibility, "animation_registry") : NULL;
    registry_count = cJSON_IsObject(registry) ? cJSON_GetObjectItem(registry, "count") : NULL;
    registry_fingerprint = cJSON_IsObject(registry) ? cJSON_GetObjectItem(registry, "fingerprint") : NULL;
    if (!cJSON_IsNumber(registry_count) || !cJSON_IsString(registry_fingerprint) ||
        registry_fingerprint->valuestring == NULL) {
        ESP_LOGW(TAG, "SD resource manifest has no animation registry compatibility metadata");
    } else if (registry_count->valueint == ANIMATION_REGISTRY_COUNT &&
               strcmp(registry_fingerprint->valuestring, ANIMATION_REGISTRY_FINGERPRINT) == 0) {
        ESP_LOGI(TAG, "SD animation registry exact match: count=%d fingerprint=%s", ANIMATION_REGISTRY_COUNT,
                 ANIMATION_REGISTRY_FINGERPRINT);
    } else if (registry_count->valueint > ANIMATION_REGISTRY_COUNT) {
        ESP_LOGW(TAG,
                 "SD animation registry is newer than firmware: sd_count=%d firmware_count=%d; appended types are "
                 "unavailable",
                 registry_count->valueint, ANIMATION_REGISTRY_COUNT);
    } else if (registry_count->valueint < ANIMATION_REGISTRY_COUNT) {
        ESP_LOGW(TAG,
                 "SD animation registry is older than firmware: sd_count=%d firmware_count=%d; manifest entries are "
                 "validated individually",
                 registry_count->valueint, ANIMATION_REGISTRY_COUNT);
    } else {
        ESP_LOGW(
            TAG,
            "SD animation registry mismatch: sd_count=%d firmware_count=%d sd_fingerprint=%s firmware_fingerprint=%s",
            registry_count->valueint, ANIMATION_REGISTRY_COUNT, registry_fingerprint->valuestring,
            ANIMATION_REGISTRY_FINGERPRINT);
    }
    cJSON_Delete(root);
}

static void settings_load_config(void) {
    nvs_handle_t handle;
    int32_t value = 0;

    s_settings_volume_percent = settings_clamp_percent(CONFIG_WATCHER_AUDIO_VOLUME, 0);
    s_settings_brightness_percent = settings_clamp_percent(SETTINGS_DEFAULT_BRIGHTNESS_PERCENT, 1);
    s_settings_rgb_enabled = true;

    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    if (nvs_get_i32(handle, SETTINGS_NVS_KEY_VOLUME, &value) == ESP_OK) {
        s_settings_volume_percent = settings_clamp_percent((int)value, 0);
    }
    if (nvs_get_i32(handle, SETTINGS_NVS_KEY_BRIGHTNESS, &value) == ESP_OK) {
        s_settings_brightness_percent = settings_clamp_percent((int)value, 1);
    }
    if (nvs_get_i32(handle, SETTINGS_NVS_KEY_RGB, &value) == ESP_OK) {
        s_settings_rgb_enabled = value != 0;
    }
    nvs_close(handle);
}

static void settings_save_config_now(void) {
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Settings NVS open failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = nvs_set_i32(handle, SETTINGS_NVS_KEY_VOLUME, s_settings_volume_percent);
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, SETTINGS_NVS_KEY_BRIGHTNESS, s_settings_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, SETTINGS_NVS_KEY_RGB, s_settings_rgb_enabled ? 1 : 0);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        s_settings_dirty = false;
        s_settings_save_due_us = 0;
    } else {
        ESP_LOGW(TAG, "Settings NVS save failed: %s", esp_err_to_name(ret));
    }
}

static void settings_mark_dirty(void) {
    s_settings_dirty = true;
    s_settings_save_due_us = esp_timer_get_time() + ((int64_t)SETTINGS_SAVE_DEBOUNCE_MS * 1000LL);
}

static void settings_save_if_due(void) {
    if (!s_settings_dirty || s_settings_save_due_us == 0 || esp_timer_get_time() < s_settings_save_due_us) {
        return;
    }
    settings_save_config_now();
}

static void settings_diag_schedule_stable_samples(void) {
    int64_t now_us = esp_timer_get_time();

    s_settings_diag_stable_1s_due_us = now_us + 1000000LL;
    s_settings_diag_stable_3s_due_us = now_us + 3000000LL;
}

static void settings_diag_log_stable_if_due(void) {
    int64_t now_us = esp_timer_get_time();

    if (s_settings_diag_stable_1s_due_us > 0 && now_us >= s_settings_diag_stable_1s_due_us) {
        s_settings_diag_stable_1s_due_us = 0;
        app_ui_diag_log_snapshot("settings_stable_1s");
    }
    if (s_settings_diag_stable_3s_due_us > 0 && now_us >= s_settings_diag_stable_3s_due_us) {
        s_settings_diag_stable_3s_due_us = 0;
        app_ui_diag_log_snapshot("settings_stable_3s");
    }
}

static void settings_apply_runtime_config(void) {
    hal_audio_set_volume((uint8_t)s_settings_volume_percent);
    (void)bsp_lcd_brightness_set(s_settings_brightness_percent);
    (void)settings_submit_rgb_light(s_settings_rgb_enabled);
}

static esp_err_t settings_ensure_head_rgb_initialized(void) {
    esp_err_t ret;

    if (s_head_rgb_initialized) {
        return ESP_OK;
    }

    ret = bsp_rgb_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Head RGB init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_head_rgb_initialized = true;
    return ESP_OK;
}

static esp_err_t settings_submit_rgb_light(bool enabled) {
    esp_err_t ret;

    ret = settings_ensure_head_rgb_initialized();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = enabled ? bsp_rgb_set(SETTINGS_HEAD_RGB_ON_R, SETTINGS_HEAD_RGB_ON_G, SETTINGS_HEAD_RGB_ON_B)
                  : bsp_rgb_set(0, 0, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Head RGB Light %s", enabled ? "on" : "off");
    }
    return ret;
}

static void settings_fill_state(factory_settings_state_t *state) {
    const esp_app_desc_t *desc = esp_app_get_description();
    uint8_t mac[6] = {0};
    bool wifi_configured = false;
    bool wifi_connected = false;
    bool ble_connected = false;
    bool ble_advertising = false;
    wifi_status_t wifi_status = WIFI_STATUS_DISCONNECTED;

    if (state == NULL) {
        return;
    }

    wifi_configured = wifi_has_credentials() == 1;
    wifi_status = wifi_get_status();
    wifi_connected = wifi_status == WIFI_STATUS_CONNECTED || wifi_is_connected() == 1;
    ble_connected = ble_service_is_connected();
    ble_advertising = ble_service_is_advertising_enabled();

    if (wifi_configured && wifi_get_saved_ssid(s_settings_wifi_ssid_text, sizeof(s_settings_wifi_ssid_text)) != 0) {
        (void)snprintf(s_settings_wifi_ssid_text, sizeof(s_settings_wifi_ssid_text), "-");
    } else if (!wifi_configured) {
        (void)snprintf(s_settings_wifi_ssid_text, sizeof(s_settings_wifi_ssid_text), "-");
    }
    if (wifi_connected && wifi_get_ip_addr(s_settings_wifi_ip_text, sizeof(s_settings_wifi_ip_text)) != 0) {
        (void)snprintf(s_settings_wifi_ip_text, sizeof(s_settings_wifi_ip_text), "-");
    } else if (!wifi_connected) {
        (void)snprintf(s_settings_wifi_ip_text, sizeof(s_settings_wifi_ip_text), "-");
    }

    if (wifi_connected) {
        (void)snprintf(s_settings_wifi_status_text, sizeof(s_settings_wifi_status_text), "Connected");
    } else if (wifi_configured && wifi_status == WIFI_STATUS_CONNECTING) {
        (void)snprintf(s_settings_wifi_status_text, sizeof(s_settings_wifi_status_text), "Connecting");
    } else if (wifi_configured) {
        (void)snprintf(s_settings_wifi_status_text, sizeof(s_settings_wifi_status_text), "Offline");
    } else {
        (void)snprintf(s_settings_wifi_status_text, sizeof(s_settings_wifi_status_text), "Setup");
    }
    if (ble_connected) {
        (void)snprintf(s_settings_bluetooth_status_text, sizeof(s_settings_bluetooth_status_text), "Connected");
    } else if (ble_advertising) {
        (void)snprintf(s_settings_bluetooth_status_text, sizeof(s_settings_bluetooth_status_text), "Open");
    } else {
        (void)snprintf(s_settings_bluetooth_status_text, sizeof(s_settings_bluetooth_status_text), "Closed");
    }
    if (desc != NULL) {
        (void)snprintf(s_settings_device_name_text, sizeof(s_settings_device_name_text), "%s", desc->project_name);
        (void)snprintf(s_settings_firmware_text, sizeof(s_settings_firmware_text), "%s", desc->version);
        (void)snprintf(s_settings_idf_text, sizeof(s_settings_idf_text), "%s", desc->idf_ver);
    }
    settings_load_resource_bundle_version_once();
    settings_format_git_ref(s_settings_esp32_git_text, sizeof(s_settings_esp32_git_text), WATCHER_BUILD_GIT_BRANCH,
                            WATCHER_BUILD_GIT_COMMIT, WATCHER_BUILD_GIT_DIRTY != 0);
    if (ble_service_get_local_mac(s_settings_ble_mac_text, sizeof(s_settings_ble_mac_text)) != ESP_OK) {
        (void)snprintf(s_settings_ble_mac_text, sizeof(s_settings_ble_mac_text), "-");
    }
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        settings_format_mac(mac, s_settings_wifi_mac_text, sizeof(s_settings_wifi_mac_text));
    } else {
        (void)snprintf(s_settings_wifi_mac_text, sizeof(s_settings_wifi_mac_text), "-");
    }

    memset(state, 0, sizeof(*state));
    state->sound_percent = s_settings_volume_percent;
    state->brightness_percent = s_settings_brightness_percent;
    state->rgb_enabled = s_settings_rgb_enabled;
    state->wifi_status = s_settings_wifi_status_text;
    state->wifi_ssid = s_settings_wifi_ssid_text;
    state->wifi_ip = s_settings_wifi_ip_text;
    state->wifi_connected = wifi_connected;
    state->wifi_configured = wifi_configured;
    state->ble_mac = s_settings_ble_mac_text;
    state->ble_connected = ble_connected;
    state->ble_advertising = ble_advertising;
    state->bluetooth_status = s_settings_bluetooth_status_text;
    state->about.device_name = s_settings_device_name_text;
    state->about.firmware_version = s_settings_firmware_text;
    state->about.idf_version = s_settings_idf_text;
    state->about.esp32_git_ref = s_settings_esp32_git_text;
    state->about.stm32_git_ref = s_settings_stm32_git_text;
    state->about.resource_bundle_version = s_settings_resource_bundle_text;
    state->about.ble_mac = s_settings_ble_mac_text;
    state->about.wifi_mac = s_settings_wifi_mac_text;
}

static void settings_request_open(const char *app_id) {
    if (app_id == NULL || app_id[0] == '\0') {
        return;
    }
    (void)snprintf(s_settings_pending_open_id, sizeof(s_settings_pending_open_id), "%s", app_id);
}

static void settings_open_wifi_detail_from_app(void) {
    s_settings_open_wifi_detail_on_open = true;
    if (watcher_app_open("settings.app") != ESP_OK) {
        s_settings_open_wifi_detail_on_open = false;
        ESP_LOGW(TAG, "App connect UI failed to open Settings Wi-Fi detail");
    }
}

static void settings_on_back(void *user_ctx) {
    (void)user_ctx;
    settings_request_open("launcher");
}

static void settings_on_reboot(void *user_ctx) {
    (void)user_ctx;
    s_settings_pending_reboot = true;
}

static void settings_request_state_refresh(void) {
    if (active_app_is("settings.app")) {
        s_settings_pending_state_refresh = true;
    }
}

static void settings_on_ble_wifi_configured(ble_service_wifi_config_event_t event) {
    if (event == BLE_SERVICE_WIFI_CONFIG_EVENT_SAVED) {
        s_provision_credentials_saved_pending = true;
        provisioning_feedback_queue_sound(PROVISIONING_FEEDBACK_SOUND_WIFI_SAVED);
        ble_provisioning_schedule_release("wifi saved");
    }
    settings_request_state_refresh();
}

static void provision_manager_dispatch(provision_event_type_t type, provision_failure_reason_t failure_reason) {
    provision_snapshot_t snapshot = provision_manager_snapshot(&s_provision_manager);
    provision_event_t event = {
        .type = type,
        .generation = snapshot.generation,
        .failure_reason = failure_reason,
    };
    provision_transition_result_t result;

    if (snapshot.generation == 0U) {
        return;
    }
    result = provision_manager_handle_event(&s_provision_manager, &event);
    if (result == PROVISION_TRANSITION_REJECTED) {
        ESP_LOGD(TAG, "Provision event rejected state=%d event=%d generation=%lu", (int)snapshot.state, (int)type,
                 (unsigned long)snapshot.generation);
    }
}

static void provision_manager_platform_tick(void) {
    wifi_status_t pending_status;
    provision_snapshot_t snapshot = provision_manager_snapshot(&s_provision_manager);

    if (snapshot.generation == 0U) {
        return;
    }

    if (s_provision_credentials_saved_pending) {
        s_provision_credentials_saved_pending = false;
        provision_manager_dispatch(PROVISION_EVENT_CREDENTIALS_RECEIVED, PROVISION_FAILURE_NONE);
    }

    if (!s_provision_wifi_status_pending_valid) {
        return;
    }
    pending_status = s_provision_wifi_status_pending;
    s_provision_wifi_status_pending_valid = false;

    if (pending_status == WIFI_STATUS_CONNECTING) {
        provision_manager_dispatch(PROVISION_EVENT_WIFI_CONNECT_STARTED, PROVISION_FAILURE_NONE);
    } else if (pending_status == WIFI_STATUS_CONNECTED) {
        provision_manager_dispatch(PROVISION_EVENT_WIFI_CONNECTED, PROVISION_FAILURE_NONE);
    }
}

static void settings_resume_wifi_if_needed(void) {
    wifi_status_t status = wifi_get_status();

    if (ble_service_is_connected() || wifi_has_credentials() != 1 || wifi_is_connected() == 1 ||
        status == WIFI_STATUS_CONNECTED || status == WIFI_STATUS_CONNECTING || wifi_is_connect_requested() == 1) {
        return;
    }

    ESP_LOGI(TAG, "Settings Wi-Fi detail is resuming saved Wi-Fi connection");
    if (wifi_resume_background() != 0) {
        ESP_LOGW(TAG, "Settings Wi-Fi detail failed to resume saved Wi-Fi connection");
    }
}

static void settings_cancel_wifi_detail_ble_start(void) {
    s_settings_pending_wifi_detail_start = false;
    s_settings_wifi_detail_ble_start_due_us = 0;
}

static void settings_schedule_wifi_detail_ble_start(uint32_t delay_ms, const char *reason) {
    if (wifi_has_credentials() == 1) {
        settings_cancel_wifi_detail_ble_start();
        return;
    }

    s_settings_pending_wifi_detail_start = true;
    s_settings_wifi_detail_ble_start_due_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000LL);
    ESP_LOGI(TAG, "Settings Wi-Fi detail scheduled BLE provisioning start (%s) delay_ms=%lu",
             reason != NULL ? reason : "no reason", (unsigned long)delay_ms);
}

static bool settings_wifi_ble_start_has_headroom(void) {
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    if (free_internal >= SETTINGS_WIFI_BLE_MIN_INTERNAL_FREE_BYTES &&
        largest_internal >= SETTINGS_WIFI_BLE_MIN_INTERNAL_LARGEST_BYTES &&
        largest_dma >= SETTINGS_WIFI_BLE_MIN_DMA_LARGEST_BYTES) {
        return true;
    }

    ESP_LOGW(TAG,
             "Deferring Settings Wi-Fi BLE provisioning due to low heap: "
             "int_free=%u int_largest=%u dma_largest=%u need=%u/%u/%u",
             (unsigned)free_internal, (unsigned)largest_internal, (unsigned)largest_dma,
             (unsigned)SETTINGS_WIFI_BLE_MIN_INTERNAL_FREE_BYTES,
             (unsigned)SETTINGS_WIFI_BLE_MIN_INTERNAL_LARGEST_BYTES, (unsigned)SETTINGS_WIFI_BLE_MIN_DMA_LARGEST_BYTES);
    return false;
}

static bool settings_lcd_dma_transfer_has_floor(void) {
    const size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    return largest_dma >= SETTINGS_LCD_SPI_TRANSFER_BYTES;
}

static void settings_stop_wifi_detail_provisioning_if_owned(const char *reason) {
    esp_err_t ret;

    if (!s_settings_wifi_ble_provisioning_active) {
        return;
    }

    s_settings_wifi_ble_provisioning_active = false;
    s_ble_provisioning_release_pending = false;
    s_ble_provisioning_release_due_us = 0;
    if (!s_ble_stack_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Settings Wi-Fi detail stopping owned BLE provisioning (%s)", reason != NULL ? reason : "no reason");
    ret = app_resource_stop_ble();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Settings Wi-Fi detail BLE provisioning stop failed: %s", esp_err_to_name(ret));
    }
    LOG_HEAP_STATE("settings_wifi_detail_ble_stop");
}

static bool settings_start_ble_provisioning_if_needed(void) {
    esp_err_t ret;
    const bool already_requested = ble_service_is_advertising_enabled();

    if (wifi_has_credentials() == 1) {
        return true;
    }

    if (ble_service_is_connected() || ble_service_is_advertising_active()) {
        return true;
    }

    if (!settings_wifi_ble_start_has_headroom()) {
        return false;
    }

    ret = app_resource_start_ble();
    if (ret == ESP_OK ||
        (ret == ESP_ERR_INVALID_STATE && (ble_service_is_connected() || ble_service_is_advertising_enabled()))) {
        if (!already_requested) {
            s_settings_wifi_ble_provisioning_active = true;
            ESP_LOGI(TAG, "Settings Wi-Fi detail started BLE provisioning advertising");
        } else {
            ESP_LOGD(TAG, "Settings Wi-Fi detail re-armed pending BLE provisioning advertising");
        }
        LOG_HEAP_STATE("settings_wifi_detail_ble_started");
        if (!settings_lcd_dma_transfer_has_floor()) {
            ESP_LOGW(TAG, "Settings Wi-Fi BLE provisioning left LCD DMA below transfer floor; releasing BLE for retry");
            settings_stop_wifi_detail_provisioning_if_owned("lcd dma floor");
            return false;
        }
        return true;
    } else {
        ESP_LOGW(TAG, "Settings Wi-Fi detail failed to start BLE provisioning advertising: %s", esp_err_to_name(ret));
        return false;
    }
}

static void settings_on_wifi(void *user_ctx) {
    (void)user_ctx;
    s_settings_wifi_detail_active = true;
    s_settings_pending_state_refresh = true;
    ESP_LOGI(TAG, "Settings Wi-Fi detail requested");
    if (wifi_has_credentials() == 1) {
        settings_cancel_wifi_detail_ble_start();
        ESP_LOGI(TAG, "Settings Wi-Fi detail keeps BLE provisioning off because Wi-Fi credentials already exist");
    } else {
        settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS, "wifi detail open");
    }
}

static void settings_on_wifi_detail_closed(void *user_ctx) {
    (void)user_ctx;
    s_settings_wifi_detail_active = false;
    settings_cancel_wifi_detail_ble_start();
    s_settings_pending_wifi_detail_close = true;
    s_settings_pending_state_refresh = true;
    ESP_LOGI(TAG, "Settings Wi-Fi detail closed");
}

static bool settings_on_wifi_disconnect(void *user_ctx) {
    (void)user_ctx;
    s_settings_pending_wifi_disconnect = true;
    ESP_LOGI(TAG, "Settings Wi-Fi disconnect requested");
    return true;
}

static void settings_perform_wifi_disconnect(void) {
    factory_settings_state_t state;

    wifi_disconnect();
    if (wifi_clear_credentials() != 0) {
        ESP_LOGW(TAG, "Settings Wi-Fi disconnect failed while clearing credentials");
    } else {
        ESP_LOGI(TAG, "Settings Wi-Fi disconnected and credentials cleared");
        s_settings_wifi_detail_active = true;
        settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS, "wifi credentials cleared");
    }
    settings_fill_state(&state);
    lvgl_port_lock(0);
    factory_settings_ui_update_state(&state);
    lvgl_port_unlock();
}

static bool settings_perform_bluetooth_change(bool enabled) {
    esp_err_t ret;

    s_ble_provisioning_release_pending = false;
    s_ble_provisioning_release_due_us = 0;

    if (enabled) {
        ret = app_resource_start_ble();
        if (ret == ESP_ERR_INVALID_STATE && (ble_service_is_connected() || ble_service_is_advertising_enabled())) {
            ret = ESP_OK;
        }
    } else {
        ret = app_resource_stop_ble();
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE Settings switch %s failed: %s", enabled ? "on" : "off", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "BLE Settings switch %s applied", enabled ? "on" : "off");
    (void)snprintf(s_settings_bluetooth_status_text, sizeof(s_settings_bluetooth_status_text), "%s",
                   enabled ? "Open" : "Closed");
    return true;
}

static bool settings_on_bluetooth_changed(bool enabled, void *user_ctx) {
    (void)user_ctx;

    s_settings_pending_ble_switch_enabled = enabled;
    s_settings_pending_ble_switch = true;
    s_settings_pending_state_refresh = true;
    (void)snprintf(s_settings_bluetooth_status_text, sizeof(s_settings_bluetooth_status_text), "%s",
                   enabled ? "Open" : "Closed");
    ESP_LOGI(TAG, "BLE Settings switch %s queued", enabled ? "on" : "off");
    return true;
}

static void settings_on_sound_changed(int value, void *user_ctx) {
    (void)user_ctx;
    s_settings_volume_percent = settings_clamp_percent(value, 0);
    hal_audio_set_volume((uint8_t)s_settings_volume_percent);
    settings_mark_dirty();
}

static void settings_on_brightness_changed(int value, void *user_ctx) {
    (void)user_ctx;
    s_settings_brightness_percent = settings_clamp_percent(value, 1);
    (void)bsp_lcd_brightness_set(s_settings_brightness_percent);
    settings_mark_dirty();
}

static bool settings_on_rgb_changed(bool enabled, void *user_ctx) {
    esp_err_t ret;

    (void)user_ctx;
    ret = settings_submit_rgb_light(enabled);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RGB Light setting apply failed: %s", esp_err_to_name(ret));
        return false;
    }
    s_settings_rgb_enabled = enabled;
    settings_mark_dirty();
    return true;
}

static void settings_on_factory_reset(void *user_ctx) {
    (void)user_ctx;
    s_settings_pending_factory_reset = true;
}

static void settings_remove_tree(const char *path) {
    DIR *dir = opendir(path);

    if (dir == NULL) {
        (void)unlink(path);
        return;
    }

    while (true) {
        struct dirent *entry = readdir(dir);
        char child[160];
        struct stat st;

        if (entry == NULL) {
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) {
            ESP_LOGW(TAG, "Skipping too-long reset path: %s/%s", path, entry->d_name);
            continue;
        }
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            settings_remove_tree(child);
        } else {
            (void)unlink(child);
        }
    }
    closedir(dir);
    (void)rmdir(path);
}

static void settings_perform_factory_reset(void) {
    esp_err_t ret;

    ESP_LOGW(TAG, "Factory reset requested for current WatcheRobot firmware settings");
    wifi_disconnect();
    (void)wifi_clear_credentials();
    settings_remove_tree(SETTINGS_APP_CENTER_PACKAGE_DIR);

    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS erase while initialized failed: %s; retrying after deinit", esp_err_to_name(ret));
        (void)nvs_flash_deinit();
        ret = nvs_flash_erase();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Factory reset NVS erase failed: %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void settings_on_open(void) {
    factory_settings_state_t state;
    const factory_settings_callbacks_t callbacks = {
        .on_back = settings_on_back,
        .on_reboot = settings_on_reboot,
        .on_wifi = settings_on_wifi,
        .on_wifi_detail_closed = settings_on_wifi_detail_closed,
        .on_wifi_disconnect = settings_on_wifi_disconnect,
        .on_bluetooth_changed = settings_on_bluetooth_changed,
        .on_sound_changed = settings_on_sound_changed,
        .on_brightness_changed = settings_on_brightness_changed,
        .on_screen_off = NULL,
        .on_rgb_changed = settings_on_rgb_changed,
        .on_factory_reset = settings_on_factory_reset,
        .user_ctx = NULL,
    };
    const bool open_wifi_detail = s_settings_open_wifi_detail_on_open;

    app_ui_diag_log_snapshot("settings_on_open_before");
    app_animation_ui_deinit();
    s_settings_open_wifi_detail_on_open = false;
    s_settings_pending_reboot = false;
    s_settings_pending_factory_reset = false;
    s_settings_pending_wifi_disconnect = false;
    s_settings_pending_state_refresh = false;
    s_settings_pending_wifi_detail_start = false;
    s_settings_pending_wifi_detail_close = false;
    s_settings_pending_ble_switch = false;
    s_settings_wifi_detail_active = false;
    s_settings_wifi_ble_provisioning_active = false;
    s_settings_pending_open_id[0] = '\0';
    s_settings_screen_dimmed = false;
    s_settings_wifi_detail_ble_start_due_us = 0;
    s_settings_refresh_due_us = esp_timer_get_time() + ((int64_t)SETTINGS_UI_REFRESH_MS * 1000LL);
    settings_fill_state(&state);
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    app_center_request_manager_snapshot_refresh();
#endif

    lvgl_port_lock(0);
    bool retain_previous_screen = s_launcher_screen_cached_for_foreground_app && s_launcher_screen != NULL &&
                                  lv_disp_get_scr_act(NULL) == s_launcher_screen;
    s_settings_screen = NULL;
    if (open_wifi_detail) {
        factory_settings_ui_build_wifi_detail_with_previous_screen_delete(NULL, &state, &callbacks,
                                                                          !retain_previous_screen);
    } else {
        factory_settings_ui_build_with_previous_screen_delete(NULL, &state, &callbacks, !retain_previous_screen);
    }
    factory_settings_ui_bind_group(&s_settings_group);
    app_ui_diag_log_snapshot_locked("settings_on_open_after");
    lvgl_port_unlock();
    settings_diag_schedule_stable_samples();
}

static void settings_restore_screen_if_dimmed(void) {
    if (!s_settings_screen_dimmed) {
        return;
    }
    (void)bsp_lcd_brightness_set(s_settings_brightness_percent);
    s_settings_screen_dimmed = false;
}

static void settings_on_tick(void) {
    const int64_t now_us = esp_timer_get_time();

    if (s_settings_pending_open_id[0] != '\0') {
        char app_id[SETTINGS_OPEN_ID_MAX];

        (void)snprintf(app_id, sizeof(app_id), "%s", s_settings_pending_open_id);
        s_settings_pending_open_id[0] = '\0';
        settings_restore_screen_if_dimmed();
        if (watcher_app_open(app_id) != ESP_OK) {
            ESP_LOGE(TAG, "Settings failed to open app: %s", app_id);
        }
        return;
    }

    if (s_settings_pending_wifi_detail_close) {
        s_settings_pending_wifi_detail_close = false;
        settings_stop_wifi_detail_provisioning_if_owned("wifi detail closed");
        s_settings_pending_state_refresh = true;
    }

    if (s_settings_pending_wifi_detail_start) {
        if (s_settings_wifi_detail_ble_start_due_us <= 0 || now_us >= s_settings_wifi_detail_ble_start_due_us) {
            s_settings_pending_wifi_detail_start = false;
            s_settings_wifi_detail_ble_start_due_us = 0;
            if (!settings_start_ble_provisioning_if_needed() && s_settings_wifi_detail_active) {
                settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_RETRY_MS, "ble start retry");
            }
            settings_resume_wifi_if_needed();
            s_settings_pending_state_refresh = true;
        }
    }

    if (s_settings_pending_ble_switch) {
        s_settings_pending_ble_switch = false;
        if (settings_perform_bluetooth_change(s_settings_pending_ble_switch_enabled)) {
            s_settings_pending_state_refresh = true;
        }
    }

    if (s_settings_pending_wifi_disconnect) {
        s_settings_pending_wifi_disconnect = false;
        settings_perform_wifi_disconnect();
    }

    if (s_settings_wifi_detail_active) {
        settings_resume_wifi_if_needed();
        if (!s_settings_pending_wifi_detail_start && wifi_has_credentials() != 1 && !ble_service_is_connected() &&
            !ble_service_is_advertising_enabled()) {
            settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_RETRY_MS, "ble inactive");
        }
    }

    if (s_settings_pending_state_refresh) {
        factory_settings_state_t state;

        s_settings_pending_state_refresh = false;
        s_settings_refresh_due_us = now_us + ((int64_t)SETTINGS_UI_REFRESH_MS * 1000LL);
        settings_fill_state(&state);
        lvgl_port_lock(0);
        factory_settings_ui_update_state(&state);
        lvgl_port_unlock();
    }

    if (s_settings_refresh_due_us > 0 && now_us >= s_settings_refresh_due_us) {
        factory_settings_state_t state;

        s_settings_refresh_due_us = now_us + ((int64_t)SETTINGS_UI_REFRESH_MS * 1000LL);
        settings_fill_state(&state);
        lvgl_port_lock(0);
        factory_settings_ui_update_state(&state);
        lvgl_port_unlock();
    }

    if (s_settings_pending_reboot) {
        s_settings_pending_reboot = false;
        settings_save_config_now();
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
    }

    if (s_settings_pending_factory_reset) {
        s_settings_pending_factory_reset = false;
        settings_perform_factory_reset();
    }

    settings_diag_log_stable_if_due();
    settings_save_if_due();
}

static void settings_on_close(void) {
    s_settings_diag_stable_1s_due_us = 0;
    s_settings_diag_stable_3s_due_us = 0;
    app_ui_diag_log_snapshot("settings_on_close_before");
    settings_restore_screen_if_dimmed();
    if (s_settings_dirty) {
        settings_save_config_now();
    }
    s_ble_provisioning_release_pending = false;
    s_ble_provisioning_release_due_us = 0;
    if (s_ble_stack_initialized) {
        (void)app_resource_stop_ble();
    }
    s_settings_wifi_ble_provisioning_active = false;
    lvgl_port_lock(0);
    app_clear_group(&s_settings_group);
    s_settings_screen = NULL;
    factory_settings_ui_reset();
    app_ui_diag_log_snapshot_locked("settings_on_close_after_reset");
    lvgl_port_unlock();
    s_settings_pending_reboot = false;
    s_settings_pending_factory_reset = false;
    s_settings_pending_wifi_disconnect = false;
    s_settings_pending_state_refresh = false;
    s_settings_pending_wifi_detail_start = false;
    s_settings_pending_wifi_detail_close = false;
    s_settings_pending_ble_switch = false;
    s_settings_open_wifi_detail_on_open = false;
    s_settings_wifi_detail_active = false;
    s_settings_wifi_detail_ble_start_due_us = 0;
    s_settings_pending_open_id[0] = '\0';
    s_settings_refresh_due_us = 0;
}

static const watcher_app_t s_settings_app = {
    .id = "settings.app",
    .name = "Settings",
    .icon = "settings",
    .theme_color = 0xA9DE2C,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
    .input_context = WATCHER_INPUT_CONTEXT_LVGL_NAV,
    .on_open = settings_on_open,
    .on_tick = settings_on_tick,
    .on_close = settings_on_close,
};

static void basic_app_on_create(void) {
    ESP_LOGI(TAG, "Basic app created");
}

static void basic_app_on_open(void) {
    if (s_ui_ready) {
        ESP_LOGI(TAG, "Basic app already open");
        return;
    }

    LOG_HEAP_STATE("before_ui_init");
    if (app_animation_ui_init() != 0) {
        ESP_LOGE(TAG, "Basic app UI init failed");
        return;
    }
    app_center_release_after_launch_if_available();
    init_runtime_inputs_and_restart_path();
    voice_recorder_reset_transport();
    voice_recorder_init();
    behavior_state_set("boot");
    wait_for_behavior_idle(STARTUP_BEHAVIOR_TIMEOUT_MS);
    behavior_state_set_text_style("BLE Ready", 0, false);
    maybe_play_ble_connected_feedback();
    apply_idle_hint_if_needed();
    LOG_HEAP_STATE("after_ui_init");

    // run_camera_boot_diag();
    LOG_HEAP_STATE("after_camera_diag");
    s_boot_completed = true;
    voice_runtime_request_start("basic open");
    transport_sync_boot_state();
    ESP_LOGI(TAG, "WatcheRobot Basic ready (transport=%s, ble=%s)", transport_state_to_string(s_transport_state),
             ble_service_is_connected() ? "connected" : "advertising");
    LOG_HEAP_STATE("ready");
}

static void basic_app_on_close(void) {
    voice_app_stop_transport("basic close");
    voice_recorder_close();
    LOG_HEAP_STATE("after_basic_app_close");
}

static const watcher_app_t s_basic_app = {
    .id = BASIC_APP_ID,
    .name = "Basic",
    .icon = "basic",
    .theme_color = 0x72E3B5,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD | WATCHER_APP_RESOURCE_SET_AUDIO,
    .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
    .on_create = basic_app_on_create,
    .on_open = basic_app_on_open,
    .on_tick = NULL,
    .on_close = basic_app_on_close,
    .on_button = NULL,
    .on_touch = NULL,
};

#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static const char *downloaded_app_json_string(cJSON *object, const char *key_a, const char *key_b, const char *key_c) {
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

static void downloaded_app_load_manifest(const char *package_path, char *name, size_t name_size, char *description,
                                         size_t description_size, char *state, size_t state_size, char *anim,
                                         size_t anim_size) {
    FILE *file = NULL;
    char *buffer = NULL;
    long length = 0;
    cJSON *root = NULL;
    const char *value = NULL;

    if (package_path == NULL || package_path[0] == '\0') {
        return;
    }

    file = fopen(package_path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Downloaded app package cannot open: %s", package_path);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    length = ftell(file);
    if (length <= 0 || length > 2048 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    buffer = (char *)calloc((size_t)length + 1, 1);
    if (buffer == NULL) {
        fclose(file);
        return;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return;
    }
    fclose(file);

    root = cJSON_ParseWithLength(buffer, (size_t)length);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Downloaded app package is not JSON manifest: %s", package_path);
        free(buffer);
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return;
    }

    value = downloaded_app_json_string(root, "name", "title", "appName");
    if (value != NULL && value[0] != '\0') {
        snprintf(name, name_size, "%s", value);
    }
    value = downloaded_app_json_string(root, "description", "desc", "message");
    if (value != NULL && value[0] != '\0') {
        snprintf(description, description_size, "%s", value);
    }
    value = downloaded_app_json_string(root, "state", "stateId", "behavior");
    if (value != NULL && value[0] != '\0') {
        snprintf(state, state_size, "%s", value);
    }
    value = downloaded_app_json_string(root, "anim", "animation", "animId");
    if (value != NULL && value[0] != '\0') {
        snprintf(anim, anim_size, "%s", value);
    }

    cJSON_Delete(root);
    free(buffer);
}

static void downloaded_app_on_open(void) {
    char name[64] = "Downloaded App";
    char description[160] = "";
    char package_path[96] = "";
    char state[32] = "standby";
    char anim[32] = "standby";
    char label[96] = {0};

    (void)app_center_get_downloaded_app_launch_info(name, sizeof(name), description, sizeof(description), package_path,
                                                    sizeof(package_path));
    downloaded_app_load_manifest(package_path, name, sizeof(name), description, sizeof(description), state,
                                 sizeof(state), anim, sizeof(anim));
    ESP_LOGI(TAG, "Opening downloaded app shell: %s", name);
    s_boot_completed = true;

    if (description[0] != '\0') {
        snprintf(label, sizeof(label), "%.*s", (int)sizeof(label) - 1, description);
    } else {
        snprintf(label, sizeof(label), "%.*s", (int)sizeof(label) - 1, name);
    }
    local_behavior_ui_open(state, label, anim, "");
    app_center_release_after_launch_if_available();

    lvgl_port_lock(0);
    local_exit_attach_locked();
    lvgl_port_unlock();
}

static void downloaded_app_on_close(void) {
    local_behavior_app_cleanup();
}

static const watcher_app_t s_downloaded_app = {
    .id = "downloaded.app",
    .name = "Downloaded App",
    .icon = "app",
    .theme_color = 0x75FFC2,
    .resource_mode = WATCHER_APP_RESOURCE_OFF,
    .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = downloaded_app_on_open,
    .on_tick = local_app_tick,
    .on_close = downloaded_app_on_close,
    .on_button = local_app_on_button,
    .on_touch = NULL,
};
#endif

static bool is_basic_app_active(void) {
    const watcher_app_t *active_app = watcher_app_get_active();
    return active_app != NULL && active_app->id != NULL && strcmp(active_app->id, BASIC_APP_ID) == 0;
}

static bool active_app_is(const char *id) {
    const watcher_app_t *active_app = watcher_app_get_active();
    return active_app != NULL && active_app->id != NULL && id != NULL && strcmp(active_app->id, id) == 0;
}

static bool app_id_is_client_voice_host(const char *app_id) {
    return app_id != NULL && (strcmp(app_id, CLIENT_APP_ID) == 0 || strcmp(app_id, VOICE_APP_ID) == 0);
}

static bool active_app_is_client_voice_host(void) {
    const watcher_app_t *active_app = watcher_app_get_active();
    return active_app != NULL && app_id_is_client_voice_host(active_app->id);
}

static bool active_app_is_voice_runtime_context(void) {
    return active_app_is_client_voice_host() || is_basic_app_active();
}

static bool power_monitor_behavior_gate(power_monitor_behavior_t behavior, const power_monitor_event_t *event,
                                        void *user_ctx) {
    const watcher_app_t *active_app = watcher_app_get_active();
    (void)event;
    (void)user_ctx;

    if (active_app == NULL) {
        return true;
    }

    ESP_LOGI(TAG, "Suppressing power behavior %d while app %s is active", (int)behavior,
             active_app->id != NULL ? active_app->id : "(unknown)");
    return false;
}

static void client_app_start_transport(void) {
    s_boot_completed = true;
    ws_client_set_behavior_feedback_enabled(true);
    voice_recorder_suspend_cloud_audio();
    s_cloud_runtime_started = false;
    transport_reset_cached_ws_resume_state();
    transport_schedule_retry(0);
}

static void client_app_stop_transport(void) {
    ws_client_set_behavior_feedback_enabled(true);
    transport_cancel_discovery("client app close");
    transport_stop_ws_for_resource_release("client app close");
    if (s_ws_stack_ready) {
        (void)transport_deinit_ws_stack("client app close");
    }
    voice_recorder_suspend_cloud_audio();
    s_cloud_runtime_started = false;
    transport_reset_cached_ws_resume_state();
    transport_clear_cached_ws_url("client app close");
    s_transport_state = TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED;
}

static bool client_app_is_transport_ready(void) {
    return ws_client_is_session_ready() != 0;
}

static bool client_app_is_transport_stale(uint32_t stale_ms) {
    return ws_client_is_session_stale(stale_ms);
}

static void client_app_restart_transport(const char *reason) {
    const char *safe_reason = reason != NULL ? reason : "client app restart";

    ws_client_invalidate_session(safe_reason);
    transport_cancel_discovery(safe_reason);
    transport_stop_ws_for_resource_release(safe_reason);
    if (s_ws_stack_ready) {
        (void)transport_deinit_ws_stack(safe_reason);
    }
    voice_recorder_suspend_cloud_audio();
    s_cloud_runtime_started = false;
    transport_reset_cached_ws_resume_state();
    transport_schedule_retry(0);
}

static void app_connect_open_wifi_gate_base_ui(void) {
    LOG_HEAP_STATE("before_app_connect_wifi_gate_ui");
    if (!s_ui_ready) {
        if (app_animation_ui_init_text_only("") != 0) {
            ESP_LOGE(TAG, "Wi-Fi gate text UI init failed");
            return;
        }
    }
    init_runtime_inputs_and_restart_path();
    reset_ready_idle_rotation();
    (void)behavior_state_set_with_resources("standby", "", 0, "", "");
    lvgl_port_lock(0);
    local_exit_attach_locked();
    lvgl_port_unlock();
    LOG_HEAP_STATE("after_app_connect_wifi_gate_ui");
}

static void client_app_open_wifi_gate_base_ui(void) {
    app_connect_open_wifi_gate_base_ui();
}

static bool client_app_show_wifi_gate(const char *app_label) {
    return app_wifi_gate_show(&s_client_wifi_gate, app_label, "Desktop Link");
}

static bool client_app_handle_wifi_gate_action(const char *reason) {
    return app_wifi_gate_handle_action(&s_client_wifi_gate, reason);
}

static void client_app_clear_wifi_gate(void) {
    app_wifi_gate_clear(&s_client_wifi_gate);
}

static void client_app_set_wifi_gate_action_enabled(bool enabled) {
    s_app_connect_action_click_pending = false;
    app_wifi_gate_reset(&s_client_wifi_gate);
    hal_display_voice_connect_status_set_action_callback(enabled ? app_connect_action_clicked : NULL, NULL);
}

static void configure_client_app(void) {
    const client_app_deps_t deps = {
        .start_transport = client_app_start_transport,
        .stop_transport = client_app_stop_transport,
        .is_transport_ready = client_app_is_transport_ready,
        .is_transport_stale = client_app_is_transport_stale,
        .restart_transport = client_app_restart_transport,
        .open_behavior_ui = local_behavior_ui_open,
        .open_wifi_gate_base_ui = client_app_open_wifi_gate_base_ui,
        .show_wifi_gate = client_app_show_wifi_gate,
        .handle_wifi_gate_action = client_app_handle_wifi_gate_action,
        .clear_wifi_gate = client_app_clear_wifi_gate,
        .set_wifi_gate_action_enabled = client_app_set_wifi_gate_action_enabled,
        .tick_local_app = local_app_tick,
        .cleanup_local_behavior_app = local_behavior_app_cleanup,
        .on_local_button = local_app_on_button,
    };

    client_app_configure(&deps);
}

#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static void app_center_open_wifi_gate_base_ui(void) {
    app_animation_ui_deinit();
    app_connect_open_wifi_gate_base_ui();
}

static bool app_center_show_wifi_gate(const char *app_label) {
    return app_wifi_gate_show(&s_app_center_wifi_gate, app_label, "App.Center");
}

static bool app_center_handle_wifi_gate_action(const char *reason) {
    return app_wifi_gate_handle_action(&s_app_center_wifi_gate, reason);
}

static void app_center_clear_wifi_gate(void) {
    app_wifi_gate_clear(&s_app_center_wifi_gate);
}

static void app_center_set_wifi_gate_action_enabled(bool enabled) {
    s_app_connect_action_click_pending = false;
    app_wifi_gate_reset(&s_app_center_wifi_gate);
    hal_display_voice_connect_status_set_action_callback(enabled ? app_connect_action_clicked : NULL, NULL);
}

static void configure_app_center(void) {
    const app_center_deps_t deps = {
        .open_wifi_gate_base_ui = app_center_open_wifi_gate_base_ui,
        .show_wifi_gate = app_center_show_wifi_gate,
        .handle_wifi_gate_action = app_center_handle_wifi_gate_action,
        .clear_wifi_gate = app_center_clear_wifi_gate,
        .set_wifi_gate_action_enabled = app_center_set_wifi_gate_action_enabled,
    };

    app_center_configure(&deps);
}

static int app_package_ws_begin(const ws_app_package_transfer_t *transfer) {
    return app_center_package_transfer_begin(transfer) == ESP_OK ? 0 : -1;
}

static int app_package_ws_write_frame(uint8_t flags, const uint8_t *payload, size_t payload_len) {
    return app_center_package_transfer_write(flags, payload, payload_len) == ESP_OK ? 0 : -1;
}

static int app_package_ws_commit(const ws_app_package_transfer_t *transfer) {
    return app_center_package_transfer_commit(transfer) == ESP_OK ? 0 : -1;
}

static void app_package_ws_abort(const ws_app_package_command_t *command, const char *reason) {
    app_center_package_transfer_abort(command, reason);
}

static int app_package_ws_install(const ws_app_package_transfer_t *transfer) {
    return app_center_package_install_from_url(transfer) == ESP_OK ? 0 : -1;
}

static int app_package_ws_open(const ws_app_package_command_t *command) {
    return app_center_package_open(command);
}

static int app_package_ws_uninstall(const ws_app_package_command_t *command) {
    return app_center_package_uninstall(command) == ESP_OK ? 0 : -1;
}

static int app_package_ws_list(const ws_app_package_command_t *command) {
    return app_center_package_send_list(command) == ESP_OK ? 0 : -1;
}

static void configure_app_package_transport(void) {
    const ws_app_package_handler_t handler = {
        .begin = app_package_ws_begin,
        .write_frame = app_package_ws_write_frame,
        .commit = app_package_ws_commit,
        .abort = app_package_ws_abort,
        .install = app_package_ws_install,
        .open = app_package_ws_open,
        .uninstall = app_package_ws_uninstall,
        .list = app_package_ws_list,
    };
    ws_client_register_app_package_handler(&handler);
}
#endif

#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static void arm_firmware_app_return_to_launcher_on_next_boot(const char *app_label) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const char *label = app_label != NULL && app_label[0] != '\0' ? app_label : "firmware app";

    if (running == NULL || next == NULL) {
        ESP_LOGE(TAG, "%s cannot arm Launcher return slot: running=%p next=%p", label, running, next);
        return;
    }

    if (next == running) {
        ESP_LOGW(TAG, "%s next OTA partition is the running slot (%s); Launcher return not changed", label,
                 running->label);
        return;
    }

    esp_err_t ret = esp_ota_set_boot_partition(next);
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "%s armed next boot back to Launcher slot: running=%s next=%s", label, running->label,
                 next->label);
    } else {
        ESP_LOGE(TAG, "%s failed to arm Launcher return slot (%s): %s", label, next->label, esp_err_to_name(ret));
    }
}
#endif

void app_main(void) {
    app_ui_mode_core_init(&s_app_ui_mode);
    int boot_frame_count = 0;
    const watcher_input_scope_t boot_input_scope = {
        .context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .owner_token = 0,
    };

    configure_runtime_log_levels();
    watcher_input_router_global_init(boot_input_scope);
    watcher_input_router_global_set_scope_provider(current_input_scope, NULL);
    mem_monitor_init();
    mem_monitor_set_context_callback(mem_monitor_fill_app_context);
    log_startup_banner();
    log_sleep_wakeup_cause();
    log_firmware_version();
    log_ble_mac_at_boot("startup");
    LOG_HEAP_STATE("app_start");

    boot_hold_if_soft_off_latched();

    /* 1. Minimal display init for boot animation */
    if (hal_display_minimal_init() != 0) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    init_runtime_inputs_and_restart_path();
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

    /* 4. App runtime services. MCU link is started on demand by robot/client mode. */
    boot_anim_set_progress(25);
    boot_anim_set_text("Runtime...");
#if defined(WATCHER_STRESS_BUILD) || defined(CONFIG_WATCHER_STRESS_BUILD)
    init_mcu_link_bootstrap();
    init_mcu_runtime_services();
    if (hal_servo_init() != ESP_OK) {
        ESP_LOGE(TAG, "Servo facade init failed");
        boot_halt_with_error("Servo init failed");
    }
    s_mcu_runtime_active = true;
#endif

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
    if (animation_service_init() != ANIMATION_SERVICE_OK) {
        ESP_LOGE(TAG, "Animation service init failed");
        boot_halt_with_error("Animation service init failed");
    }
    behavior_state_init();
    if (power_monitor_service_init() != ESP_OK) {
        ESP_LOGE(TAG, "Power monitor init failed");
        boot_halt_with_error("Power monitor init failed");
    }
    power_monitor_service_set_behavior_gate(power_monitor_behavior_gate, NULL);
    boot_prime_power_monitor_for_launcher();

#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
    LOG_HEAP_STATE("before_control_ingress_on_demand");
#endif

    ble_connection_event_init();
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    debug_app_control_init();
    if (debug_cli_start() != ESP_OK) {
        ESP_LOGW(TAG, "Debug CLI start failed");
    }
#else
    ESP_LOGI(TAG, "Debug CLI disabled by config");
#endif

    /* 5.5 BLE control + provisioning is started by BLE/Provision apps on demand. */
    boot_anim_set_progress(35);
    boot_anim_set_text("Apps...");
    LOG_HEAP_STATE("before_ble_on_demand");

    /* 6. WiFi manager only; cloud link is coordinated after boot. */
    boot_anim_set_progress(40);
    boot_anim_set_text("WiFi...");
    wifi_init();
    time_sync_service_init();
    time_sync_service_register_callback(on_time_synchronized);
    wifi_register_status_callback(on_wifi_status_changed);
    provision_manager_init(&s_provision_manager, wifi_has_credentials() == 1, wifi_is_connected() == 1);
    settings_load_config();
    settings_apply_runtime_config();
    boot_prepare_saved_wifi_before_launcher();
    boot_anim_set_progress(55);
    LOG_HEAP_STATE("after_wifi_ready");

    /* 7. Ready for Launcher; cloud transport is started by Wi-Fi apps on demand. */
    boot_anim_set_progress(100);
    boot_anim_set_text("Launcher");
    vTaskDelay(pdMS_TO_TICKS(500));
    boot_anim_finish();

    init_runtime_inputs_and_restart_path();
    watcher_app_set_resource_apply_cb(app_resource_apply);
    configure_client_app();
    configure_sdk_control_app();
#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    configure_app_center();
    configure_app_package_transport();
#endif
    ESP_ERROR_CHECK(watcher_app_register(&s_launcher_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_settings_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_ble_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_phone_control_app));
    ESP_ERROR_CHECK(watcher_app_register(sdk_control_app_get()));
    ESP_ERROR_CHECK(watcher_app_register(&s_client_voice_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_voice_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_agent_app));
    ESP_ERROR_CHECK(watcher_app_register(&s_provision_app));
#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    ESP_ERROR_CHECK(watcher_app_register(app_center_get_app()));
#endif
    ESP_ERROR_CHECK(watcher_app_register(&s_basic_app));
#if CONFIG_WATCHER_APP_CENTER_ENABLE && !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    ESP_ERROR_CHECK(watcher_app_register(&s_downloaded_app));
#endif
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    ESP_ERROR_CHECK(watcher_app_open(PHONE_CONTROL_APP_ID));
    s_boot_completed = true;
    LOG_HEAP_STATE("phone_control_firmware_ready");
    ota_service_mark_valid();
    arm_firmware_app_return_to_launcher_on_next_boot("Phone Control");
#else
    ESP_ERROR_CHECK(watcher_app_open("launcher"));
    s_boot_completed = true;
    LOG_HEAP_STATE("launcher_ready");

    /* 10. Mark OTA partition valid (prevent rollback after successful boot) */
    ota_service_mark_valid();
#endif

    /* Main loop */
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
        ble_connection_event_tick();
        ble_provisioning_release_tick();
        provision_manager_platform_tick();
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
        debug_app_control_tick();
#endif
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
        if (!s_shutdown_in_progress) {
            service_mcu_link_runtime();
        }
#endif
        if (!s_shutdown_in_progress) {
            maybe_apply_startup_green_leds();
        }
        if (!s_shutdown_in_progress) {
            watcher_app_tick();
        }
        if (!s_shutdown_in_progress) {
            provisioning_feedback_tick();
        }
        if (!s_shutdown_in_progress && watcher_input_router_global_consume_app_click()) {
            on_button_single_click_dispatch();
        } else if (s_shutdown_in_progress) {
            watcher_input_router_global_clear_pending();
        }
        if (!s_shutdown_in_progress) {
            app_connect_process_action_click();
#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
            phone_control_firmware_process_reboot_to_launcher_partition();
#endif
            process_pending_app_navigation();
        } else {
            s_local_return_to_launcher_pending = false;
        }
        bool shutdown_active = s_shutdown_in_progress;
        bool ble_app_active = !shutdown_active && active_app_is("ble.app");
        bool phone_control_app_active = !shutdown_active && active_app_is(PHONE_CONTROL_APP_ID);
        bool provision_app_active = !shutdown_active && active_app_is("provision.app");
        bool client_app_active =
            !s_local_return_to_launcher_pending && !shutdown_active && active_app_is(CLIENT_APP_ID);
        bool launcher_app_active = !shutdown_active && active_app_is("launcher");
        bool voice_app_active =
            !s_local_return_to_launcher_pending && !shutdown_active && active_app_is_voice_runtime_context();
        bool wifi_transport_app_active = client_app_active || voice_app_active;
        if (wifi_transport_app_active) {
            transport_coordinator_tick();
        }
        if (voice_app_active) {
            voice_runtime_tick();
        }
        (void)power_monitor_service_tick(launcher_app_active);
        if (ble_app_active || phone_control_app_active || provision_app_active) {
            maybe_update_local_ble_connection_feedback();
            maybe_play_ble_connected_feedback();
        }
        if (voice_app_active) {
            apply_idle_hint_if_needed();
        }
        if (voice_app_active) {
            ws_tts_timeout_check();
        }
#if !defined(WATCHER_STRESS_BUILD) && !defined(CONFIG_WATCHER_STRESS_BUILD)
        if (voice_app_active) {
            stress_mode_tick();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}
