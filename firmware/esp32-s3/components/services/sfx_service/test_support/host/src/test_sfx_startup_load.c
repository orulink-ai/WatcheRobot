#include "esp_heap_caps.h"
#include "hal_audio.h"
#include "sfx_service.h"

#include <stdio.h>
#include <string.h>

size_t hal_audio_host_bytes_written;
int hal_audio_host_start_count;
int hal_audio_host_release_idle_count;
bool hal_audio_host_running;
bool hal_audio_host_playback_mode;
bool heap_caps_host_fail_spiram_allocs;
int heap_caps_host_internal_alloc_count;

int sfx_service_cached_audio_count_for_test(void);
bool sfx_service_is_initialized_for_test(void);
esp_err_t sfx_service_play_immediate_for_test(const char *sound_id);

static int expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;

    failures += expect_true(sfx_service_reload() == ESP_OK, "cold reload initializes sfx service");
    failures += expect_true(sfx_service_cached_audio_count_for_test() == 0, "cold reload does not cache audio blobs");

    failures += expect_true(sfx_service_init() == ESP_OK, "init is idempotent after cold reload");
    failures += expect_true(sfx_service_cached_audio_count_for_test() == 0, "idempotent init still has no cached audio blobs");

    failures += expect_true(sfx_service_reload() == ESP_OK, "hot reload succeeds");
    failures += expect_true(sfx_service_cached_audio_count_for_test() == 0, "hot reload does not cache audio blobs");

    failures += expect_true(sfx_service_deinit() == ESP_OK, "deinit releases idle sfx service");
    failures += expect_true(sfx_service_reload() == ESP_OK, "reload works after deinit");
    failures += expect_true(sfx_service_cached_audio_count_for_test() == 0, "reload after deinit has no cached audio blobs");

    hal_audio_host_reset();
    failures += expect_true(sfx_service_play_immediate_for_test("boot") == ESP_OK, "lazy immediate playback returns OK");
    failures += expect_true(hal_audio_host_total_starts() == 1, "lazy playback starts audio once");
    failures += expect_true(hal_audio_host_total_bytes_written() == strlen("boot-audio"), "lazy playback writes boot audio bytes");
    failures += expect_true(hal_audio_host_total_release_idle() == 1,
                            "lazy playback releases idle audio path after boot sfx");
    failures += expect_true(sfx_service_cached_audio_count_for_test() == 0, "lazy playback does not retain audio blob cache");
    failures += expect_true(sfx_service_deinit() == ESP_OK, "deinit works after lazy playback");

    heap_caps_host_reset();
    heap_caps_host_fail_spiram_allocs = true;
    hal_audio_host_reset();
    failures += expect_true(sfx_service_play_immediate_for_test("boot") == ESP_OK,
                            "psram allocation failure streams sfx instead of failing");
    failures += expect_true(hal_audio_host_total_bytes_written() == strlen("boot-audio"),
                            "streaming fallback writes boot audio bytes");
    failures += expect_true(hal_audio_host_total_release_idle() == 1,
                            "streaming fallback releases idle audio path after boot sfx");
    failures += expect_true(heap_caps_host_internal_alloc_count == 0,
                            "psram allocation failure does not fallback to internal sfx blob");
    failures += expect_true(sfx_service_deinit() == ESP_OK, "deinit works after streaming fallback");
    heap_caps_host_reset();

    sfx_service_set_cloud_audio_busy(false);
    failures += expect_true(!sfx_service_is_initialized_for_test(), "cloud audio idle does not cold-start sfx");
    sfx_service_set_cloud_audio_busy(true);
    failures += expect_true(!sfx_service_is_initialized_for_test(), "cloud audio busy does not cold-start sfx");
    failures += expect_true(sfx_service_init() == ESP_OK, "explicit init after cloud busy succeeds");
    failures += expect_true(sfx_service_is_cloud_audio_busy(), "cloud audio busy flag is retained after explicit init");
    failures += expect_true(sfx_service_play_delayed("boot", 0) == ESP_ERR_INVALID_STATE,
                            "cloud audio busy rejects every new local sfx");
    sfx_service_set_cloud_audio_busy(false);
    failures += expect_true(sfx_service_play_delayed("standby", 10000) == ESP_OK,
                            "local sfx can be scheduled after cloud audio is idle");
    failures += expect_true(sfx_service_is_busy(), "scheduled local sfx makes service busy");
    sfx_service_set_cloud_audio_busy(true);
    failures += expect_true(!sfx_service_is_busy(), "cloud audio busy cancels pending local sfx");
    failures += expect_true(sfx_service_play_delayed("standby", 10000) == ESP_ERR_INVALID_STATE,
                            "cloud audio busy keeps rejecting delayed local sfx");
    sfx_service_set_cloud_audio_busy(false);
    failures += expect_true(sfx_service_deinit() == ESP_OK, "deinit works after cloud audio busy");

    sfx_service_set_voice_audio_busy(false);
    failures += expect_true(!sfx_service_is_initialized_for_test(), "voice audio idle does not cold-start sfx");
    sfx_service_set_voice_audio_busy(true);
    failures += expect_true(!sfx_service_is_initialized_for_test(), "voice audio busy does not cold-start sfx");
    failures += expect_true(sfx_service_init() == ESP_OK, "explicit init after voice busy succeeds");
    failures += expect_true(sfx_service_is_voice_audio_busy(), "voice audio busy flag is retained after explicit init");
    hal_audio_host_reset();
    failures += expect_true(sfx_service_play("standby") == ESP_ERR_INVALID_STATE,
                            "voice audio busy rejects scheduled local sfx");
    failures += expect_true(sfx_service_play_immediate_for_test("boot") == ESP_OK,
                            "voice audio busy skips immediate local sfx without error");
    failures += expect_true(hal_audio_host_total_starts() == 0, "voice audio busy does not start local sfx audio");
    failures += expect_true(hal_audio_host_total_release_idle() == 0,
                            "voice audio busy does not release foreground audio path");
    sfx_service_set_voice_audio_busy(false);
    failures += expect_true(sfx_service_deinit() == ESP_OK, "deinit works after voice audio busy");

    if (failures != 0) {
        return 1;
    }

    puts("sfx startup load host tests passed");
    return 0;
}
