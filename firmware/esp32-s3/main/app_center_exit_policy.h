#ifndef APP_CENTER_EXIT_POLICY_H
#define APP_CENTER_EXIT_POLICY_H

#include <stdbool.h>

typedef enum {
    APP_CENTER_EXIT_PAGE_WIFI_SETUP = 0,
    APP_CENTER_EXIT_PAGE_LIST,
    APP_CENTER_EXIT_PAGE_DETAIL,
    APP_CENTER_EXIT_PAGE_INSTALL,
} app_center_exit_page_t;

typedef enum {
    APP_CENTER_EXIT_BUTTON_PASS_THROUGH = 0,
    APP_CENTER_EXIT_BUTTON_SHOW_EXIT,
    APP_CENTER_EXIT_BUTTON_RETURN_TO_LAUNCHER,
    APP_CENTER_EXIT_BUTTON_CANCEL_DOWNLOAD,
} app_center_exit_button_action_t;

typedef enum {
    APP_CENTER_EXIT_TICK_NONE = 0,
    APP_CENTER_EXIT_TICK_CANCEL_DOWNLOAD,
    APP_CENTER_EXIT_TICK_RETURN_TO_LAUNCHER,
} app_center_exit_tick_action_t;

app_center_exit_button_action_t app_center_exit_policy_button_action(app_center_exit_page_t page,
                                                                     bool exit_visible);
app_center_exit_tick_action_t app_center_exit_policy_tick_action(bool return_requested,
                                                                 bool download_running);
bool app_center_exit_policy_gesture_reveals_exit(app_center_exit_page_t page);

#endif /* APP_CENTER_EXIT_POLICY_H */
