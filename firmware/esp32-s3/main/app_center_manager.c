#include "app_center_manager.h"

#include <stdio.h>

#define APP_CENTER_MANAGER_KB (1024U)
#define APP_CENTER_MANAGER_MB (1024U * 1024U)

app_center_manager_uninstall_policy_t
app_center_manager_uninstall_policy(app_center_manager_app_kind_t kind) {
    app_center_manager_uninstall_policy_t policy = {
        .clear_installed_state = true,
        .remove_package_files = true,
        .remove_metadata_file = true,
        .erase_ota_slot = false,
        .removed_message = "removed",
    };

    if (kind == APP_CENTER_MANAGER_APP_FIRMWARE) {
        policy.remove_package_files = false;
        policy.removed_message = "removed. Slot will be overwritten";
    }
    return policy;
}

void app_center_manager_format_app_count(size_t installed_count, char *out, size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return;
    }
    if (installed_count == 0U) {
        snprintf(out, out_size, "None");
    } else if (installed_count == 1U) {
        snprintf(out, out_size, "1 installed");
    } else {
        snprintf(out, out_size, "%u installed", (unsigned int)installed_count);
    }
}

void app_center_manager_format_bytes(size_t bytes, char *out, size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return;
    }
    if (bytes >= APP_CENTER_MANAGER_MB) {
        const unsigned int whole = (unsigned int)(bytes / APP_CENTER_MANAGER_MB);
        const unsigned int tenths = (unsigned int)(((bytes % APP_CENTER_MANAGER_MB) * 10U) / APP_CENTER_MANAGER_MB);
        snprintf(out, out_size, "%u.%u MB", whole, tenths);
    } else if (bytes >= APP_CENTER_MANAGER_KB) {
        snprintf(out, out_size, "%u KB", (unsigned int)((bytes + APP_CENTER_MANAGER_KB - 1U) / APP_CENTER_MANAGER_KB));
    } else {
        snprintf(out, out_size, "%u B", (unsigned int)bytes);
    }
}

void app_center_manager_format_bytes_pair(size_t used_bytes, size_t total_bytes, char *out, size_t out_size) {
    char used_text[24] = {0};
    char total_text[24] = {0};

    if (out == NULL || out_size == 0U) {
        return;
    }
    app_center_manager_format_bytes(used_bytes, used_text, sizeof(used_text));
    app_center_manager_format_bytes(total_bytes, total_text, sizeof(total_text));
    snprintf(out, out_size, "%s / %s", used_text, total_text);
}

void app_center_manager_format_memory(size_t free_bytes, size_t largest_bytes, char *out, size_t out_size) {
    char free_text[24] = {0};
    char largest_text[24] = {0};

    if (out == NULL || out_size == 0U) {
        return;
    }
    app_center_manager_format_bytes(free_bytes, free_text, sizeof(free_text));
    app_center_manager_format_bytes(largest_bytes, largest_text, sizeof(largest_text));
    snprintf(out, out_size, "%s / %s largest", free_text, largest_text);
}

void app_center_manager_format_ota_slot(const char *app_name, bool installed, char *out, size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return;
    }
    if (installed) {
        snprintf(out, out_size, "%s", app_name != NULL && app_name[0] != '\0' ? app_name : "Firmware app");
    } else {
        snprintf(out, out_size, "Empty / available");
    }
}
