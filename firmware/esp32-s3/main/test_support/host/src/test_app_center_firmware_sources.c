#include "app_center_firmware_sources.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void test_promotes_preferred_mirror_to_front(void) {
    app_center_firmware_source_t sources[] = {
        {.url = "https://github.com/release.bin", .source_index = 0},
        {.url = "https://cdn.jsdelivr.net/release.bin", .source_index = 1},
        {.url = "https://raw.githubusercontent.com/release.bin", .source_index = 2},
    };

    assert(app_center_firmware_sources_promote_preferred(sources, 3U, 2));
    assert(strcmp(sources[0].url, "https://raw.githubusercontent.com/release.bin") == 0);
    assert(sources[0].source_index == 2);
    assert(strcmp(sources[1].url, "https://github.com/release.bin") == 0);
    assert(sources[1].source_index == 0);
    assert(strcmp(sources[2].url, "https://cdn.jsdelivr.net/release.bin") == 0);
    assert(sources[2].source_index == 1);
}

static void test_keeps_order_when_preferred_is_primary_or_missing(void) {
    app_center_firmware_source_t primary[] = {
        {.url = "primary", .source_index = 0},
        {.url = "mirror", .source_index = 1},
    };
    app_center_firmware_source_t missing[] = {
        {.url = "primary", .source_index = 0},
        {.url = "mirror", .source_index = 1},
    };

    assert(!app_center_firmware_sources_promote_preferred(primary, 2U, 0));
    assert(strcmp(primary[0].url, "primary") == 0);
    assert(strcmp(primary[1].url, "mirror") == 0);

    assert(!app_center_firmware_sources_promote_preferred(missing, 2U, 3));
    assert(strcmp(missing[0].url, "primary") == 0);
    assert(strcmp(missing[1].url, "mirror") == 0);
}

static void test_rejects_invalid_inputs_without_touching_sources(void) {
    app_center_firmware_source_t sources[] = {
        {.url = "primary", .source_index = 0},
        {.url = "mirror", .source_index = 1},
    };

    assert(!app_center_firmware_sources_promote_preferred(NULL, 2U, 1));
    assert(!app_center_firmware_sources_promote_preferred(sources, 0U, 1));
    assert(!app_center_firmware_sources_promote_preferred(sources, 2U, -1));
    assert(strcmp(sources[0].url, "primary") == 0);
    assert(strcmp(sources[1].url, "mirror") == 0);
}

int main(void) {
    test_promotes_preferred_mirror_to_front();
    test_keeps_order_when_preferred_is_primary_or_missing();
    test_rejects_invalid_inputs_without_touching_sources();

    puts("app_center_firmware_sources tests passed");
    return 0;
}
