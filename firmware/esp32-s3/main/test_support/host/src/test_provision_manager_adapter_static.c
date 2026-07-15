#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *contents;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }

    contents = (char *)malloc((size_t)size + 1U);
    if (contents == NULL || fread(contents, 1U, (size_t)size, file) != (size_t)size) {
        free(contents);
        fclose(file);
        return NULL;
    }
    contents[size] = '\0';
    fclose(file);
    return contents;
}

int main(int argc, char **argv) {
    static const char *const required[] = {
        "#include \"provision_manager.h\"",
        "static provision_manager_t s_provision_manager;",
        "provision_manager_init(&s_provision_manager",
        "static void provision_manager_platform_tick(void)",
        "provision_manager_resume(&s_provision_manager)",
        "provision_manager_suspend(&s_provision_manager)",
        "provision_manager_platform_tick();",
    };
    char path[1024];
    char *contents;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "expected main component directory\n");
        return 2;
    }
    (void)snprintf(path, sizeof(path), "%s/app_main.c", argv[1]);
    contents = read_file(path);
    if (contents == NULL) {
        fprintf(stderr, "failed to read %s\n", path);
        return 2;
    }

    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (strstr(contents, required[i]) == NULL) {
            fprintf(stderr, "missing Provision Manager adapter contract: %s\n", required[i]);
            failures++;
        }
    }

    free(contents);
    return failures == 0 ? 0 : 1;
}
