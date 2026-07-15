#include "anim_fault_injection.h"
#include "anim_frame_pool_core.h"
#include "anim_prepare_watchdog.h"
#include "anim_rgb565_fade.h"
#include "anim_worker_scheduler.h"

#include <assert.h>
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
    assert(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    content = (char *)malloc((size_t)size + 1U);
    assert(content != NULL);
    assert(fread(content, 1U, (size_t)size, file) == (size_t)size);
    content[size] = '\0';
    fclose(file);
    return content;
}

static void assert_contains(const char *content, const char *token) {
    if (content == NULL || strstr(content, token) == NULL) {
        fprintf(stderr, "missing required token: %s\n", token);
        abort();
    }
}

static void assert_absent(const char *content, const char *token) {
    if (content == NULL || strstr(content, token) != NULL) {
        fprintf(stderr, "forbidden token present: %s\n", token);
        abort();
    }
}

static void assert_legacy_player_calls_absent(const char *content) {
    static const char *const forbidden[] = {
        "emoji_anim_start(",
        "emoji_anim_request_fade_in(",
        "emoji_anim_get_type(",
        "emoji_anim_get_requested_type(",
        "emoji_anim_is_running(",
        "emoji_anim_is_switch_pending(",
        "emoji_anim_current_completed_once(",
        "emoji_anim_show_static(",
        "emoji_anim_set_interval(",
        "emoji_anim_get_fps(",
        "emoji_anim_set_fps(",
    };
    for (size_t index = 0U; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        assert_absent(content, forbidden[index]);
    }
}

static void test_scheduler_priority(void) {
    anim_worker_schedule_input_t input = {
        .active_in_use = true,
        .active_loadable = true,
        .active_buffered = 1U,
        .active_capacity = 4U,
        .active_deadline_near = false,
        .staging_in_use = true,
        .staging_loadable = true,
        .staging_buffered = 0U,
        .staging_capacity = 4U,
        .prefetch_in_use = true,
        .prefetch_loadable = true,
        .prefetch_buffered = 0U,
        .prefetch_capacity = 4U,
        .active_low_water = 1U,
        .staging_prefill = 2U,
    };

    assert(anim_worker_schedule(&input) == ANIM_WORK_ACTIVE);
    input.active_buffered = 2U;
    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
    input.staging_buffered = 2U;
    assert(anim_worker_schedule(&input) == ANIM_WORK_ACTIVE);
    input.active_buffered = input.active_capacity;
    assert(anim_worker_schedule(&input) == ANIM_WORK_PREFETCH);
    input.prefetch_buffered = input.prefetch_capacity;
    assert(anim_worker_schedule(&input) == ANIM_WORK_NONE);
}

static void test_deadline_prevents_active_starvation(void) {
    anim_worker_schedule_input_t input = {
        .active_in_use = true,
        .active_loadable = true,
        .active_buffered = 3U,
        .active_capacity = 4U,
        .active_deadline_near = true,
        .staging_in_use = true,
        .staging_loadable = true,
        .staging_buffered = 0U,
        .staging_capacity = 4U,
        .prefetch_in_use = true,
        .prefetch_loadable = true,
        .prefetch_buffered = 0U,
        .prefetch_capacity = 4U,
        .active_low_water = 1U,
        .staging_prefill = 3U,
    };
    assert(anim_worker_schedule(&input) == ANIM_WORK_ACTIVE);

    input.active_deadline_near = false;
    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
    input.staging_buffered = input.staging_prefill;
    assert(anim_worker_schedule(&input) == ANIM_WORK_ACTIVE);
}

static void test_buffered_sleep_loop_does_not_delay_sleep_out_prefill(void) {
    anim_worker_schedule_input_t input = {
        .active_in_use = true,
        .active_loadable = true,
        .active_buffered = 3U,
        .active_capacity = 4U,
        .active_deadline_near = false,
        .staging_in_use = true,
        .staging_loadable = true,
        .staging_buffered = 0U,
        .staging_capacity = 3U,
        .active_low_water = 1U,
        .staging_prefill = 3U,
    };

    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
    input.staging_buffered = 1U;
    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
    input.staging_buffered = 2U;
    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
}

static void test_exhausted_non_loop_active_does_not_starve_staging(void) {
    anim_worker_schedule_input_t input = {
        .active_in_use = true,
        .active_loadable = false,
        .active_buffered = 1U,
        .active_capacity = 3U,
        .active_deadline_near = false,
        .staging_in_use = true,
        .staging_loadable = true,
        .staging_buffered = 0U,
        .staging_capacity = 4U,
        .active_low_water = 1U,
        .staging_prefill = 3U,
    };

    assert(anim_worker_schedule(&input) == ANIM_WORK_STAGING);
}

