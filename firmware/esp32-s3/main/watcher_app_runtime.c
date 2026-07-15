#include "watcher_app_runtime.h"

#include "esp_log.h"

#include <stdbool.h>
#include <string.h>

#define TAG "APP_RUNTIME"
#define WATCHER_APP_MAX_COUNT 16

static const watcher_app_t *s_apps[WATCHER_APP_MAX_COUNT];
static bool s_created[WATCHER_APP_MAX_COUNT];
static bool s_destroy_pending[WATCHER_APP_MAX_COUNT];
static int s_app_count = 0;
static volatile int s_active_index = -1;
static watcher_app_resource_apply_cb_t s_resource_apply_cb = NULL;

watcher_app_resource_set_t watcher_app_effective_resources(const watcher_app_t *app) {
    if (app == NULL) {
        return WATCHER_APP_RESOURCE_SET_NONE;
    }
    if (app->resources != WATCHER_APP_RESOURCE_SET_NONE) {
        return app->resources;
    }

    switch (app->resource_mode) {
    case WATCHER_APP_RESOURCE_BLE_ONLY:
        return WATCHER_APP_RESOURCE_SET_BLE;
    case WATCHER_APP_RESOURCE_WIFI_ONLY:
        return WATCHER_APP_RESOURCE_SET_WIFI_STA;
    case WATCHER_APP_RESOURCE_PROVISIONING:
        return WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_PROVISIONING;
    case WATCHER_APP_RESOURCE_OFF:
    default:
        return WATCHER_APP_RESOURCE_SET_NONE;
    }
}

static esp_err_t destroy_app_at_index(int index) {
    esp_err_t ret;

    if (index < 0 || index >= s_app_count || !s_created[index] || s_apps[index] == NULL) {
        return ESP_OK;
    }
    if (s_apps[index]->on_destroy != NULL) {
        ret = s_apps[index]->on_destroy();
        if (ret != ESP_OK) {
            s_destroy_pending[index] = true;
            return ret;
        }
    }
    s_created[index] = false;
    s_destroy_pending[index] = false;
    return ESP_OK;
}

static int find_app_index(const char *id) {
    if (id == NULL || id[0] == '\0') {
        return -1;
    }

    for (int index = 0; index < s_app_count; ++index) {
        if (s_apps[index] != NULL && s_apps[index]->id != NULL && strcmp(s_apps[index]->id, id) == 0) {
            return index;
        }
    }

    return -1;
}

