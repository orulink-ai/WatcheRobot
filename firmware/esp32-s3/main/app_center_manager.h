#ifndef APP_CENTER_MANAGER_H
#define APP_CENTER_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    APP_CENTER_MANAGER_APP_MANIFEST = 0,
    APP_CENTER_MANAGER_APP_FIRMWARE,
} app_center_manager_app_kind_t;

typedef struct {
    bool clear_installed_state;
    bool remove_package_files;
    bool remove_metadata_file;
    bool erase_ota_slot;
    const char *removed_message;
} app_center_manager_uninstall_policy_t;

app_center_manager_uninstall_policy_t
app_center_manager_uninstall_policy(app_center_manager_app_kind_t kind);
void app_center_manager_format_app_count(size_t installed_count, char *out, size_t out_size);
void app_center_manager_format_bytes(size_t bytes, char *out, size_t out_size);
void app_center_manager_format_bytes_pair(size_t used_bytes, size_t total_bytes, char *out, size_t out_size);
void app_center_manager_format_memory(size_t free_bytes, size_t largest_bytes, char *out, size_t out_size);
void app_center_manager_format_ota_slot(const char *app_name, bool installed, char *out, size_t out_size);

#endif /* APP_CENTER_MANAGER_H */
