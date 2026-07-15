#ifndef WATCHER_FACTORY_SETTINGS_UI_INTERNAL_H
#define WATCHER_FACTORY_SETTINGS_UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "factory_settings_original_ui/ui.h"
#include "factory_settings_scroll_curve.h"
#include "factory_settings_ui.h"

#define SETTINGS_FOCUS_MAX 12
#define SETTINGS_GREEN 0xA9DE2C
#define SETTINGS_SOUND_SELECTION_COLOR 0x8FC31F
#define SETTINGS_BRIGHTNESS_SELECTION_COLOR 0x1F6DC3
#define SETTINGS_SET_SCROLL_RADIUS FACTORY_SETTINGS_SCROLL_RADIUS
#define SETTINGS_SWITCH_UNLOCK_MS 1000
#define SETTINGS_RGB_TOGGLE_DEBOUNCE_MS 500
#define MAX_OBJECTS_IN_GROUP SETTINGS_FOCUS_MAX
#define UI_CALLER 0

typedef struct {
    lv_obj_t *group[MAX_OBJECTS_IN_GROUP];
    uint8_t obj_count;
} GroupInfo;

typedef struct {
    lv_obj_t *g_prepage;
    lv_obj_t *g_curpage;
    lv_obj_t *g_prefocused_obj;
    lv_obj_t *g_curfocused_obj;
} lv_pm_page_record;

typedef enum {
    PM_ADD_OBJS_TO_GROUP,
    PM_NO_OPERATION,
    PM_CLEAR_GROUP,
} pm_operation_t;

typedef enum {
    SETTINGS_PAGE_SET = 0,
    SETTINGS_PAGE_SLIDER,
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_SWIPE,
    SETTINGS_PAGE_SLEEP,
    SETTINGS_PAGE_CONNECT,
    SETTINGS_PAGE_NETWORK,
} settings_page_t;

typedef enum {
    SETTINGS_SET_FOCUS_BACK = 0,
    SETTINGS_SET_FOCUS_REBOOT,
    SETTINGS_SET_FOCUS_WIFI,
    SETTINGS_SET_FOCUS_SOUND,
    SETTINGS_SET_FOCUS_BRIGHTNESS,
    SETTINGS_SET_FOCUS_RGB,
    SETTINGS_SET_FOCUS_ABOUT,
    SETTINGS_SET_FOCUS_FACTORY_RESET,
    SETTINGS_SET_FOCUS_COUNT,
} settings_set_focus_t;

typedef struct {
    int32_t vs_value;
    int32_t bs_value;
} factory_settings_volbri_t;

extern lv_group_t *s_group;
extern factory_settings_slider_t s_slider_kind;
extern factory_settings_item_t s_confirm_item;
extern factory_settings_state_t s_state;
extern factory_settings_callbacks_t s_callbacks;
extern factory_settings_volbri_t volbri;
extern uint8_t g_swipeid;
extern uint8_t g_shutdown;
extern int g_screenoff_time;
extern int g_screenoff_switch;

extern GroupInfo group_page_volume;
extern GroupInfo group_page_brightness;
extern GroupInfo group_page_about;
extern GroupInfo group_page_swipe;
extern GroupInfo group_page_sleep;
extern GroupInfo group_page_connectapp;
extern GroupInfo group_page_network;

int clamp_percent(int value, int min_value);
const char *safe_text(const char *text, const char *fallback);
void ensure_page_initialized(lv_obj_t **page, void (*init_fn)(void));
void remember_set_focus(settings_set_focus_t focus);
void open_set_page(void);
void open_network_page(void);
void lv_pm_open_page(lv_group_t *group, GroupInfo *group_info, pm_operation_t operation, lv_obj_t **target,
                     lv_scr_load_anim_t fademode, int speed, int delay, void (*target_init)(void));
void note_original_page_opened(settings_page_t page_kind, GroupInfo *group_info, uint8_t focus_index, bool editing,
                               bool wrap);
void preserve_slider_editing_on_touch(lv_event_t *event);
bool factory_settings_ui_handle_network_cancel(void);
void update_about_labels(void);
void set_obj_style_defocused(lv_obj_t *obj, lv_obj_t *obj_text);
void set_obj_style_focused(lv_obj_t *obj, lv_obj_t *obj_text);
void Page_ConnAPP_Mate(void);
void Page_ConnAPP_BLE(void);
int get_ble_switch(int caller);
int set_ble_switch(int caller, int value);
int get_rgb_switch(int caller);
int set_rgb_switch(int caller, int value);
int set_sound(int caller, int value);
int set_brightness(int caller, int value);
int set_screenoff_time(int caller, int value);
int get_screenoff_time(int caller);
int set_screenoff_switch(int caller, int value);
int get_screenoff_switch(int caller);
void Page_shutdown(void);
void Page_facreset(void);

#endif
