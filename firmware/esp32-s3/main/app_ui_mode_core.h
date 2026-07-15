#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_UI_MODE_NONE = 0,
    APP_UI_MODE_TEXT_ONLY,
    APP_UI_MODE_ANIMATION,
    APP_UI_MODE_FULLSCREEN,
} app_ui_mode_t;

typedef enum {
    APP_UI_MODE_RESULT_OK = 0,
    APP_UI_MODE_RESULT_INVALID_ARGUMENT,
    APP_UI_MODE_RESULT_CREATE_FAILED,
    APP_UI_MODE_RESULT_BIND_FAILED,
    APP_UI_MODE_RESULT_UNBIND_FAILED,
} app_ui_mode_result_t;

typedef struct {
    app_ui_mode_t mode;
    bool surface_bound;
} app_ui_mode_core_t;

typedef struct {
    int (*prepare_mode)(void *ctx, app_ui_mode_t previous, app_ui_mode_t required, void **surface_out);
    int (*bind_surface)(void *ctx, void *surface);
    int (*unbind_surface)(void *ctx);
    void (*rollback_mode)(void *ctx, app_ui_mode_t previous, app_ui_mode_t attempted);
    void (*release_mode)(void *ctx, app_ui_mode_t mode);
    void *ctx;
} app_ui_mode_ops_t;

void app_ui_mode_core_init(app_ui_mode_core_t *core);
app_ui_mode_result_t app_ui_mode_core_ensure(app_ui_mode_core_t *core, app_ui_mode_t required,
                                             const app_ui_mode_ops_t *ops);
app_ui_mode_result_t app_ui_mode_core_close(app_ui_mode_core_t *core, const app_ui_mode_ops_t *ops);
app_ui_mode_t app_ui_mode_core_get(const app_ui_mode_core_t *core);
bool app_ui_mode_core_surface_bound(const app_ui_mode_core_t *core);

#ifdef __cplusplus
}
#endif
