#ifndef WATCHER_FACTORY_SETTINGS_UI_H
#define WATCHER_FACTORY_SETTINGS_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACTORY_SETTINGS_ITEM_BACK = 0,
    FACTORY_SETTINGS_ITEM_REBOOT,
    FACTORY_SETTINGS_ITEM_WIFI,
    FACTORY_SETTINGS_ITEM_BLUETOOTH,
    FACTORY_SETTINGS_ITEM_SOUND,
    FACTORY_SETTINGS_ITEM_BRIGHTNESS,
    FACTORY_SETTINGS_ITEM_SCREEN_OFF,
    FACTORY_SETTINGS_ITEM_RGB,
    FACTORY_SETTINGS_ITEM_ABOUT,
    FACTORY_SETTINGS_ITEM_FACTORY_RESET,
    FACTORY_SETTINGS_ITEM_COUNT,
} factory_settings_item_t;

typedef enum {
    FACTORY_SETTINGS_SLIDER_SOUND = 0,
    FACTORY_SETTINGS_SLIDER_BRIGHTNESS,
} factory_settings_slider_t;

typedef struct {
    const char *device_name;
    const char *firmware_version;
    const char *idf_version;
    const char *esp32_git_ref;
    const char *stm32_git_ref;
    const char *resource_bundle_version;
    const char *ble_mac;
    const char *wifi_mac;
} factory_settings_about_t;

typedef struct {
    int sound_percent;
    int brightness_percent;
    bool rgb_enabled;
    const char *wifi_status;
    const char *wifi_ssid;
    const char *wifi_ip;
    bool wifi_connected;
    bool wifi_configured;
    const char *ble_mac;
    bool ble_connected;
    bool ble_advertising;
    const char *bluetooth_status;
    const char *network_wait_title;
    const char *network_wait_hint;
    const char *network_connected_title;
    const char *network_connected_hint;
    bool network_use_ble_icon;
    uint32_t network_ble_mac_color;
    factory_settings_about_t about;
} factory_settings_state_t;

typedef struct {
    void (*on_back)(void *user_ctx);
    void (*on_reboot)(void *user_ctx);
    void (*on_wifi)(void *user_ctx);
    void (*on_wifi_detail_closed)(void *user_ctx);
    bool (*on_wifi_disconnect)(void *user_ctx);
    bool (*on_bluetooth_changed)(bool enabled, void *user_ctx);
    void (*on_sound_changed)(int value, void *user_ctx);
    void (*on_brightness_changed)(int value, void *user_ctx);
    void (*on_screen_off)(void *user_ctx);
    bool (*on_rgb_changed)(bool enabled, void *user_ctx);
    void (*on_factory_reset)(void *user_ctx);
    void *user_ctx;
} factory_settings_callbacks_t;

void factory_settings_ui_build(lv_obj_t *screen,
                               const factory_settings_state_t *state,
                               const factory_settings_callbacks_t *callbacks);
void factory_settings_ui_build_with_previous_screen_delete(lv_obj_t *screen,
                                                           const factory_settings_state_t *state,
                                                           const factory_settings_callbacks_t *callbacks,
                                                           bool auto_delete_previous_screen);
void factory_settings_ui_build_wifi_detail_with_previous_screen_delete(lv_obj_t *screen,
                                                                       const factory_settings_state_t *state,
                                                                       const factory_settings_callbacks_t *callbacks,
                                                                       bool auto_delete_previous_screen);
void factory_settings_ui_build_network_page(lv_obj_t *screen,
                                            const factory_settings_state_t *state,
                                            const factory_settings_callbacks_t *callbacks,
                                            lv_group_t **group,
                                            bool auto_delete_previous_screen);
void factory_settings_ui_bind_group(lv_group_t **group);
void factory_settings_ui_reset(void);
void factory_settings_ui_click_focused(void);
void factory_settings_ui_update_state(const factory_settings_state_t *state);
bool factory_settings_ui_open_wifi_detail(void);
void factory_settings_ui_log_debug_snapshot(const char *stage);
bool factory_settings_ui_debug_open_page(const char *page_id);
bool factory_settings_ui_debug_focus(const char *target_id);
bool factory_settings_ui_debug_rotate(int steps);
bool factory_settings_ui_debug_click(void);
const char *factory_settings_ui_debug_page_name(void);
const char *factory_settings_ui_debug_focus_name(void);

#ifdef __cplusplus
}
#endif

#endif
