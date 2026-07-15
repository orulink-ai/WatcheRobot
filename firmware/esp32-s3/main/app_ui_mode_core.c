#include "app_ui_mode_core.h"

#include <stddef.h>

static bool mode_is_valid(app_ui_mode_t mode) {
    return mode >= APP_UI_MODE_NONE && mode <= APP_UI_MODE_FULLSCREEN;
}

void app_ui_mode_core_init(app_ui_mode_core_t *core) {
    if (core == NULL) {
        return;
    }
    core->mode = APP_UI_MODE_NONE;
    core->surface_bound = false;
}

app_ui_mode_result_t app_ui_mode_core_ensure(app_ui_mode_core_t *core, app_ui_mode_t required,
                                             const app_ui_mode_ops_t *ops) {
    app_ui_mode_t previous;
    void *surface = NULL;

    if (core == NULL || ops == NULL || ops->prepare_mode == NULL || !mode_is_valid(required) ||
        required == APP_UI_MODE_NONE) {
        return APP_UI_MODE_RESULT_INVALID_ARGUMENT;
    }
    if (core->mode == required && (required != APP_UI_MODE_ANIMATION || core->surface_bound)) {
        return APP_UI_MODE_RESULT_OK;
    }

    previous = core->mode;
    if (ops->prepare_mode(ops->ctx, previous, required, &surface) != 0) {
        return APP_UI_MODE_RESULT_CREATE_FAILED;
    }
    if (required == APP_UI_MODE_ANIMATION) {
        if (surface == NULL || ops->bind_surface == NULL || ops->bind_surface(ops->ctx, surface) != 0) {
            if (ops->rollback_mode != NULL) {
                ops->rollback_mode(ops->ctx, previous, required);
            }
            return APP_UI_MODE_RESULT_BIND_FAILED;
        }
    }

    if (previous == APP_UI_MODE_ANIMATION && required != APP_UI_MODE_ANIMATION && core->surface_bound &&
        ops->unbind_surface != NULL) {
        if (ops->unbind_surface(ops->ctx) != 0) {
            return APP_UI_MODE_RESULT_UNBIND_FAILED;
        }
    }
    core->mode = required;
    core->surface_bound = required == APP_UI_MODE_ANIMATION;
    return APP_UI_MODE_RESULT_OK;
}

app_ui_mode_result_t app_ui_mode_core_close(app_ui_mode_core_t *core, const app_ui_mode_ops_t *ops) {
    if (core == NULL || ops == NULL) {
        return APP_UI_MODE_RESULT_INVALID_ARGUMENT;
    }
    if (core->mode == APP_UI_MODE_ANIMATION && core->surface_bound) {
        if (ops->unbind_surface == NULL || ops->unbind_surface(ops->ctx) != 0) {
            return APP_UI_MODE_RESULT_UNBIND_FAILED;
        }
    }
    if (core->mode != APP_UI_MODE_NONE && ops->release_mode != NULL) {
        ops->release_mode(ops->ctx, core->mode);
    }
    core->mode = APP_UI_MODE_NONE;
    core->surface_bound = false;
    return APP_UI_MODE_RESULT_OK;
}

app_ui_mode_t app_ui_mode_core_get(const app_ui_mode_core_t *core) {
    return core != NULL ? core->mode : APP_UI_MODE_NONE;
}

bool app_ui_mode_core_surface_bound(const app_ui_mode_core_t *core) {
    return core != NULL && core->mode == APP_UI_MODE_ANIMATION && core->surface_bound;
}
