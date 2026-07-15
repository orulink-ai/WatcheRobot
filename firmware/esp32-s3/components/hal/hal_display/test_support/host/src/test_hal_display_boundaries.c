#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *content;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    content = (char *)malloc((size_t)size + 1U);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(content, 1U, (size_t)size, file) != (size_t)size) {
        free(content);
        fclose(file);
        return NULL;
    }
    content[size] = '\0';
    fclose(file);
    return content;
}

static int expect_contains(const char *content, const char *token, const char *message) {
    if (content != NULL && strstr(content, token) != NULL) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int expect_absent(const char *content, const char *token, const char *message) {
    if (content != NULL && strstr(content, token) == NULL) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    char *hal_source = read_file(HAL_DISPLAY_SOURCE);
    char *hal_cmake = read_file(HAL_DISPLAY_CMAKE);
    char *display_ui = read_file(DISPLAY_UI_SOURCE);
    char *display_ui_header = read_file(DISPLAY_UI_HEADER);
    int failures = 0;

    if (hal_source == NULL || hal_cmake == NULL || display_ui == NULL || display_ui_header == NULL) {
        fprintf(stderr, "FAIL: could not read boundary source files\n");
        free(hal_source);
        free(hal_cmake);
        free(display_ui);
        free(display_ui_header);
        return 1;
    }

    failures += expect_contains(hal_source, "hal_display_get_animation_surface(void)",
                                "HAL exposes an opaque animation surface");
    failures += expect_absent(hal_source, "anim_player.h", "HAL must not include Anim Player");
    failures += expect_absent(hal_source, "anim_storage.h", "HAL must not include Anim Storage");
    failures += expect_absent(hal_source, "emoji_anim_", "HAL must not control animation playback");
    failures += expect_absent(hal_source, "animation_registry_", "HAL must not map animation identities");
    failures += expect_absent(hal_source, "boot_anim", "HAL must not own boot animation screens");
    failures += expect_absent(hal_source, "complete_once", "HAL must not own animation interruption policy");
    failures += expect_absent(hal_source, "anim_policy.json", "HAL must not load animation policy files");
    failures += expect_absent(hal_cmake, "anim_service", "HAL CMake must not depend on Anim Service");
    failures += expect_absent(hal_cmake, "\"json\"", "HAL CMake must not depend on JSON");
    failures += expect_absent(hal_cmake, "boot_anim", "HAL CMake must not depend on Boot Animation");

    failures += expect_absent(display_ui, "hal_display_set_emoji", "Display UI must not use HAL animation control");
    failures += expect_absent(display_ui, "anim_player.h", "Display UI must not bypass Animation Service");
    failures += expect_absent(display_ui, "animation_service.h", "Display UI must be a text-only facade");
    failures += expect_absent(display_ui, "animation_submit", "Display UI must not submit animation requests");
    failures += expect_absent(display_ui, "animation_get_snapshot", "Display UI must not query animation state");
    failures += expect_absent(display_ui, "animation_registry", "Display UI must not map animation identities");
    failures += expect_absent(display_ui, "emoji", "Display UI must not expose legacy emoji behavior");
    failures += expect_absent(display_ui, "display_ui_init", "Display UI must not own HAL lifecycle");
    failures += expect_absent(display_ui, "hal_display_init", "Display UI must not initialize HAL implicitly");
    failures +=
        expect_contains(display_ui, "hal_display_set_text_with_style", "Display UI remains a text facade over HAL");
    failures += expect_absent(display_ui_header, "animation", "Display UI header must not expose animation concepts");
    failures += expect_absent(display_ui_header, "emoji", "Display UI header must not expose legacy emoji APIs");
    failures += expect_absent(display_ui_header, "display_ui_init", "Display UI header must not expose HAL lifecycle");

    free(hal_source);
    free(hal_cmake);
    free(display_ui);
    free(display_ui_header);
    if (failures != 0) {
        fprintf(stderr, "hal display boundary host tests failed: %d\n", failures);
        return 1;
    }
    printf("hal display boundary host tests passed\n");
    return 0;
}
