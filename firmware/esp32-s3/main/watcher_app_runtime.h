#ifndef WATCHER_APP_RUNTIME_H
#define WATCHER_APP_RUNTIME_H

#include "esp_err.h"
#include "watcher_input_router.h"

#include <stdint.h>

typedef struct watcher_app watcher_app_t;

typedef void (*watcher_app_cb_t)(void);
typedef esp_err_t (*watcher_app_destroy_cb_t)(void);
typedef void (*watcher_app_button_cb_t)(void);
typedef void (*watcher_app_touch_cb_t)(int16_t x, int16_t y);
typedef void (*watcher_app_rotate_cb_t)(int32_t diff);
typedef watcher_input_context_t (*watcher_app_input_context_cb_t)(void);

typedef enum {
    WATCHER_APP_RESOURCE_OFF = 0,
    WATCHER_APP_RESOURCE_BLE_ONLY,
    WATCHER_APP_RESOURCE_WIFI_ONLY,
    WATCHER_APP_RESOURCE_PROVISIONING,
} watcher_app_resource_mode_t;

typedef uint32_t watcher_app_resource_set_t;

enum {
    WATCHER_APP_RESOURCE_SET_NONE = 0,
    WATCHER_APP_RESOURCE_SET_WIFI_STA = 1U << 0,
    WATCHER_APP_RESOURCE_SET_BLE = 1U << 1,
    WATCHER_APP_RESOURCE_SET_PROVISIONING = 1U << 2,
    WATCHER_APP_RESOURCE_SET_CLOUD = 1U << 3,
    WATCHER_APP_RESOURCE_SET_AUDIO = 1U << 4,
    WATCHER_APP_RESOURCE_SET_MCU_RUNTIME = 1U << 5,
    WATCHER_APP_RESOURCE_SET_APP_CENTER = 1U << 6,
};

typedef enum {
    WATCHER_APP_LIFECYCLE_PERSISTENT = 0,
    WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE,
} watcher_app_lifecycle_t;

typedef esp_err_t (*watcher_app_resource_apply_cb_t)(watcher_app_resource_mode_t mode,
                                                     watcher_app_resource_set_t resources, const char *app_id);

struct watcher_app {
    const char *id;
    const char *name;
    const char *icon;
    uint32_t theme_color;
    watcher_app_resource_mode_t resource_mode;
    watcher_app_resource_set_t resources;
    watcher_app_lifecycle_t lifecycle;
    watcher_app_cb_t on_create;
    watcher_app_cb_t on_open;
    watcher_app_cb_t on_tick;
    watcher_app_cb_t on_close;
    watcher_app_destroy_cb_t on_destroy;
    watcher_input_context_t input_context;
    watcher_app_input_context_cb_t get_input_context;
    watcher_app_button_cb_t on_button;
    watcher_app_touch_cb_t on_touch;
    watcher_app_rotate_cb_t on_rotate;
};

esp_err_t watcher_app_register(const watcher_app_t *app);
void watcher_app_set_resource_apply_cb(watcher_app_resource_apply_cb_t cb);
esp_err_t watcher_app_open(const char *id);
esp_err_t watcher_app_close_current(void);
esp_err_t watcher_app_destroy(const char *id);
void watcher_app_tick(void);
const watcher_app_t *watcher_app_get_active(void);
watcher_app_resource_set_t watcher_app_effective_resources(const watcher_app_t *app);

#endif /* WATCHER_APP_RUNTIME_H */
