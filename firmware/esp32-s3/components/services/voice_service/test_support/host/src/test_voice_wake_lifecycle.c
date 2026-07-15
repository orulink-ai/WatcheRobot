#include "voice_wake_lifecycle.h"

#include <stdio.h>
#include <string.h>

typedef struct fake_wake_ctx_s {
    int id;
} fake_wake_ctx_t;

typedef struct {
    bool supported;
    bool init_should_fail;
    bool resume_during_deinit;
    bool release_during_init;
    bool release_during_feed;
    bool can_resume;
    int audio_start_count;
    int audio_idle_count;
    int init_count;
    int start_count;
    int feed_count;
    int stop_count;
    int deinit_count;
    int delay_count;
    int log_count;
    int nested_resume_ret;
    const char *last_stage;
    voice_wake_lifecycle_t *runtime;
    fake_wake_ctx_t ctx;
} fake_env_t;

static bool fake_is_supported(void *user_data) {
    return ((fake_env_t *)user_data)->supported;
}

static int fake_start_audio(void *user_data) {
    ((fake_env_t *)user_data)->audio_start_count++;
    return 0;
}

static int fake_enter_audio_idle(void *user_data) {
    ((fake_env_t *)user_data)->audio_idle_count++;
    return 0;
}

static wake_word_ctx_t *fake_wake_init(const wake_word_config_t *config, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    (void)config;
    env->init_count++;
    if (env->release_during_init && env->runtime != NULL) {
        voice_wake_lifecycle_release(env->runtime, "release while resuming");
    }
    if (env->init_should_fail) {
        return NULL;
    }
    return (wake_word_ctx_t *)&env->ctx;
}

static void fake_wake_start(wake_word_ctx_t *ctx, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    if (ctx != NULL) {
        env->start_count++;
    }
}

static void fake_wake_feed(wake_word_ctx_t *ctx, const int16_t *samples, size_t num_samples, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    if (ctx != NULL && samples != NULL && num_samples > 0U) {
        env->feed_count++;
        if (env->release_during_feed && env->runtime != NULL) {
            voice_wake_lifecycle_release(env->runtime, "release while feeding");
        }
    }
}

static size_t fake_wake_get_feed_size(wake_word_ctx_t *ctx, void *user_data) {
    (void)user_data;
    return ctx != NULL ? 960U : 0U;
}

static void fake_wake_stop(wake_word_ctx_t *ctx, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    if (ctx != NULL) {
        env->stop_count++;
    }
}

static void fake_wake_deinit(wake_word_ctx_t *ctx, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    if (ctx != NULL) {
        env->deinit_count++;
        if (env->resume_during_deinit && env->runtime != NULL) {
            env->nested_resume_ret = voice_wake_lifecycle_resume(env->runtime, "resume while releasing");
        }
    }
}

static void fake_delay_ms(uint32_t delay_ms, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    if (delay_ms > 0U) {
        env->delay_count++;
    }
}

static void fake_snapshot(voice_wake_heap_snapshot_t *out_snapshot, void *user_data) {
    (void)user_data;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
}

static bool fake_can_resume(const voice_wake_heap_snapshot_t *snapshot, void *user_data) {
    (void)snapshot;
    return ((fake_env_t *)user_data)->can_resume;
}

static void fake_log(const char *stage, const char *reason, const voice_wake_heap_snapshot_t *before,
                     const voice_wake_heap_snapshot_t *after, void *user_data) {
    fake_env_t *env = (fake_env_t *)user_data;

    (void)reason;
    (void)before;
    (void)after;
    env->last_stage = stage;
    env->log_count++;
}

static const voice_wake_lifecycle_ops_t k_fake_ops = {
    .is_supported = fake_is_supported,
    .start_audio = fake_start_audio,
    .enter_audio_idle = fake_enter_audio_idle,
    .wake_init = fake_wake_init,
    .wake_start = fake_wake_start,
    .wake_feed = fake_wake_feed,
    .wake_get_feed_size = fake_wake_get_feed_size,
    .wake_stop = fake_wake_stop,
    .wake_deinit = fake_wake_deinit,
    .delay_ms = fake_delay_ms,
    .snapshot = fake_snapshot,
    .can_resume = fake_can_resume,
    .log = fake_log,
};

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static void init_runtime(voice_wake_lifecycle_t *runtime, fake_env_t *env, bool enabled) {
    wake_word_config_t config = {0};

    memset(env, 0, sizeof(*env));
    env->supported = true;
    env->can_resume = true;
    env->nested_resume_ret = ESP_OK;
    env->runtime = runtime;
    voice_wake_lifecycle_init(runtime, &k_fake_ops, env, &config, enabled);
}

static int test_standby_resume_is_idempotent(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "runtime starts inactive");
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "first") == ESP_OK, "first resume succeeds");
    failures += expect_true(voice_wake_lifecycle_is_active(&runtime), "runtime becomes active after resume");
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "second") == ESP_OK, "second resume succeeds");
    failures += expect_true(env.audio_start_count == 1, "audio starts once");
    failures += expect_true(env.init_count == 1, "wake init runs once");
    failures += expect_true(env.start_count == 1, "wake start runs once");
    failures += expect_true(voice_wake_lifecycle_is_active(&runtime), "runtime has active context");
    return failures;
}

