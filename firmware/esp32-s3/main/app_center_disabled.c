#include "app_center.h"

#include <string.h>

void app_center_configure(const app_center_deps_t *deps) {
    (void)deps;
}

esp_err_t app_center_wait_for_selection(app_center_selection_t *out_selection) {
    if (out_selection != NULL) {
        *out_selection = APP_CENTER_SELECTION_NONE;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

void app_center_release_after_launch(void) {}

bool app_center_get_downloaded_app_launch_info(char *name, size_t name_size, char *description, size_t description_size,
                                               char *package_path, size_t package_path_size) {
    if (name != NULL && name_size > 0) {
        name[0] = '\0';
    }
    if (description != NULL && description_size > 0) {
        description[0] = '\0';
    }
    if (package_path != NULL && package_path_size > 0) {
        package_path[0] = '\0';
    }
    return false;
}

static esp_err_t app_center_disabled_snapshot(app_center_manager_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_get_manager_snapshot(app_center_manager_snapshot_t *out_snapshot) {
    return app_center_disabled_snapshot(out_snapshot);
}

esp_err_t app_center_get_cached_manager_snapshot(app_center_manager_snapshot_t *out_snapshot) {
    return app_center_disabled_snapshot(out_snapshot);
}

void app_center_request_manager_snapshot_refresh(void) {}

esp_err_t app_center_uninstall_app(const char *app_id, const char *name, const char *command_id) {
    (void)app_id;
    (void)name;
    (void)command_id;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_transfer_begin(const ws_app_package_transfer_t *transfer) {
    (void)transfer;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_transfer_write(uint8_t flags, const uint8_t *payload, size_t payload_len) {
    (void)flags;
    (void)payload;
    (void)payload_len;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_transfer_commit(const ws_app_package_transfer_t *transfer) {
    (void)transfer;
    return ESP_ERR_NOT_SUPPORTED;
}

void app_center_package_transfer_abort(const ws_app_package_command_t *command, const char *reason) {
    (void)command;
    (void)reason;
}

esp_err_t app_center_package_install_from_url(const ws_app_package_transfer_t *transfer) {
    (void)transfer;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_open(const ws_app_package_command_t *command) {
    (void)command;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_uninstall(const ws_app_package_command_t *command) {
    (void)command;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_center_package_send_list(const ws_app_package_command_t *command) {
    (void)command;
    return ESP_ERR_NOT_SUPPORTED;
}

const watcher_app_t *app_center_get_app(void) {
    return NULL;
}

void app_center_process_connect_action_click(void) {}

void app_center_debug_log_ui_snapshot(const char *stage) {
    (void)stage;
}