esp_err_t watcher_app_register(const watcher_app_t *app) {
    if (app == NULL || app->id == NULL || app->id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (app->input_context <= WATCHER_INPUT_CONTEXT_UNSPECIFIED ||
        app->input_context > WATCHER_INPUT_CONTEXT_SYSTEM_ONLY) {
        ESP_LOGE(TAG, "App %s must declare a valid input context", app->id);
        return ESP_ERR_INVALID_ARG;
    }

    if (find_app_index(app->id) >= 0) {
        return ESP_OK;
    }

    if (s_app_count >= WATCHER_APP_MAX_COUNT) {
        ESP_LOGE(TAG, "App registry full; cannot register %s", app->id);
        return ESP_ERR_NO_MEM;
    }

    s_apps[s_app_count] = app;
    s_created[s_app_count] = false;
    s_destroy_pending[s_app_count] = false;
    ++s_app_count;
    ESP_LOGI(TAG, "Registered app id=%s name=%s", app->id, app->name != NULL ? app->name : "");
    return ESP_OK;
}

void watcher_app_set_resource_apply_cb(watcher_app_resource_apply_cb_t cb) {
    s_resource_apply_cb = cb;
}

esp_err_t watcher_app_open(const char *id) {
    int next_index = find_app_index(id);
    const watcher_app_t *next_app = NULL;
    const watcher_app_t *closed_app = NULL;
    esp_err_t ret;

    if (next_index < 0) {
        ESP_LOGE(TAG, "App not found: %s", id != NULL ? id : "(null)");
        return ESP_ERR_NOT_FOUND;
    }

    if (s_active_index == next_index) {
        return ESP_OK;
    }

    if (s_destroy_pending[next_index]) {
        ret = destroy_app_at_index(next_index);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "App %s is still waiting for deferred destroy: %s", id, esp_err_to_name(ret));
            return ret;
        }
    }

    next_app = s_apps[next_index];
    closed_app = s_active_index >= 0 ? s_apps[s_active_index] : NULL;

    if (s_active_index >= 0 && s_apps[s_active_index] != NULL && s_apps[s_active_index]->on_close != NULL) {
        ESP_LOGI(TAG, "Closing app %s", s_apps[s_active_index]->id);
        s_apps[s_active_index]->on_close();
    }
    s_active_index = -1;

    if (s_resource_apply_cb != NULL) {
        ret = s_resource_apply_cb(next_app->resource_mode, watcher_app_effective_resources(next_app), next_app->id);
        if (ret != ESP_OK) {
            esp_err_t rollback_ret = ESP_OK;

            ESP_LOGE(TAG, "Resource apply failed for app %s: %s", next_app->id, esp_err_to_name(ret));
            if (closed_app != NULL) {
                rollback_ret = s_resource_apply_cb(closed_app->resource_mode,
                                                   watcher_app_effective_resources(closed_app), closed_app->id);
                if (rollback_ret == ESP_OK) {
                    s_active_index = find_app_index(closed_app->id);
                    if (closed_app->on_open != NULL) {
                        closed_app->on_open();
                    }
                } else {
                    ESP_LOGE(TAG, "Resource rollback failed for app %s: %s", closed_app->id,
                             esp_err_to_name(rollback_ret));
                }
            }
            return ret;
        }
    }

    if (closed_app != NULL && closed_app->lifecycle == WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE) {
        ret = destroy_app_at_index(find_app_index(closed_app->id));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Destroy deferred for app %s: %s", closed_app->id, esp_err_to_name(ret));
        }
    }

    if (!s_created[next_index]) {
        if (s_apps[next_index]->on_create != NULL) {
            s_apps[next_index]->on_create();
        }
        s_created[next_index] = true;
    }

    s_active_index = next_index;
    ESP_LOGI(TAG, "Opening app %s", s_apps[s_active_index]->id);
    if (s_apps[s_active_index]->on_open != NULL) {
        s_apps[s_active_index]->on_open();
    }

    return ESP_OK;
}

esp_err_t watcher_app_close_current(void) {
    esp_err_t ret = ESP_OK;
    const watcher_app_t *closing_app;

    if (s_active_index < 0) {
        return ESP_OK;
    }

    closing_app = s_apps[s_active_index];
    if (closing_app != NULL && closing_app->on_close != NULL) {
        closing_app->on_close();
    }
    if (s_resource_apply_cb != NULL) {
        ret = s_resource_apply_cb(WATCHER_APP_RESOURCE_WIFI_ONLY, WATCHER_APP_RESOURCE_SET_WIFI_STA,
                                  closing_app != NULL ? closing_app->id : NULL);
        if (ret != ESP_OK) {
            esp_err_t rollback_ret = ESP_OK;

            ESP_LOGW(TAG, "Resource release after app close returned: %s", esp_err_to_name(ret));
            if (closing_app != NULL) {
                rollback_ret = s_resource_apply_cb(closing_app->resource_mode,
                                                   watcher_app_effective_resources(closing_app), closing_app->id);
                if (rollback_ret == ESP_OK && closing_app->on_open != NULL) {
                    closing_app->on_open();
                } else if (rollback_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Resource rollback after close failed for app %s: %s", closing_app->id,
                             esp_err_to_name(rollback_ret));
                }
            }
            return ret;
        }
    }
    if (closing_app != NULL && closing_app->lifecycle == WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE) {
        ret = destroy_app_at_index(s_active_index);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Destroy deferred for app %s: %s", closing_app->id, esp_err_to_name(ret));
        }
    }
    s_active_index = -1;
    return ESP_OK;
}

esp_err_t watcher_app_destroy(const char *id) {
    int index = find_app_index(id);

    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (index == s_active_index) {
        return ESP_ERR_INVALID_STATE;
    }
    return destroy_app_at_index(index);
}

void watcher_app_tick(void) {
    for (int index = 0; index < s_app_count; ++index) {
        if (index != s_active_index && s_destroy_pending[index]) {
            (void)destroy_app_at_index(index);
        }
    }
    if (s_active_index >= 0 && s_apps[s_active_index] != NULL && s_apps[s_active_index]->on_tick != NULL) {
        s_apps[s_active_index]->on_tick();
    }
}

const watcher_app_t *watcher_app_get_active(void) {
    return s_active_index >= 0 ? s_apps[s_active_index] : NULL;
}