static void test_rgb565_fade_preserves_source_color(void) {
    const uint16_t source = 0xA53DU;
    size_t previous_visible = 0U;
    anim_rgb565_fade_pattern_t pattern;

    for (uint16_t alpha = 0U; alpha <= 255U; ++alpha) {
        size_t visible = 0U;
        anim_rgb565_fade_pattern_prepare(&pattern, (uint8_t)alpha);
        for (uint32_t y = 0U; y < 8U; ++y) {
            for (uint32_t x = 0U; x < 8U; ++x) {
                uint16_t output = anim_rgb565_fade_pattern_apply(&pattern, source, x, y);
                assert(output == 0U || output == source);
                visible += output == source ? 1U : 0U;
            }
        }
        assert(visible >= previous_visible);
        previous_visible = visible;
    }

    anim_rgb565_fade_pattern_prepare(&pattern, 0U);
    assert(anim_rgb565_fade_pattern_apply(&pattern, source, 0U, 0U) == 0U);
    anim_rgb565_fade_pattern_prepare(&pattern, 255U);
    assert(anim_rgb565_fade_pattern_apply(&pattern, source, 7U, 7U) == source);
    anim_rgb565_fade_pattern_prepare(&pattern, 128U);
    assert(anim_rgb565_fade_pattern_apply(&pattern, source, 0U, 0U) == source);
    assert(anim_rgb565_fade_pattern_apply(&pattern, source, 7U, 0U) == 0U);
}

static void test_frame_pool_is_atomic_and_preserves_active(void) {
    anim_frame_pool_core_t pool;
    uint16_t active[4];
    uint16_t staging[4];
    uint16_t extra = ANIM_FRAME_POOL_INVALID_INDEX;

    assert(anim_frame_pool_core_init(&pool, 8U));
    assert(anim_frame_pool_core_acquire(&pool, 4U, active));
    assert(anim_frame_pool_core_acquire(&pool, 4U, staging));
    assert(pool.used_count == 8U);
    assert(!anim_frame_pool_core_acquire(&pool, 1U, &extra));
    assert(pool.used_count == 8U);
    for (size_t index = 0U; index < 4U; ++index) {
        assert(pool.used[active[index]]);
    }
    anim_frame_pool_core_release(&pool, 4U, staging);
    assert(pool.used_count == 4U);
    assert(anim_frame_pool_core_acquire(&pool, 1U, &extra));
    assert(pool.used_count == 5U);
}

static void test_prepare_watchdog_uses_progress_not_completion_time(void) {
    anim_prepare_watchdog_t watchdog;
    uint32_t generation = 0U;
    uint32_t ticket = 0U;

    anim_prepare_watchdog_reset(&watchdog);
    anim_prepare_watchdog_start(&watchdog, 7U, 42U, 100U, 500U);
    assert(!anim_prepare_watchdog_poll(&watchdog, 599U, &generation, &ticket));
    assert(!anim_prepare_watchdog_note_progress(&watchdog, 8U, 42U, 300U));
    assert(anim_prepare_watchdog_note_progress(&watchdog, 7U, 42U, 300U));
    assert(!anim_prepare_watchdog_poll(&watchdog, 799U, &generation, &ticket));
    assert(anim_prepare_watchdog_poll(&watchdog, 800U, &generation, &ticket));
    assert(generation == 7U && ticket == 42U);
    assert(!anim_prepare_watchdog_poll(&watchdog, 900U, &generation, &ticket));
}

static void test_fault_hooks_fail_only_selected_call(void) {
    anim_fault_injection_reset();
    for (int point = 0; point < ANIM_FAULT_COUNT; ++point) {
        anim_fault_injection_fail_on_call((anim_fault_point_t)point, 2U);
        assert(!anim_fault_injection_should_fail((anim_fault_point_t)point));
        assert(anim_fault_injection_should_fail((anim_fault_point_t)point));
        assert(!anim_fault_injection_should_fail((anim_fault_point_t)point));
    }
}

