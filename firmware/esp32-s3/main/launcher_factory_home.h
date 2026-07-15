#ifndef WATCHER_LAUNCHER_FACTORY_HOME_H
#define WATCHER_LAUNCHER_FACTORY_HOME_H

#include <stddef.h>
#include <stdbool.h>
#include "lvgl.h"
#include "launcher_home_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_FACTORY_HOME_ENTRY_COUNT 5

typedef struct {
    const char *title;
    const void *default_img;
    const void *focused_img;
} launcher_factory_home_entry_t;

typedef void (*launcher_factory_home_event_cb_t)(int index, void *user_ctx);

typedef struct {
    launcher_factory_home_event_cb_t on_focused;
    launcher_factory_home_event_cb_t on_clicked;
    void *user_ctx;
} launcher_factory_home_callbacks_t;

void launcher_factory_home_build(lv_obj_t *screen,
                                 const launcher_factory_home_entry_t entries[LAUNCHER_FACTORY_HOME_ENTRY_COUNT],
                                 size_t entry_count,
                                 const launcher_factory_home_callbacks_t *callbacks);
void launcher_factory_home_reset(void);
void launcher_factory_home_bind_group(lv_group_t **group, int focused_index);
void launcher_factory_home_focus_index(int index, bool animate);
void launcher_factory_home_click_focused(void);
void launcher_factory_home_update_status(const char *time_text,
                                         const char *battery_text,
                                         launcher_home_battery_state_t battery_state,
                                         bool wifi_connected,
                                         bool ble_connected);

#ifdef __cplusplus
}
#endif

#endif
