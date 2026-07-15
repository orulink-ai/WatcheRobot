#include "voice_wake_lifecycle.h"

#include <string.h>

#define VOICE_WAKE_STOP_SETTLE_MS 50U

static const char *safe_reason(const char *reason) {
    return (reason != NULL && reason[0] != '\0') ? reason : "unspecified";
}

static void capture_snapshot(const voice_wake_lifecycle_t *runtime, voice_wake_heap_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (runtime != NULL && runtime->ops != NULL && runtime->ops->snapshot != NULL) {
        runtime->ops->snapshot(out_snapshot, runtime->user_data);
    }
}

static void log_stage(const voice_wake_lifecycle_t *runtime, const char *stage, const char *reason,
                      const voice_wake_heap_snapshot_t *before, const voice_wake_heap_snapshot_t *after) {
    if (runtime != NULL && runtime->ops != NULL && runtime->ops->log != NULL) {
        runtime->ops->log(stage, safe_reason(reason), before, after, runtime->user_data);
    }
}

static bool ops_are_valid(const voice_wake_lifecycle_ops_t *ops) {
    return ops != NULL && ops->is_supported != NULL && ops->start_audio != NULL && ops->enter_audio_idle != NULL &&
           ops->wake_init != NULL && ops->wake_start != NULL && ops->wake_stop != NULL && ops->wake_deinit != NULL;
}

static bool lifecycle_lock(voice_wake_lifecycle_t *runtime) {
    if (runtime == NULL || runtime->ops == NULL || runtime->ops->lock == NULL) {
        return true;
    }
    return runtime->ops->lock(runtime->user_data);
}

static void lifecycle_unlock(voice_wake_lifecycle_t *runtime) {
    if (runtime != NULL && runtime->ops != NULL && runtime->ops->unlock != NULL) {
        runtime->ops->unlock(runtime->user_data);
    }
}

static void release_active_locked(voice_wake_lifecycle_t *runtime, const char *reason) {
    voice_wake_heap_snapshot_t before;
    voice_wake_heap_snapshot_t after;
    wake_word_ctx_t *ctx;

    if (runtime == NULL || runtime->ops == NULL || runtime->ctx == NULL) {
        if (runtime != NULL) {
            runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
            runtime->release_requested = false;
        }
        return;
    }

    runtime->phase = VOICE_WAKE_LIFECYCLE_RELEASING;
    runtime->release_requested = false;
    ctx = runtime->ctx;
    runtime->ctx = NULL;
    capture_snapshot(runtime, &before);
    runtime->ops->wake_stop(ctx, runtime->user_data);
    if (runtime->ops->delay_ms != NULL) {
        runtime->ops->delay_ms(VOICE_WAKE_STOP_SETTLE_MS, runtime->user_data);
    }
    runtime->ops->wake_deinit(ctx, runtime->user_data);
    capture_snapshot(runtime, &after);
    runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
    log_stage(runtime, "released", reason, &before, &after);
}

void voice_wake_lifecycle_init(voice_wake_lifecycle_t *runtime, const voice_wake_lifecycle_ops_t *ops,
                               void *user_data, const wake_word_config_t *config, bool enabled) {
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->ops = ops;
    runtime->user_data = user_data;
    runtime->enabled = enabled;
    runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
    runtime->release_requested = false;
    if (config != NULL) {
        runtime->config = *config;
    }
}

