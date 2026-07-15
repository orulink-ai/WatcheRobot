#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *text;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    rewind(file);
    text = (char *)malloc((size_t)size + 1U);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    fclose(file);
    text[size] = '\0';
    return text;
}

static int expect_file_contains(const char *root, const char *relative, const char *needle) {
    char path[1024];
    char *text;
    int failed = 0;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    text = read_text(path);
    if (text == NULL || strstr(text, needle) == NULL) {
        fprintf(stderr, "FAIL: %s missing '%s'\n", relative, needle);
        failed = 1;
    }
    free(text);
    return failed;
}

static int expect_file_not_contains(const char *root, const char *relative, const char *needle) {
    char path[1024];
    char *text;
    int failed = 0;

    snprintf(path, sizeof(path), "%s/%s", root, relative);
    text = read_text(path);
    if (text == NULL || strstr(text, needle) != NULL) {
        fprintf(stderr, "FAIL: %s unexpectedly contains '%s'\n", relative, needle);
        failed = 1;
    }
    free(text);
    return failed;
}

int main(int argc, char **argv) {
    int failures = 0;
    const char *root;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <main-component-dir>\n", argv[0]);
        return 2;
    }
    root = argv[1];

    failures += expect_file_contains(root, "Kconfig.projbuild", "config WATCHER_APP_CENTER_ENABLE");
    failures += expect_file_contains(root, "Kconfig.projbuild", "default n");
    failures += expect_file_contains(root, "CMakeLists.txt", "if(CONFIG_WATCHER_APP_CENTER_ENABLE)");
    failures += expect_file_contains(root, "CMakeLists.txt", "app_center_disabled.c");
    failures += expect_file_contains(root, "app_center.c", "EXT_RAM_BSS_ATTR static app_center_entry_t s_entries");
    failures += expect_file_contains(root, "watcher_app_runtime.h", "WATCHER_APP_RESOURCE_SET_CLOUD");
    failures += expect_file_contains(root, "watcher_app_runtime.h", "WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE");
    failures += expect_file_contains(root, "watcher_app_runtime.c",
                                     "WATCHER_APP_RESOURCE_WIFI_ONLY, WATCHER_APP_RESOURCE_SET_WIFI_STA");
    failures += expect_file_not_contains(root, "app_main.c", "wifi_release_station()");
    failures += expect_file_contains(root, "app_main.c", "resources |= WATCHER_APP_RESOURCE_SET_WIFI_STA");
    failures += expect_file_contains(root, "app_main.c", "launcher_screen_cache_policy_allows");
    failures += expect_file_contains(root, "app_main.c", "evt=task_stack_hwm");
    failures += expect_file_contains(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                     "Behavior action reload kept last-known-good bundle after failure");
    return failures == 0 ? 0 : 1;
}
