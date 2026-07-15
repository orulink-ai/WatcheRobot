#ifndef VOICE_WAKE_LIFECYCLE_H
#define VOICE_WAKE_LIFECYCLE_H

#include "esp_err.h"
#include "hal_wake_word.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t internal_free;
    size_t internal_largest;
    size_t dma_largest;
    size_t psram_free;
} voice_wake_heap_snapshot_t;

typedef struct {
    bool (*is_supported)(void *user_data);
    int (*start_audio)(void *user_data);
    int (*enter_audio_idle)(void *user_data);
    wake_word_ctx_t *(*wake_init)(const wake_word_config_t *config, void *user_data);
    void (*wake_start)(wake_word_ctx_t *ctx, void *user_data);
    void (*wake_feed)(wake_word_ctx_t *ctx, const int16_t *samples, size_t num_samples, void *user_data);
    size_t (*wake_get_feed_size)(wake_word_ctx_t *ctx, void *user_data);
    void (*wake_stop)(wake_word_ctx_t *ctx, void *user_data);
    void (*wake_deinit)(wake_word_ctx_t *ctx, void *user_data);
    void (*delay_ms)(uint32_t delay_ms, void *user_data);
    void (*snapshot)(voice_wake_heap_snapshot_t *out_snapshot, void *user_data);
    bool (*can_resume)(const voice_wake_heap_snapshot_t *snapshot, void *user_data);
    bool (*lock)(void *user_data);
    void (*unlock)(void *user_data);
    void (*log)(const char *stage, const char *reason, const voice_wake_heap_snapshot_t *before,
                const voice_wake_heap_snapshot_t *after, void *user_data);
} voice_wake_lifecycle_ops_t;

typedef enum {
    VOICE_WAKE_LIFECYCLE_IDLE = 0,
    VOICE_WAKE_LIFECYCLE_RESUMING,
    VOICE_WAKE_LIFECYCLE_ACTIVE,
    VOICE_WAKE_LIFECYCLE_FEEDING,
    VOICE_WAKE_LIFECYCLE_RELEASING,
} voice_wake_lifecycle_phase_t;

typedef struct {
    const voice_wake_lifecycle_ops_t *ops;
    void *user_data;
    wake_word_config_t config;
    wake_word_ctx_t *ctx;
    bool enabled;
    volatile voice_wake_lifecycle_phase_t phase;
    volatile bool release_requested;
} voice_wake_lifecycle_t;

void voice_wake_lifecycle_init(voice_wake_lifecycle_t *runtime, const voice_wake_lifecycle_ops_t *ops,
                               void *user_data, const wake_word_config_t *config, bool enabled);
esp_err_t voice_wake_lifecycle_resume(voice_wake_lifecycle_t *runtime, const char *reason);
void voice_wake_lifecycle_release(voice_wake_lifecycle_t *runtime, const char *reason);
void voice_wake_lifecycle_close(voice_wake_lifecycle_t *runtime, const char *reason);
bool voice_wake_lifecycle_is_active(voice_wake_lifecycle_t *runtime);
bool voice_wake_lifecycle_feed(voice_wake_lifecycle_t *runtime, const int16_t *samples, size_t num_samples,
                               size_t *out_feed_size);

#endif /* VOICE_WAKE_LIFECYCLE_H */
