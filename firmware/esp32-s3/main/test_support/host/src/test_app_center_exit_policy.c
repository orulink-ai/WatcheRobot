#include "app_center_exit_policy.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>

static const app_center_exit_page_t all_pages[] = {
    APP_CENTER_EXIT_PAGE_WIFI_SETUP,
    APP_CENTER_EXIT_PAGE_LIST,
    APP_CENTER_EXIT_PAGE_DETAIL,
    APP_CENTER_EXIT_PAGE_INSTALL,
};

static void test_visible_exit_button_always_returns_to_launcher(void) {
    for (size_t index = 0; index < sizeof(all_pages) / sizeof(all_pages[0]); ++index) {
        assert(app_center_exit_policy_button_action(all_pages[index], true) ==
               APP_CENTER_EXIT_BUTTON_RETURN_TO_LAUNCHER);
    }
}

static void test_wifi_setup_button_reveals_exit_before_returning(void) {
    assert(app_center_exit_policy_button_action(APP_CENTER_EXIT_PAGE_WIFI_SETUP, false) ==
           APP_CENTER_EXIT_BUTTON_SHOW_EXIT);
}

static void test_list_and_detail_buttons_keep_page_actions_until_exit_is_visible(void) {
    assert(app_center_exit_policy_button_action(APP_CENTER_EXIT_PAGE_LIST, false) ==
           APP_CENTER_EXIT_BUTTON_PASS_THROUGH);
    assert(app_center_exit_policy_button_action(APP_CENTER_EXIT_PAGE_DETAIL, false) ==
           APP_CENTER_EXIT_BUTTON_PASS_THROUGH);
}

static void test_install_button_cancels_download_until_exit_is_visible(void) {
    assert(app_center_exit_policy_button_action(APP_CENTER_EXIT_PAGE_INSTALL, false) ==
           APP_CENTER_EXIT_BUTTON_CANCEL_DOWNLOAD);
}

static void test_return_request_waits_for_download_cancellation(void) {
    assert(app_center_exit_policy_tick_action(false, false) == APP_CENTER_EXIT_TICK_NONE);
    assert(app_center_exit_policy_tick_action(false, true) == APP_CENTER_EXIT_TICK_NONE);
    assert(app_center_exit_policy_tick_action(true, true) == APP_CENTER_EXIT_TICK_CANCEL_DOWNLOAD);
    assert(app_center_exit_policy_tick_action(true, false) == APP_CENTER_EXIT_TICK_RETURN_TO_LAUNCHER);
}

static void test_top_gesture_reveals_exit_on_every_app_center_page(void) {
    for (size_t index = 0; index < sizeof(all_pages) / sizeof(all_pages[0]); ++index) {
        assert(app_center_exit_policy_gesture_reveals_exit(all_pages[index]));
    }
}

int main(void) {
    test_visible_exit_button_always_returns_to_launcher();
    test_wifi_setup_button_reveals_exit_before_returning();
    test_list_and_detail_buttons_keep_page_actions_until_exit_is_visible();
    test_install_button_cancels_download_until_exit_is_visible();
    test_return_request_waits_for_download_cancellation();
    test_top_gesture_reveals_exit_on_every_app_center_page();

    puts("app_center_exit_policy tests passed");
    return 0;
}
