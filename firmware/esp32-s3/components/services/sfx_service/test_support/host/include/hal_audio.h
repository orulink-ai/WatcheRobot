#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

#if defined(SFX_ENABLE_TEST_API)
extern size_t hal_audio_host_bytes_written;
extern int hal_audio_host_start_count;
extern int hal_audio_host_release_idle_count;
extern bool hal_audio_host_running;
extern bool hal_audio_host_playback_mode;

static inline void hal_audio_host_reset(void) {
    hal_audio_host_bytes_written = 0U;
    hal_audio_host_start_count = 0;
    hal_audio_host_release_idle_count = 0;
    hal_audio_host_running = false;
    hal_audio_host_playback_mode = false;
}

static inline size_t hal_audio_host_total_bytes_written(void) {
    return hal_audio_host_bytes_written;
}

static inline int hal_audio_host_total_starts(void) {
    return hal_audio_host_start_count;
}

static inline int hal_audio_host_total_release_idle(void) {
    return hal_audio_host_release_idle_count;
}
#endif

static inline void hal_audio_set_playback_mode(bool enabled) {
#if defined(SFX_ENABLE_TEST_API)
    hal_audio_host_playback_mode = enabled;
#else
    (void)enabled;
#endif
}

static inline void hal_audio_set_sample_rate(int sample_rate) {
    (void)sample_rate;
}

static inline int hal_audio_start(void) {
#if defined(SFX_ENABLE_TEST_API)
    hal_audio_host_start_count++;
    hal_audio_host_running = true;
#endif
    return 0;
}

static inline int hal_audio_write(const void *data, int len) {
    (void)data;
#if defined(SFX_ENABLE_TEST_API)
    if (len > 0) {
        hal_audio_host_bytes_written += (size_t)len;
    }
#endif
    return len;
}

static inline int hal_audio_stop(void) {
#if defined(SFX_ENABLE_TEST_API)
    hal_audio_host_running = false;
    hal_audio_host_playback_mode = false;
#endif
    return 0;
}

static inline bool hal_audio_is_running(void) {
#if defined(SFX_ENABLE_TEST_API)
    return hal_audio_host_running;
#else
    return false;
#endif
}

static inline bool hal_audio_is_playback_mode(void) {
#if defined(SFX_ENABLE_TEST_API)
    return hal_audio_host_playback_mode;
#else
    return false;
#endif
}

static inline int hal_audio_release_idle(void) {
#if defined(SFX_ENABLE_TEST_API)
    hal_audio_host_release_idle_count++;
    hal_audio_host_running = false;
    hal_audio_host_playback_mode = false;
#endif
    return 0;
}

#endif /* HAL_AUDIO_H */
