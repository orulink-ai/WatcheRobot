#ifndef DEBUG_CLI_H
#define DEBUG_CLI_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*debug_cli_app_open_cb_t)(const char *app_id);
typedef esp_err_t (*debug_cli_app_close_cb_t)(void);
typedef esp_err_t (*debug_cli_app_status_cb_t)(void);
typedef esp_err_t (*debug_cli_app_connect_cb_t)(void);
typedef esp_err_t (*debug_cli_settings_open_cb_t)(const char *page_id);
typedef esp_err_t (*debug_cli_settings_focus_cb_t)(const char *target_id);
typedef esp_err_t (*debug_cli_settings_rotate_cb_t)(int steps);
typedef esp_err_t (*debug_cli_settings_click_cb_t)(void);
typedef esp_err_t (*debug_cli_settings_status_cb_t)(void);

void debug_cli_set_app_control_callbacks(debug_cli_app_open_cb_t open_cb, debug_cli_app_close_cb_t close_cb,
                                         debug_cli_app_status_cb_t status_cb,
                                         debug_cli_app_connect_cb_t connect_cb);
void debug_cli_set_settings_callbacks(debug_cli_settings_open_cb_t open_cb,
                                      debug_cli_settings_focus_cb_t focus_cb,
                                      debug_cli_settings_rotate_cb_t rotate_cb,
                                      debug_cli_settings_click_cb_t click_cb,
                                      debug_cli_settings_status_cb_t status_cb);
esp_err_t debug_cli_start(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_CLI_H */
