#ifndef WATCHER_SDK_CONTROL_UI_H
#define WATCHER_SDK_CONTROL_UI_H

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDK_CONTROL_UI_WAITING = 0,
    SDK_CONTROL_UI_CONNECTED,
    SDK_CONTROL_UI_RECONNECTING,
    SDK_CONTROL_UI_ERROR,
} sdk_control_ui_state_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *title;
    lv_obj_t *emblem;
    lv_obj_t *icon_prompt;
    lv_obj_t *icon_prompt_bar;
    lv_obj_t *icon_stroke_a;
    lv_obj_t *icon_stroke_b;
    lv_obj_t *headline;
    lv_obj_t *caption;
    lv_obj_t *code;
    lv_obj_t *status_pill;
    lv_obj_t *status_dot;
    lv_obj_t *status;
    lv_obj_t *detail;
    sdk_control_ui_state_t state;
    bool built;
} sdk_control_ui_t;

/* Build and presentation updates must run while the caller owns the LVGL lock. */
bool sdk_control_ui_build(sdk_control_ui_t *ui, lv_obj_t *screen);
void sdk_control_ui_show_pairing(sdk_control_ui_t *ui, const char *pairing_code, bool reconnecting);
void sdk_control_ui_show_connected(sdk_control_ui_t *ui);
void sdk_control_ui_show_error(sdk_control_ui_t *ui, const char *headline, const char *detail);
void sdk_control_ui_show_input_debug(sdk_control_ui_t *ui, const char *line);
bool sdk_control_ui_is_attached(const sdk_control_ui_t *ui, const lv_obj_t *screen);
void sdk_control_ui_reset(sdk_control_ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif /* WATCHER_SDK_CONTROL_UI_H */
