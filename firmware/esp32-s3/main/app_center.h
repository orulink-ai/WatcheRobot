#ifndef APP_CENTER_H
#define APP_CENTER_H

#include "app_center_manager.h"
#include "esp_err.h"
#include "watcher_app_runtime.h"
#include "ws_client.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    APP_CENTER_SELECTION_NONE = 0,
    APP_CENTER_SELECTION_BASIC,
} app_center_selection_t;

#define APP_CENTER_MANAGER_MAX_APPS 8
#define APP_CENTER_MANAGER_ID_MAX 48
#define APP_CENTER_MANAGER_NAME_MAX 64
#define APP_CENTER_MANAGER_VERSION_MAX 32

typedef struct {
    char id[APP_CENTER_MANAGER_ID_MAX];
    char name[APP_CENTER_MANAGER_NAME_MAX];
    char version[APP_CENTER_MANAGER_VERSION_MAX];
    app_center_manager_app_kind_t type;
    size_t size_bytes;
    bool installed;
} app_center_manager_app_info_t;

typedef struct {
    app_center_manager_app_info_t apps[APP_CENTER_MANAGER_MAX_APPS];
    size_t app_count;
    size_t installed_count;
    bool spiffs_available;
    size_t spiffs_total_bytes;
    size_t spiffs_used_bytes;
    size_t spiffs_free_bytes;
    size_t app_center_package_bytes;
    bool firmware_app_installed;
    char firmware_app_name[APP_CENTER_MANAGER_NAME_MAX];
    char firmware_app_version[APP_CENTER_MANAGER_VERSION_MAX];
    size_t firmware_app_size_bytes;
    size_t firmware_slot_reserved_bytes;
} app_center_manager_snapshot_t;

typedef struct {
    void (*open_wifi_gate_base_ui)(void);
    bool (*show_wifi_gate)(const char *app_label);
    bool (*handle_wifi_gate_action)(const char *reason);
    void (*clear_wifi_gate)(void);
    void (*set_wifi_gate_action_enabled)(bool enabled);
} app_center_deps_t;

void app_center_configure(const app_center_deps_t *deps);
esp_err_t app_center_wait_for_selection(app_center_selection_t *out_selection);
void app_center_release_after_launch(void);
bool app_center_get_downloaded_app_launch_info(char *name, size_t name_size, char *description,
                                               size_t description_size, char *package_path,
                                               size_t package_path_size);
esp_err_t app_center_get_manager_snapshot(app_center_manager_snapshot_t *out_snapshot);
esp_err_t app_center_get_cached_manager_snapshot(app_center_manager_snapshot_t *out_snapshot);
void app_center_request_manager_snapshot_refresh(void);
esp_err_t app_center_uninstall_app(const char *app_id, const char *name, const char *command_id);
esp_err_t app_center_package_transfer_begin(const ws_app_package_transfer_t *transfer);
esp_err_t app_center_package_transfer_write(uint8_t flags, const uint8_t *payload, size_t payload_len);
esp_err_t app_center_package_transfer_commit(const ws_app_package_transfer_t *transfer);
void app_center_package_transfer_abort(const ws_app_package_command_t *command, const char *reason);
esp_err_t app_center_package_install_from_url(const ws_app_package_transfer_t *transfer);
esp_err_t app_center_package_open(const ws_app_package_command_t *command);
esp_err_t app_center_package_uninstall(const ws_app_package_command_t *command);
esp_err_t app_center_package_send_list(const ws_app_package_command_t *command);
const watcher_app_t *app_center_get_app(void);
void app_center_process_connect_action_click(void);
void app_center_debug_log_ui_snapshot(const char *stage);

#endif /* APP_CENTER_H */
