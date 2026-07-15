#include "app_center_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_manifest_uninstall_removes_package_files(void) {
    const app_center_manager_uninstall_policy_t policy =
        app_center_manager_uninstall_policy(APP_CENTER_MANAGER_APP_MANIFEST);

    assert(policy.clear_installed_state);
    assert(policy.remove_package_files);
    assert(policy.remove_metadata_file);
    assert(!policy.erase_ota_slot);
    assert(strcmp(policy.removed_message, "removed") == 0);
}

static void test_firmware_uninstall_keeps_ota_slot_for_next_overwrite(void) {
    const app_center_manager_uninstall_policy_t policy =
        app_center_manager_uninstall_policy(APP_CENTER_MANAGER_APP_FIRMWARE);

    assert(policy.clear_installed_state);
    assert(!policy.remove_package_files);
    assert(policy.remove_metadata_file);
    assert(!policy.erase_ota_slot);
    assert(strcmp(policy.removed_message, "removed. Slot will be overwritten") == 0);
}

static void test_summary_formatters_match_settings_copy(void) {
    char text[48] = {0};

    app_center_manager_format_app_count(0U, text, sizeof(text));
    assert(strcmp(text, "None") == 0);
    app_center_manager_format_app_count(1U, text, sizeof(text));
    assert(strcmp(text, "1 installed") == 0);
    app_center_manager_format_app_count(2U, text, sizeof(text));
    assert(strcmp(text, "2 installed") == 0);

    app_center_manager_format_bytes_pair(256U * 1024U, 1024U * 1024U, text, sizeof(text));
    assert(strcmp(text, "256 KB / 1.0 MB") == 0);

    app_center_manager_format_memory(64U * 1024U, 32U * 1024U, text, sizeof(text));
    assert(strcmp(text, "64 KB / 32 KB largest") == 0);
}

static void test_ota_slot_formatter(void) {
    char text[48] = {0};

    app_center_manager_format_ota_slot(NULL, false, text, sizeof(text));
    assert(strcmp(text, "Empty / available") == 0);
    app_center_manager_format_ota_slot("Phone Control", true, text, sizeof(text));
    assert(strcmp(text, "Phone Control") == 0);
}

int main(void) {
    test_manifest_uninstall_removes_package_files();
    test_firmware_uninstall_keeps_ota_slot_for_next_overwrite();
    test_summary_formatters_match_settings_copy();
    test_ota_slot_formatter();

    puts("app_center_manager tests passed");
    return 0;
}