esp_err_t voice_wake_lifecycle_resume(voice_wake_lifecycle_t *runtime, const char *reason) {
    voice_wake_heap_snapshot_t before;
    voice_wake_heap_snapshot_t after;

    if (runtime == NULL || !ops_are_valid(runtime->ops)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lifecycle_lock(runtime)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (runtime->phase == VOICE_WAKE_LIFECYCLE_RESUMING ||
        runtime->phase == VOICE_WAKE_LIFECYCLE_RELEASING) {
        capture_snapshot(runtime, &after);
        log_stage(runtime, "resume_skip", reason != NULL ? reason : "transition_busy", NULL, &after);
        lifecycle_unlock(runtime);
        return ESP_ERR_INVALID_STATE;
    }

    if (!runtime->enabled) {
        capture_snapshot(runtime, &after);
        log_stage(runtime, "resume_skip", reason != NULL ? reason : "disabled", NULL, &after);
        lifecycle_unlock(runtime);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!runtime->ops->is_supported(runtime->user_data)) {
        capture_snapshot(runtime, &after);
        log_stage(runtime, "resume_skip", reason != NULL ? reason : "unsupported", NULL, &after);
        lifecycle_unlock(runtime);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (runtime->ctx != NULL) {
        capture_snapshot(runtime, &after);
        log_stage(runtime, "resume_skip", reason != NULL ? reason : "already_active", NULL, &after);
        lifecycle_unlock(runtime);
        return ESP_OK;
    }

    capture_snapshot(runtime, &before);
    if (runtime->ops->can_resume != NULL && !runtime->ops->can_resume(&before, runtime->user_data)) {
        log_stage(runtime, "resume_skip", "low_heap", &before, &before);
        lifecycle_unlock(runtime);
        return ESP_ERR_NO_MEM;
    }

    runtime->phase = VOICE_WAKE_LIFECYCLE_RESUMING;
    runtime->release_requested = false;
    if (runtime->ops->start_audio(runtime->user_data) != 0) {
        runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
        runtime->release_requested = false;
        capture_snapshot(runtime, &after);
        log_stage(runtime, "init_fail", reason != NULL ? reason : "audio_start_failed", &before, &after);
        lifecycle_unlock(runtime);
        return ESP_FAIL;
    }

    wake_word_ctx_t *ctx = runtime->ops->wake_init(&runtime->config, runtime->user_data);
    if (ctx == NULL) {
        (void)runtime->ops->enter_audio_idle(runtime->user_data);
        runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
        runtime->release_requested = false;
        capture_snapshot(runtime, &after);
        log_stage(runtime, "init_fail", reason != NULL ? reason : "wake_init_failed", &before, &after);
        lifecycle_unlock(runtime);
        return ESP_FAIL;
    }

    runtime->ctx = ctx;
    runtime->ops->wake_start(runtime->ctx, runtime->user_data);
    runtime->phase = VOICE_WAKE_LIFECYCLE_ACTIVE;
    capture_snapshot(runtime, &after);
    log_stage(runtime, "standby_init", reason, &before, &after);
    if (runtime->release_requested) {
        release_active_locked(runtime, "deferred release after resume");
        lifecycle_unlock(runtime);
        return ESP_ERR_INVALID_STATE;
    }
    lifecycle_unlock(runtime);
    return ESP_OK;
}

void voice_wake_lifecycle_release(voice_wake_lifecycle_t *runtime, const char *reason) {
    voice_wake_heap_snapshot_t after;

    if (runtime == NULL || runtime->ops == NULL) {
        return;
    }

    if (!lifecycle_lock(runtime)) {
        return;
    }

    if (runtime->phase == VOICE_WAKE_LIFECYCLE_RESUMING || runtime->phase == VOICE_WAKE_LIFECYCLE_FEEDING) {
        runtime->release_requested = true;
        capture_snapshot(runtime, &after);
        log_stage(runtime, "release_defer", reason != NULL ? reason : "resume_busy", NULL, &after);
        lifecycle_unlock(runtime);
        return;
    }

    if (runtime->phase == VOICE_WAKE_LIFECYCLE_RELEASING) {
        capture_snapshot(runtime, &after);
        log_stage(runtime, "release_skip", reason != NULL ? reason : "already_releasing", NULL, &after);
        lifecycle_unlock(runtime);
        return;
    }

    if (runtime->ctx == NULL) {
        runtime->phase = VOICE_WAKE_LIFECYCLE_IDLE;
        runtime->release_requested = false;
        lifecycle_unlock(runtime);
        return;
    }

    release_active_locked(runtime, reason);
    lifecycle_unlock(runtime);
}

void voice_wake_lifecycle_close(voice_wake_lifecycle_t *runtime, const char *reason) {
    if (runtime == NULL || runtime->ops == NULL) {
        return;
    }

    voice_wake_lifecycle_release(runtime, reason);
    if (runtime->ops->enter_audio_idle != NULL) {
        (void)runtime->ops->enter_audio_idle(runtime->user_data);
    }
}

bool voice_wake_lifecycle_is_active(voice_wake_lifecycle_t *runtime) {
    bool active;

    if (runtime == NULL) {
        return false;
    }
    if (!lifecycle_lock(runtime)) {
        return false;
    }
    active = runtime->phase == VOICE_WAKE_LIFECYCLE_ACTIVE && runtime->ctx != NULL;
    lifecycle_unlock(runtime);
    return active;
}

bool voice_wake_lifecycle_feed(voice_wake_lifecycle_t *runtime, const int16_t *samples, size_t num_samples,
                               size_t *out_feed_size) {
    wake_word_ctx_t *ctx;

    if (out_feed_size != NULL) {
        *out_feed_size = 0;
    }
    if (runtime == NULL || runtime->ops == NULL || samples == NULL || num_samples == 0U ||
        runtime->ops->wake_feed == NULL) {
        return false;
    }

    if (!lifecycle_lock(runtime)) {
        return false;
    }

    if (runtime->phase != VOICE_WAKE_LIFECYCLE_ACTIVE || runtime->ctx == NULL) {
        lifecycle_unlock(runtime);
        return false;
    }

    runtime->phase = VOICE_WAKE_LIFECYCLE_FEEDING;
    ctx = runtime->ctx;
    runtime->ops->wake_feed(ctx, samples, num_samples, runtime->user_data);
    if (out_feed_size != NULL && runtime->ops->wake_get_feed_size != NULL) {
        *out_feed_size = runtime->ops->wake_get_feed_size(ctx, runtime->user_data);
    }
    runtime->phase = VOICE_WAKE_LIFECYCLE_ACTIVE;
    if (runtime->release_requested) {
        release_active_locked(runtime, "deferred release after feed");
        lifecycle_unlock(runtime);
        return false;
    }

    lifecycle_unlock(runtime);
    return true;
}
