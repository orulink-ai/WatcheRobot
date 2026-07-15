#include "app_center_exit_policy.h"

app_center_exit_button_action_t app_center_exit_policy_button_action(app_center_exit_page_t page,
                                                                     bool exit_visible) {
    if (exit_visible) {
        return APP_CENTER_EXIT_BUTTON_RETURN_TO_LAUNCHER;
    }

    switch (page) {
    case APP_CENTER_EXIT_PAGE_WIFI_SETUP:
        return APP_CENTER_EXIT_BUTTON_SHOW_EXIT;
    case APP_CENTER_EXIT_PAGE_INSTALL:
        return APP_CENTER_EXIT_BUTTON_CANCEL_DOWNLOAD;
    case APP_CENTER_EXIT_PAGE_LIST:
    case APP_CENTER_EXIT_PAGE_DETAIL:
    default:
        return APP_CENTER_EXIT_BUTTON_PASS_THROUGH;
    }
}

app_center_exit_tick_action_t app_center_exit_policy_tick_action(bool return_requested,
                                                                 bool download_running) {
    if (!return_requested) {
        return APP_CENTER_EXIT_TICK_NONE;
    }
    return download_running ? APP_CENTER_EXIT_TICK_CANCEL_DOWNLOAD : APP_CENTER_EXIT_TICK_RETURN_TO_LAUNCHER;
}

bool app_center_exit_policy_gesture_reveals_exit(app_center_exit_page_t page) {
    (void)page;
    return true;
}