static int test_release_is_idempotent(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    (void)voice_wake_lifecycle_resume(&runtime, "standby");
    voice_wake_lifecycle_release(&runtime, "active");
    voice_wake_lifecycle_release(&runtime, "active-again");
    failures += expect_true(env.stop_count == 1, "wake stop runs once");
    failures += expect_true(env.deinit_count == 1, "wake deinit runs once");
    failures += expect_true(env.delay_count == 1, "release waits for task once");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "release leaves runtime inactive");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "runtime context cleared");
    return failures;
}

static int test_close_releases_and_idles_audio(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    (void)voice_wake_lifecycle_resume(&runtime, "standby");
    voice_wake_lifecycle_close(&runtime, "close");
    failures += expect_true(env.stop_count == 1, "close stops wake once");
    failures += expect_true(env.deinit_count == 1, "close deinitializes wake once");
    failures += expect_true(env.audio_idle_count == 1, "close idles audio path once");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "close clears runtime context");
    return failures;
}

static int test_init_failure_clears_state(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    env.init_should_fail = true;
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "fail") == ESP_FAIL, "resume reports init failure");
    failures += expect_true(env.audio_idle_count == 1, "init failure idles audio path");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "failed init leaves no context");
    failures += expect_true(strcmp(env.last_stage, "init_fail") == 0, "init failure is logged");
    return failures;
}

static int test_disabled_runtime_is_safe_noop(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, false);
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "disabled") == ESP_ERR_NOT_SUPPORTED,
                            "disabled resume is not supported");
    voice_wake_lifecycle_release(&runtime, "disabled-release");
    failures += expect_true(env.audio_start_count == 0, "disabled runtime does not start audio");
    failures += expect_true(env.init_count == 0, "disabled runtime does not init wake");
    failures += expect_true(env.stop_count == 0, "disabled runtime release is no-op");
    return failures;
}

static int test_low_heap_preflight_skips_resume(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    env.can_resume = false;
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "sleep standby") == ESP_ERR_NO_MEM,
                            "low heap resume reports no memory");
    failures += expect_true(env.audio_start_count == 0, "low heap resume does not start audio");
    failures += expect_true(env.init_count == 0, "low heap resume does not init wake");
    failures += expect_true(strcmp(env.last_stage, "resume_skip") == 0, "low heap skip is logged");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "low heap leaves no context");
    return failures;
}

static int test_resume_during_release_is_rejected(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    (void)voice_wake_lifecycle_resume(&runtime, "standby");
    env.resume_during_deinit = true;
    voice_wake_lifecycle_release(&runtime, "active");
    failures += expect_true(env.nested_resume_ret == ESP_ERR_INVALID_STATE,
                            "resume during release is rejected as busy");
    failures += expect_true(env.audio_start_count == 1, "busy resume does not start audio again");
    failures += expect_true(env.init_count == 1, "busy resume does not init a second wake runtime");
    failures += expect_true(env.deinit_count == 1, "release still deinitializes once");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "release leaves no context");
    return failures;
}

static int test_release_during_resume_deinitializes_new_context(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    env.release_during_init = true;
    failures += expect_true(voice_wake_lifecycle_resume(&runtime, "standby") == ESP_ERR_INVALID_STATE,
                            "resume reports deferred release");
    failures += expect_true(env.audio_start_count == 1, "resume starts audio once");
    failures += expect_true(env.init_count == 1, "resume initializes wake once");
    failures += expect_true(env.start_count == 1, "wake starts before deferred release");
    failures += expect_true(env.stop_count == 1, "deferred release stops wake");
    failures += expect_true(env.deinit_count == 1, "deferred release deinitializes wake");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "deferred release leaves no context");
    return failures;
}

static int test_release_during_feed_deinitializes_after_feed(void) {
    voice_wake_lifecycle_t runtime;
    fake_env_t env;
    int16_t samples[4] = {1, 2, 3, 4};
    size_t feed_size = 0;
    int failures = 0;

    init_runtime(&runtime, &env, true);
    (void)voice_wake_lifecycle_resume(&runtime, "standby");
    env.release_during_feed = true;
    failures += expect_true(!voice_wake_lifecycle_feed(&runtime, samples, 4, &feed_size),
                            "feed reports runtime released during feed");
    failures += expect_true(env.feed_count == 1, "wake feed runs once");
    failures += expect_true(feed_size == 960U, "feed size is returned");
    failures += expect_true(env.stop_count == 1, "deferred feed release stops wake");
    failures += expect_true(env.deinit_count == 1, "deferred feed release deinitializes wake");
    failures += expect_true(!voice_wake_lifecycle_is_active(&runtime), "feed release leaves no context");
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_standby_resume_is_idempotent();
    failures += test_release_is_idempotent();
    failures += test_close_releases_and_idles_audio();
    failures += test_init_failure_clears_state();
    failures += test_disabled_runtime_is_safe_noop();
    failures += test_low_heap_preflight_skips_resume();
    failures += test_resume_during_release_is_rejected();
    failures += test_release_during_resume_deinitializes_new_context();
    failures += test_release_during_feed_deinitializes_after_feed();

    if (failures != 0) {
        return 1;
    }

    puts("voice wake lifecycle host tests passed");
    return 0;
}
