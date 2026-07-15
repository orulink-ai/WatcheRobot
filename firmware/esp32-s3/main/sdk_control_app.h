#ifndef SDK_CONTROL_APP_H
#define SDK_CONTROL_APP_H

#include "watcher_app_runtime.h"

#include <stdbool.h>

#define SDK_CONTROL_APP_ID "sdk.control.app"

typedef enum {
    SDK_CONTROL_APP_UI_ERROR_STOPPING = 0,
    SDK_CONTROL_APP_UI_ERROR_INITIALIZATION,
} sdk_control_app_ui_error_t;

typedef struct {
    void (*show_pairing_code)(const char *pairing_code, bool reconnecting);
    void (*show_connected)(void);
    void (*show_error)(sdk_control_app_ui_error_t error);
    bool (*prepare_animation)(void);
    void (*restore_control_ui)(void);
    void (*close_ui)(void);
    void (*tick_ui)(void);
    void (*on_button)(void);
} sdk_control_app_ui_t;

void sdk_control_app_configure(const sdk_control_app_ui_t *ui);
const watcher_app_t *sdk_control_app_get(void);
/** Re-print the active temporary pairing code in Debug CLI builds only. */
bool sdk_control_app_debug_log_pairing_code(void);

#endif /* SDK_CONTROL_APP_H */