static void test_data_plane_static_boundaries(void) {
    char *player = read_file(ANIM_PLAYER_SOURCE);
    char *storage = read_file(ANIM_STORAGE_SOURCE);
    char *runtime = read_file(ANIM_RUNTIME_SOURCE);
    char *kconfig = read_file(ANIM_KCONFIG_SOURCE);
    char *app_main = read_file(APP_MAIN_SOURCE);
    char *voice = read_file(VOICE_FSM_SOURCE);
    char *behavior = read_file(BEHAVIOR_SOURCE);
    char *hal = read_file(HAL_DISPLAY_SOURCE);
    FILE *legacy_header = fopen(LEGACY_ANIM_PLAYER_HEADER, "rb");

    assert(legacy_header == NULL);
    assert_absent(runtime, "#include \"anim_player.h\"");
    assert_contains(runtime, "frame_index=%ld");
    assert_contains(runtime, "completed_cycles=%u");
    assert_contains(runtime, "ANIMATION_RUNTIME_TASK_STACK_BYTES 8192U");
    assert_contains(runtime, "ANIMATION_RUNTIME_STACK_WARN_FREE_BYTES 2048U");
    assert_contains(runtime, "EXT_RAM_BSS_ATTR static StackType_t g_animation_controller_task_stack");
    assert_contains(runtime,
                    "_Static_assert(sizeof(g_animation_controller_task_stack) >= ANIMATION_RUNTIME_TASK_STACK_BYTES");
    assert_contains(runtime, "controller_stack configured_bytes=%u min_free_bytes=%u status=%s");
    assert_contains(runtime, "emoji_anim_player_poll_prepare_watchdog()");
    assert_contains(runtime, "ANIMATION_RUNTIME_WATCHDOG_TICK_MS");
    assert_absent(runtime, "ANIMATION_RUNTIME_TASK_STACK_WORDS");
    assert_contains(player, "select_worker_target_locked");
    assert_contains(player, "g_prefetch_playback.in_use && g_prefetch_playback.type == type");
    assert_absent(player, "g_prefetch_playback.type == type && g_prefetch_playback.buffered_frames > 0");
    assert_contains(player, "return failure_queued ? 0 : EMOJI_ANIM_PLAYER_ERR_QUEUE_FULL;");
    assert_contains(player, "ANIM_PLAYER_FAILURE_PREPARE_STALLED");
    assert_contains(player, "poll_prepare_watchdog_locked");
    assert_contains(player, "prepare_watchdog_expired = poll_prepare_watchdog_locked()");
    assert_contains(player, "render_safe_error_page_locked");
    assert_contains(player, "anim_rgb565_fade_pattern_apply");
    assert_absent(player, "anim_rgb565_fade_lut");
    assert_absent(player, "anim_rgb565_blend_black");
    assert_absent(player, "int emoji_anim_start(");
    assert_absent(player, "emoji_anim_request_fade_in");
    assert_absent(player, "emoji_anim_get_snapshot");
    assert_absent(player, "emoji_anim_show_static");
    assert_contains(storage, "ANIM_FAULT_SD_OPEN");
    assert_contains(storage, "ANIM_FAULT_SD_SHORT_READ");
    assert_contains(storage, "ANIM_FAULT_PSRAM_FRAME_BUFFER_ALLOC");
    assert_contains(kconfig, "WATCHER_ANIM_ACTIVE_LOW_WATER_FRAMES");
    assert_contains(kconfig, "WATCHER_ANIM_STAGING_PREFILL_FRAMES");
    assert_contains(kconfig, "WATCHER_ANIM_PREPARE_STALL_TIMEOUT_MS");
    assert_absent(kconfig, "WATCHER_ANIM_WARM_BUDGET_KB");
    assert_absent(kconfig, "WATCHER_ANIM_HOT_BUDGET_KB");
    assert_absent(kconfig, "WATCHER_ANIM_CACHE_RGB565");
    assert_absent(app_main, "#include \"anim_player.h\"");
    assert_legacy_player_calls_absent(app_main);
    assert_absent(voice, "#include \"anim_player.h\"");
    assert_legacy_player_calls_absent(voice);
    assert_absent(voice, "animation_service_suspend(");
    assert_contains(app_main, "agent_app_prefetch_wake_anim(\"agent sleep loop committed\")");
    assert_contains(voice, "if (!g_remote_control.recording_permitted)");
    assert_contains(voice, "Recording start deferred until animation gate opens");
    assert_contains(app_main, "voice_recorder_set_recording_permitted(false)");
    assert_contains(app_main, "voice_recorder_set_recording_permitted(true)");
    assert_contains(app_main, "snapshot.visible_type != EMOJI_ANIM_LISTENING");
    assert_contains(app_main, "Agent wake reconciled from visible listening snapshot");
    assert_absent(behavior, "#include \"anim_player.h\"");
    assert_legacy_player_calls_absent(behavior);
    assert_absent(hal, "#include \"anim_player.h\"");
    assert_legacy_player_calls_absent(hal);

    free(player);
    free(storage);
    free(runtime);
    free(kconfig);
    free(app_main);
    free(voice);
    free(behavior);
    free(hal);
}

int main(void) {
    test_scheduler_priority();
    test_deadline_prevents_active_starvation();
    test_buffered_sleep_loop_does_not_delay_sleep_out_prefill();
    test_exhausted_non_loop_active_does_not_starve_staging();
    test_rgb565_fade_preserves_source_color();
    test_frame_pool_is_atomic_and_preserves_active();
    test_prepare_watchdog_uses_progress_not_completion_time();
    test_fault_hooks_fail_only_selected_call();
    test_data_plane_static_boundaries();
    puts("anim data plane host tests passed");
    return 0;
}
