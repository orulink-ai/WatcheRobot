#include "factory_settings_ui_internal.h"

#include "app_center.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "factory_settings_perf_stats.h"
#include "sdkconfig.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_UI_DIAG_TAG "UI_DIAG"

lv_obj_t *ui_Page_Set = NULL;
lv_obj_t *ui_Set_panel = NULL;
lv_obj_t *ui_setback = NULL;
lv_obj_t *ui_Image1 = NULL;
lv_obj_t *ui_setbackt = NULL;
lv_obj_t *ui_setdown = NULL;
lv_obj_t *ui_setdownt = NULL;
lv_obj_t *ui_setapp = NULL;
lv_obj_t *ui_setappt = NULL;
lv_obj_t *ui_setwifi = NULL;
lv_obj_t *ui_setwifit = NULL;
lv_obj_t *ui_setble = NULL;
lv_obj_t *ui_setblet = NULL;
lv_obj_t *ui_setblesw = NULL;
lv_obj_t *ui_setvol = NULL;
lv_obj_t *ui_setvolt = NULL;
lv_obj_t *ui_setbri = NULL;
lv_obj_t *ui_setbrit = NULL;
lv_obj_t *ui_settime = NULL;
lv_obj_t *ui_settimt = NULL;
lv_obj_t *ui_setrgb = NULL;
lv_obj_t *ui_setrgbt = NULL;
lv_obj_t *ui_setrgbsw = NULL;
lv_obj_t *ui_setww = NULL;
lv_obj_t *ui_setwwt = NULL;
lv_obj_t *ui_setwwsw = NULL;
lv_obj_t *ui_setdev = NULL;
lv_obj_t *ui_setdevt = NULL;
lv_obj_t *ui_setfac = NULL;
lv_obj_t *ui_setfact = NULL;
lv_obj_t *ui_settextp = NULL;
lv_obj_t *ui_Set_title = NULL;
lv_obj_t *ui_scontrolp = NULL;

lv_obj_t *ui_Page_Slider = NULL;
lv_obj_t *ui_bvpb = NULL;
lv_obj_t *ui_bvbimg = NULL;
lv_obj_t *ui_bvbt = NULL;
lv_obj_t *ui_bvbv = NULL;
lv_obj_t *ui_bvs = NULL;
lv_obj_t *ui_bvtp = NULL;
lv_obj_t *ui_bvtitle = NULL;
lv_obj_t *ui_bvback = NULL;
lv_obj_t *ui_vp = NULL;
lv_obj_t *ui_vslider = NULL;
lv_obj_t *ui_bp = NULL;
lv_obj_t *ui_bslider = NULL;

lv_obj_t *ui_Page_About = NULL;
lv_obj_t *ui_AboutP = NULL;
lv_obj_t *ui_aboutdevname = NULL;
lv_obj_t *ui_devnamet1 = NULL;
lv_obj_t *ui_devnamet2 = NULL;
lv_obj_t *ui_aboutespversion = NULL;
lv_obj_t *ui_espversiont1 = NULL;
lv_obj_t *ui_espversiont2 = NULL;
lv_obj_t *ui_aboutaiversion = NULL;
lv_obj_t *ui_aiversion1 = NULL;
lv_obj_t *ui_aiversion2 = NULL;
lv_obj_t *ui_aboutsn = NULL;
lv_obj_t *ui_snt1 = NULL;
lv_obj_t *ui_snt2 = NULL;
lv_obj_t *ui_abouteui = NULL;
lv_obj_t *ui_euit1 = NULL;
lv_obj_t *ui_euit2 = NULL;
lv_obj_t *ui_aboutblemac = NULL;
lv_obj_t *ui_blet1 = NULL;
lv_obj_t *ui_blet2 = NULL;
lv_obj_t *ui_aboutwifimac = NULL;
lv_obj_t *ui_wifit1 = NULL;
lv_obj_t *ui_wifit2 = NULL;
lv_obj_t *ui_Paboutb = NULL;
lv_obj_t *ui_abtp = NULL;
lv_obj_t *ui_abtitle = NULL;
static lv_obj_t *ui_about_apps = NULL;
static lv_obj_t *ui_about_apps_value = NULL;
static lv_obj_t *ui_about_apps_hint = NULL;
static lv_obj_t *ui_about_storage = NULL;
static lv_obj_t *ui_about_storage_value = NULL;
static lv_obj_t *ui_about_memory = NULL;
static lv_obj_t *ui_about_memory_value = NULL;
static lv_obj_t *ui_about_ota_slot = NULL;
static lv_obj_t *ui_about_ota_slot_value = NULL;
static lv_obj_t *ui_about_resource_bundle = NULL;
static lv_obj_t *ui_about_resource_bundle_value = NULL;

lv_obj_t *ui_Page_Swipe = NULL;
lv_obj_t *ui_spsilder = NULL;
lv_obj_t *ui_spback = NULL;
lv_obj_t *ui_sptext = NULL;
lv_obj_t *ui_swipep = NULL;
lv_obj_t *ui_sptitle = NULL;
lv_obj_t *ui_swipep2 = NULL;
lv_obj_t *ui_sptext2 = NULL;
lv_obj_t *ui_Spinner4 = NULL;

lv_obj_t *ui_Page_Sleep = NULL;
lv_obj_t *ui_sleeptimeroller = NULL;
lv_obj_t *ui_slpback = NULL;
lv_obj_t *ui_sleepswitchp = NULL;
lv_obj_t *ui_sleepswitcht = NULL;
lv_obj_t *ui_sleepswitch = NULL;

lv_obj_t *ui_Page_Connect = NULL;
lv_obj_t *ui_conn_arcl = NULL;
lv_obj_t *ui_conn_arcr = NULL;
lv_obj_t *ui_conn_panel1 = NULL;
lv_obj_t *ui_connp11 = NULL;
lv_obj_t *ui_connp12 = NULL;
lv_obj_t *ui_connp13 = NULL;
lv_obj_t *ui_connp14 = NULL;
lv_obj_t *ui_conn_panel2 = NULL;
lv_obj_t *ui_connp21 = NULL;
lv_obj_t *ui_connp22 = NULL;
lv_obj_t *ui_connp23 = NULL;
lv_obj_t *ui_connp24 = NULL;
lv_obj_t *ui_conn_QR = NULL;
lv_obj_t *ui_conncancel = NULL;
lv_obj_t *ui_connp1 = NULL;
lv_obj_t *ui_arrow1 = NULL;
lv_obj_t *ui_connp2 = NULL;
lv_obj_t *ui_arrow2 = NULL;

lv_obj_t *ui_Page_Network = NULL;
lv_obj_t *ui_wifip1 = NULL;
lv_obj_t *ui_wifiicon = NULL;
lv_obj_t *ui_wifissid = NULL;
lv_obj_t *ui_wifibtnt = NULL;
lv_obj_t *ui_wifimqtt = NULL;
lv_obj_t *ui_wifip3 = NULL;
lv_obj_t *ui_wifitext3 = NULL;
lv_obj_t *ui_wifilogo = NULL;
lv_obj_t *ui_wifitext2 = NULL;
lv_obj_t *ui_wificancel = NULL;
static lv_obj_t *ui_wifi_disconnect = NULL;
static lv_obj_t *ui_wifi_disconnect_label = NULL;

lv_group_t *s_group = NULL;
static settings_page_t s_page = SETTINGS_PAGE_SET;
factory_settings_slider_t s_slider_kind = FACTORY_SETTINGS_SLIDER_SOUND;
factory_settings_item_t s_confirm_item = FACTORY_SETTINGS_ITEM_REBOOT;
factory_settings_state_t s_state = {0};
factory_settings_callbacks_t s_callbacks = {0};
static uint8_t s_last_set_focus_index = SETTINGS_SET_FOCUS_BACK;
static lv_obj_t *s_set_scroll_anim_panel = NULL;
static int32_t s_set_scroll_center_y = 0;
static factory_settings_scroll_curve_t s_set_scroll_curve = {0};
static bool s_restore_slider_editing_after_touch_focus = false;
static lv_timer_t *s_ble_switch_unlock_timer = NULL;
static lv_pm_page_record g_page_record = {0};
static bool s_network_disconnect_focusable = false;
static bool s_network_standalone = false;
static bool s_set_defocused_fill_is_redundant = false;
typedef struct {
    lv_img_dsc_t *back;
    lv_img_dsc_t *row;
    bool ready;
    bool attempted;
} settings_focus_background_cache_t;
static settings_focus_background_cache_t s_set_focus_background_cache = {0};
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
static factory_settings_perf_stats_t s_set_perf_stats = {0};
static lv_disp_drv_t *s_set_perf_disp_drv = NULL;
static void (*s_set_perf_previous_monitor_cb)(lv_disp_drv_t *disp_drv, uint32_t time, uint32_t px) = NULL;
#endif
factory_settings_volbri_t volbri = {0};
uint8_t g_swipeid = 0;
uint8_t g_shutdown = 0;
int g_screenoff_time = 0;
int g_screenoff_switch = 1;

static GroupInfo group_page_set = {0};
GroupInfo group_page_volume = {0};
GroupInfo group_page_brightness = {0};
GroupInfo group_page_about = {0};
GroupInfo group_page_swipe = {0};
GroupInfo group_page_sleep = {0};
GroupInfo group_page_connectapp = {0};
GroupInfo group_page_network = {0};

#define NETWORK_DISCONNECT_BUTTON_WIDTH 184
#define NETWORK_DISCONNECT_BUTTON_HEIGHT 42
#define NETWORK_DISCONNECT_BUTTON_Y 54
#define NETWORK_CANCEL_BUTTON_Y 146
#define NETWORK_CANCEL_BUTTON_HEIGHT 60
#define NETWORK_DISCONNECT_CANCEL_MIN_GAP 36
#define NETWORK_DISCONNECT_CANCEL_GAP_PX                                                                               \
    ((NETWORK_CANCEL_BUTTON_Y - (NETWORK_CANCEL_BUTTON_HEIGHT / 2)) -                                                  \
     (NETWORK_DISCONNECT_BUTTON_Y + (NETWORK_DISCONNECT_BUTTON_HEIGHT / 2)))

#if NETWORK_DISCONNECT_CANCEL_GAP_PX < NETWORK_DISCONNECT_CANCEL_MIN_GAP
#error "Network disconnect button is too close to cancel button"
#endif

static void rebind_group(lv_obj_t *const *objects, uint8_t count, uint8_t focus_index, bool editing, bool wrap);
static void settings_enable_set_scroll_anim(void);
static void init_settings_groups(void);
static void ensure_about_system_cards(void);
static void reset_about_system_cards(void);
static void update_about_system_labels(void);
static void update_network_page(void);
static unsigned long count_lvgl_tree_nodes(lv_obj_t *root);

int clamp_percent(int value, int min_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

const char *safe_text(const char *text, const char *fallback) {
    return text != NULL && text[0] != '\0' ? text : fallback;
}

static void unlock_ble_switch_timer_cb(lv_timer_t *timer) {
    if (timer == s_ble_switch_unlock_timer) {
        s_ble_switch_unlock_timer = NULL;
        if (ui_setblesw != NULL) {
            lv_obj_clear_state(ui_setblesw, LV_STATE_DISABLED);
        }
    }
    lv_timer_del(timer);
}

static void schedule_ble_switch_unlock(void) {
    if (ui_setblesw == NULL) {
        return;
    }
    lv_obj_add_state(ui_setblesw, LV_STATE_DISABLED);
    if (s_ble_switch_unlock_timer != NULL) {
        lv_timer_del(s_ble_switch_unlock_timer);
        s_ble_switch_unlock_timer = NULL;
    }
    s_ble_switch_unlock_timer = lv_timer_create(unlock_ble_switch_timer_cb, SETTINGS_SWITCH_UNLOCK_MS, NULL);
}

static void cancel_ble_switch_unlock(void) {
    if (s_ble_switch_unlock_timer != NULL) {
        lv_timer_del(s_ble_switch_unlock_timer);
        s_ble_switch_unlock_timer = NULL;
    }
}

static void bind_encoder_to_group(void) {
    lv_indev_t *indev = lv_indev_get_next(NULL);

    while (indev != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(indev, s_group);
        }
        indev = lv_indev_get_next(indev);
    }
}

static void addObjToGroup(GroupInfo *group_info, lv_obj_t *const *objects, uint8_t count) {
    if (group_info == NULL) {
        return;
    }
    if (count > MAX_OBJECTS_IN_GROUP) {
        count = MAX_OBJECTS_IN_GROUP;
    }

    memset(group_info->group, 0, sizeof(group_info->group));
    for (uint8_t i = 0; i < count; ++i) {
        group_info->group[i] = objects[i];
    }
    group_info->obj_count = count;
}

static void lv_pm_obj_group(lv_group_t *group, GroupInfo *group_info) {
    if (group == NULL || group_info == NULL) {
        return;
    }
    lv_group_remove_all_objs(group);
    for (uint8_t index = 0; index < group_info->obj_count; ++index) {
        if (group_info->group[index] != NULL) {
            lv_group_add_obj(group, group_info->group[index]);
        }
    }
}

static void lv_pm_open_page_internal(lv_group_t *group, GroupInfo *group_info, pm_operation_t operation,
                                     lv_obj_t **target, lv_scr_load_anim_t fademode, int speed, int delay,
                                     void (*target_init)(void), bool auto_delete_previous_screen) {
    if (target == NULL || target_init == NULL) {
        return;
    }

    if (g_page_record.g_curfocused_obj != NULL) {
        g_page_record.g_prefocused_obj = g_page_record.g_curfocused_obj;
    }
    g_page_record.g_curfocused_obj = group != NULL ? lv_group_get_focused(group) : NULL;

    if (g_page_record.g_curpage != NULL) {
        g_page_record.g_prepage = g_page_record.g_curpage;
    }
    if (*target == NULL) {
        target_init();
    }
    g_page_record.g_curpage = *target;
    if (g_page_record.g_curpage == NULL) {
        return;
    }

    switch (operation) {
    case PM_ADD_OBJS_TO_GROUP:
        lv_pm_obj_group(group, group_info);
        break;
    case PM_CLEAR_GROUP:
        if (group != NULL) {
            lv_group_remove_all_objs(group);
        }
        break;
    case PM_NO_OPERATION:
    default:
        break;
    }

    if (group != NULL && g_page_record.g_prefocused_obj != NULL) {
        lv_group_focus_obj(g_page_record.g_prefocused_obj);
    }
    lv_scr_load_anim(*target, fademode, speed, delay, auto_delete_previous_screen);
}

void lv_pm_open_page(lv_group_t *group, GroupInfo *group_info, pm_operation_t operation, lv_obj_t **target,
                     lv_scr_load_anim_t fademode, int speed, int delay, void (*target_init)(void)) {
    lv_pm_open_page_internal(group, group_info, operation, target, fademode, speed, delay, target_init, false);
}

static void rebind_group_info(GroupInfo *group_info, uint8_t focus_index, bool editing, bool wrap) {
    if (group_info == NULL) {
        return;
    }
    rebind_group(group_info->group, group_info->obj_count, focus_index, editing, wrap);
}

static void rebind_group(lv_obj_t *const *objects, uint8_t count, uint8_t focus_index, bool editing, bool wrap) {
    if (s_group == NULL) {
        return;
    }

    lv_group_remove_all_objs(s_group);
    lv_group_set_wrap(s_group, wrap);
    for (uint8_t i = 0; i < count; ++i) {
        if (objects[i] != NULL) {
            lv_group_add_obj(s_group, objects[i]);
        }
    }
    if (count > 0) {
        if (focus_index >= count) {
            focus_index = 0;
        }
        if (objects[focus_index] != NULL) {
            lv_group_focus_obj(objects[focus_index]);
        }
    }
    lv_group_set_editing(s_group, editing);
    bind_encoder_to_group();
}

static uint16_t settings_scroll_sqrt_integer(uint32_t value) {
    lv_sqrt_res_t result;

    lv_sqrt(value, &result, 0x8000);
    return result.i;
}

static int16_t settings_scroll_project_uncached(int32_t diff_y) {
    if (diff_y >= SETTINGS_SET_SCROLL_RADIUS) {
        return SETTINGS_SET_SCROLL_RADIUS;
    }

    const uint32_t x_sqr = SETTINGS_SET_SCROLL_RADIUS * SETTINGS_SET_SCROLL_RADIUS - diff_y * diff_y;
    return (int16_t)(SETTINGS_SET_SCROLL_RADIUS - settings_scroll_sqrt_integer(x_sqr));
}

#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
static void settings_perf_monitor_cb(lv_disp_drv_t *disp_drv, uint32_t time, uint32_t px) {
    if (s_page == SETTINGS_PAGE_SET && ui_Page_Set != NULL && lv_disp_get_scr_act(NULL) == ui_Page_Set) {
        factory_settings_perf_stats_record(&s_set_perf_stats, time, px);
    }
    if (s_set_perf_previous_monitor_cb != NULL) {
        s_set_perf_previous_monitor_cb(disp_drv, time, px);
    }
}

static void settings_perf_monitor_install(void) {
    lv_disp_t *disp = ui_Set_panel != NULL ? lv_obj_get_disp(ui_Set_panel) : NULL;
    lv_disp_drv_t *disp_drv = disp != NULL ? disp->driver : NULL;

    if (disp_drv == NULL) {
        return;
    }
    if (s_set_perf_disp_drv != disp_drv) {
        if (s_set_perf_disp_drv != NULL && s_set_perf_disp_drv->monitor_cb == settings_perf_monitor_cb) {
            s_set_perf_disp_drv->monitor_cb = s_set_perf_previous_monitor_cb;
        }
        s_set_perf_previous_monitor_cb = disp_drv->monitor_cb;
        disp_drv->monitor_cb = settings_perf_monitor_cb;
        s_set_perf_disp_drv = disp_drv;
    }
    factory_settings_perf_stats_reset(&s_set_perf_stats);
}

static void settings_perf_monitor_uninstall(void) {
    if (s_set_perf_disp_drv != NULL && s_set_perf_disp_drv->monitor_cb == settings_perf_monitor_cb) {
        s_set_perf_disp_drv->monitor_cb = s_set_perf_previous_monitor_cb;
    }
    s_set_perf_disp_drv = NULL;
    s_set_perf_previous_monitor_cb = NULL;
    factory_settings_perf_stats_reset(&s_set_perf_stats);
}
#endif

static void settings_set_scroll_cb(lv_event_t *event) {
    lv_obj_t *cont = lv_event_get_target(event);

    if (cont == NULL) {
        return;
    }

    for (uint8_t i = 0; i < group_page_set.obj_count; ++i) {
        lv_obj_t *child = group_page_set.group[i];
        lv_area_t child_area;
        int32_t child_y_center;
        int32_t diff_y;
        int16_t x;

        if (child == NULL || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }

        lv_obj_get_coords(child, &child_area);
        child_y_center = child_area.y1 + lv_area_get_height(&child_area) / 2;
        diff_y = LV_ABS(child_y_center - s_set_scroll_center_y);

        if (!factory_settings_scroll_curve_project(&s_set_scroll_curve, diff_y, &x)) {
            x = settings_scroll_project_uncached(diff_y);
        }
        if (lv_obj_get_style_translate_x(child, LV_PART_MAIN) == x) {
            continue;
        }
        lv_obj_set_style_translate_x(child, x, 0);
    }
}

static void settings_enable_set_scroll_anim(void) {
    if (ui_Set_panel == NULL) {
        return;
    }

    (void)factory_settings_scroll_curve_init(&s_set_scroll_curve, settings_scroll_sqrt_integer);
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    settings_perf_monitor_install();
#endif

    lv_obj_set_scroll_snap_y(ui_Set_panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(ui_Set_panel, LV_DIR_VER);
    if (s_set_scroll_anim_panel != ui_Set_panel) {
        lv_area_t panel_area;

        lv_obj_get_coords(ui_Set_panel, &panel_area);
        s_set_scroll_center_y = lv_area_get_height(&panel_area) / 2;
        lv_obj_add_event_cb(ui_Set_panel, settings_set_scroll_cb, LV_EVENT_SCROLL, NULL);
        lv_obj_scroll_to_view(lv_obj_get_child(ui_Set_panel, 0), LV_ANIM_OFF);
        s_set_scroll_anim_panel = ui_Set_panel;
    }
    lv_event_send(ui_Set_panel, LV_EVENT_SCROLL, NULL);
}

static lv_obj_t *set_focus_obj(uint8_t index) {
    switch ((settings_set_focus_t)index) {
    case SETTINGS_SET_FOCUS_BACK:
        return ui_setback;
    case SETTINGS_SET_FOCUS_REBOOT:
        return ui_setdown;
    case SETTINGS_SET_FOCUS_WIFI:
        return ui_setwifi;
    case SETTINGS_SET_FOCUS_SOUND:
        return ui_setvol;
    case SETTINGS_SET_FOCUS_BRIGHTNESS:
        return ui_setbri;
    case SETTINGS_SET_FOCUS_RGB:
        return ui_setrgb;
    case SETTINGS_SET_FOCUS_ABOUT:
        return ui_setdev;
    case SETTINGS_SET_FOCUS_FACTORY_RESET:
        return ui_setfac;
    default:
        return NULL;
    }
}

static lv_obj_t *set_focus_label(uint8_t index) {
    switch ((settings_set_focus_t)index) {
    case SETTINGS_SET_FOCUS_BACK:
        return ui_setbackt;
    case SETTINGS_SET_FOCUS_REBOOT:
        return ui_setdownt;
    case SETTINGS_SET_FOCUS_WIFI:
        return ui_setwifit;
    case SETTINGS_SET_FOCUS_SOUND:
        return ui_setvolt;
    case SETTINGS_SET_FOCUS_BRIGHTNESS:
        return ui_setbrit;
    case SETTINGS_SET_FOCUS_RGB:
        return ui_setrgbt;
    case SETTINGS_SET_FOCUS_ABOUT:
        return ui_setdevt;
    case SETTINGS_SET_FOCUS_FACTORY_RESET:
        return ui_setfact;
    default:
        return NULL;
    }
}

static lv_obj_t *set_focus_switch(uint8_t index) {
    switch ((settings_set_focus_t)index) {
    case SETTINGS_SET_FOCUS_RGB:
        return ui_setrgbsw;
    default:
        return NULL;
    }
}

static const char *set_focus_name(settings_set_focus_t focus) {
    switch (focus) {
    case SETTINGS_SET_FOCUS_BACK:
        return "back";
    case SETTINGS_SET_FOCUS_REBOOT:
        return "reboot";
    case SETTINGS_SET_FOCUS_WIFI:
        return "wifi";
    case SETTINGS_SET_FOCUS_SOUND:
        return "sound";
    case SETTINGS_SET_FOCUS_BRIGHTNESS:
        return "brightness";
    case SETTINGS_SET_FOCUS_RGB:
        return "rgb";
    case SETTINGS_SET_FOCUS_ABOUT:
        return "about";
    case SETTINGS_SET_FOCUS_FACTORY_RESET:
        return "factory_reset";
    default:
        return "unknown";
    }
}

static int find_set_focus_index(lv_obj_t *obj) {
    for (uint8_t i = 0; i < SETTINGS_SET_FOCUS_COUNT; ++i) {
        if (set_focus_obj(i) == obj) {
            return (int)i;
        }
    }
    return -1;
}

static bool settings_has_opaque_black_canvas(void) {
    if (ui_Page_Set == NULL || ui_Set_panel == NULL) {
        return false;
    }

    return lv_color_to32(lv_obj_get_style_bg_color(ui_Page_Set, LV_PART_MAIN)) == 0xFF000000U &&
           lv_obj_get_style_bg_opa(ui_Page_Set, LV_PART_MAIN) == LV_OPA_COVER &&
           lv_obj_get_style_bg_opa(ui_Set_panel, LV_PART_MAIN) == LV_OPA_TRANSP;
}

void set_obj_style_defocused(lv_obj_t *obj, lv_obj_t *obj_text) {
    const bool is_back = obj == ui_setback;

    if (obj == NULL) {
        return;
    }
    lv_obj_set_width(obj, is_back ? 300 : 412);
    lv_obj_set_height(obj, 50);
    lv_obj_set_style_radius(obj, 45, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(obj, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, s_set_defocused_fill_is_redundant ? LV_OPA_TRANSP : 120,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(obj, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, s_set_defocused_fill_is_redundant ? LV_OPA_TRANSP : LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    if (obj_text != NULL) {
        lv_obj_set_style_text_font(obj_text, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void set_obj_style_focused(lv_obj_t *obj, lv_obj_t *obj_text) {
    const bool is_back = obj == ui_setback;
    const lv_img_dsc_t *background = is_back ? s_set_focus_background_cache.back : s_set_focus_background_cache.row;

    if (obj == NULL) {
        return;
    }
    lv_obj_set_width(obj, is_back ? 300 : 380);
    lv_obj_set_height(obj, 90);
    lv_obj_set_style_radius(obj, 45, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (s_set_focus_background_cache.ready && background != NULL) {
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_src(obj, background, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_tiled(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_bg_img_src(obj, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(SETTINGS_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(obj, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (obj_text != NULL) {
        lv_obj_set_style_text_font(obj_text, &ui_font_font_bold, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void settings_restore_hidden_flag(lv_obj_t *obj, bool was_hidden) {
    if (obj == NULL) {
        return;
    }
    if (was_hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_focus_background_cache_release(void) {
    if (ui_Page_Set != NULL &&
        (s_set_focus_background_cache.back != NULL || s_set_focus_background_cache.row != NULL)) {
        for (uint8_t index = 0; index < SETTINGS_SET_FOCUS_COUNT; ++index) {
            lv_obj_t *row = set_focus_obj(index);
            if (row != NULL) {
                lv_obj_set_style_bg_img_src(row, NULL, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }
    if (s_set_focus_background_cache.back != NULL) {
        lv_snapshot_free(s_set_focus_background_cache.back);
    }
    if (s_set_focus_background_cache.row != NULL) {
        lv_snapshot_free(s_set_focus_background_cache.row);
    }
    memset(&s_set_focus_background_cache, 0, sizeof(s_set_focus_background_cache));
}

static bool settings_focus_background_cache_build(void) {
    bool back_label_was_hidden;
    bool back_icon_was_hidden;
    bool row_label_was_hidden;

    if (s_set_focus_background_cache.attempted) {
        return s_set_focus_background_cache.ready;
    }
    s_set_focus_background_cache.attempted = true;
    if (!s_set_defocused_fill_is_redundant || ui_setback == NULL || ui_setbackt == NULL || ui_setdown == NULL ||
        ui_setdownt == NULL) {
        return false;
    }

    back_label_was_hidden = lv_obj_has_flag(ui_setbackt, LV_OBJ_FLAG_HIDDEN);
    back_icon_was_hidden = ui_Image1 == NULL || lv_obj_has_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_setbackt, LV_OBJ_FLAG_HIDDEN);
    if (ui_Image1 != NULL) {
        lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
    set_obj_style_focused(ui_setback, NULL);
    s_set_focus_background_cache.back = lv_snapshot_take(ui_setback, LV_IMG_CF_TRUE_COLOR);
    set_obj_style_defocused(ui_setback, NULL);
    settings_restore_hidden_flag(ui_setbackt, back_label_was_hidden);
    settings_restore_hidden_flag(ui_Image1, back_icon_was_hidden);

    row_label_was_hidden = lv_obj_has_flag(ui_setdownt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_setdownt, LV_OBJ_FLAG_HIDDEN);
    set_obj_style_focused(ui_setdown, NULL);
    s_set_focus_background_cache.row = lv_snapshot_take(ui_setdown, LV_IMG_CF_TRUE_COLOR);
    set_obj_style_defocused(ui_setdown, NULL);
    settings_restore_hidden_flag(ui_setdownt, row_label_was_hidden);

    if (s_set_focus_background_cache.back == NULL || s_set_focus_background_cache.row == NULL) {
        settings_focus_background_cache_release();
        s_set_focus_background_cache.attempted = true;
        return false;
    }
    s_set_focus_background_cache.ready = true;
    return true;
}

static void update_set_switches(void) {
    if (ui_setrgbsw != NULL) {
        if (s_state.rgb_enabled) {
            lv_obj_add_state(ui_setrgbsw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_setrgbsw, LV_STATE_CHECKED);
        }
    }
    if (ui_setblesw != NULL) {
        if (s_state.bluetooth_status != NULL && strcmp(s_state.bluetooth_status, "Closed") != 0) {
            lv_obj_add_state(ui_setblesw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_setblesw, LV_STATE_CHECKED);
        }
    }
}

static void configure_set_page(void) {
    s_set_defocused_fill_is_redundant = settings_has_opaque_black_canvas();

    if (ui_setdownt != NULL) {
        lv_label_set_text(ui_setdownt, "Reboot");
    }
    if (ui_setapp != NULL) {
        lv_obj_add_flag(ui_setapp, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_setble != NULL) {
        lv_obj_add_flag(ui_setble, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_setww != NULL) {
        lv_obj_add_flag(ui_setww, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_settime != NULL) {
        lv_obj_add_flag(ui_settime, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_setblesw != NULL) {
        lv_obj_add_flag(ui_setblesw, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_setrgbsw != NULL) {
        lv_obj_add_flag(ui_setrgbsw, LV_OBJ_FLAG_HIDDEN);
    }
    update_set_switches();
    (void)settings_focus_background_cache_build();
}

static void focus_set_row(uint8_t index, lv_anim_enable_t anim) {
    lv_obj_t *obj = set_focus_obj(index);

    if (obj == NULL) {
        return;
    }
    for (uint8_t i = 0; i < SETTINGS_SET_FOCUS_COUNT; ++i) {
        lv_obj_t *row = set_focus_obj(i);
        lv_obj_t *label = set_focus_label(i);
        lv_obj_t *sw = set_focus_switch(i);

        set_obj_style_defocused(row, label);
        if (sw != NULL) {
            lv_obj_add_flag(sw, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (ui_Image1 != NULL) {
        lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }

    set_obj_style_focused(obj, set_focus_label(index));
    if (set_focus_switch(index) != NULL) {
        lv_obj_clear_flag(set_focus_switch(index), LV_OBJ_FLAG_HIDDEN);
    }
    if (obj == ui_setback && ui_Image1 != NULL) {
        lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_scroll_to_view(obj, anim);
    if (ui_Set_panel != NULL) {
        lv_event_send(ui_Set_panel, LV_EVENT_SCROLL, NULL);
    }
    s_last_set_focus_index = index;
}

void remember_set_focus(settings_set_focus_t focus) {
    if (focus >= SETTINGS_SET_FOCUS_COUNT) {
        return;
    }
    s_last_set_focus_index = (uint8_t)focus;
}

static void reset_about_system_cards(void) {
    ui_about_apps = NULL;
    ui_about_apps_value = NULL;
    ui_about_apps_hint = NULL;
    ui_about_storage = NULL;
    ui_about_storage_value = NULL;
    ui_about_memory = NULL;
    ui_about_memory_value = NULL;
    ui_about_ota_slot = NULL;
    ui_about_ota_slot_value = NULL;
    ui_about_resource_bundle = NULL;
    ui_about_resource_bundle_value = NULL;
}

static lv_obj_t *create_about_system_card(const char *title, lv_obj_t **value_label) {
    lv_obj_t *card = NULL;
    lv_obj_t *title_label = NULL;
    lv_obj_t *value = NULL;

    if (ui_AboutP == NULL || value_label == NULL) {
        return NULL;
    }

    card = lv_obj_create(ui_AboutP);
    lv_obj_set_width(card, 300);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_align(card, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(card, lv_color_hex(SETTINGS_GREEN), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(card, 255, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(card, 1, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(card, 0, LV_PART_MAIN | LV_STATE_FOCUSED);

    title_label = lv_label_create(card);
    lv_obj_set_width(title_label, LV_SIZE_CONTENT);
    lv_obj_set_height(title_label, LV_SIZE_CONTENT);
    lv_obj_set_align(title_label, LV_ALIGN_LEFT_MID);
    lv_label_set_text(title_label, title != NULL ? title : "");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    value = lv_label_create(card);
    lv_obj_set_width(value, 260);
    lv_obj_set_height(value, LV_SIZE_CONTENT);
    lv_obj_set_align(value, LV_ALIGN_LEFT_MID);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_label_set_text(value, "-");
    lv_obj_add_flag(value, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_text_color(value, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(value, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    *value_label = value;
    return card;
}

#if defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
static void format_about_memory(uint32_t free_bytes, uint32_t largest_free_block, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%luKB free / %luKB max", (unsigned long)(free_bytes / 1024U),
             (unsigned long)(largest_free_block / 1024U));
}
#endif

static void ensure_about_system_cards(void) {
    if (ui_Page_About == NULL || ui_AboutP == NULL || ui_about_apps != NULL) {
        return;
    }

    ui_about_apps = create_about_system_card("Apps", &ui_about_apps_value);
    if (ui_about_apps != NULL) {
        ui_about_apps_hint = lv_label_create(ui_about_apps);
        lv_obj_set_width(ui_about_apps_hint, 260);
        lv_obj_set_height(ui_about_apps_hint, LV_SIZE_CONTENT);
        lv_obj_set_align(ui_about_apps_hint, LV_ALIGN_LEFT_MID);
        lv_label_set_long_mode(ui_about_apps_hint, LV_LABEL_LONG_DOT);
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
        lv_label_set_text(ui_about_apps_hint, "Manage in App.Center");
#else
        lv_label_set_text(ui_about_apps_hint, "Managed by phone control");
#endif
        lv_obj_set_style_text_color(ui_about_apps_hint, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(ui_about_apps_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_about_apps_hint, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    ui_about_storage = create_about_system_card("Storage", &ui_about_storage_value);
    ui_about_memory = create_about_system_card("Memory", &ui_about_memory_value);
    ui_about_ota_slot = create_about_system_card("OTA Slot", &ui_about_ota_slot_value);
    ui_about_resource_bundle = create_about_system_card("Resource Bundle", &ui_about_resource_bundle_value);
    if (ui_Paboutb != NULL) {
        lv_obj_move_foreground(ui_Paboutb);
    }
}

static void update_about_system_labels(void) {
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    app_center_manager_snapshot_t snapshot;
    esp_err_t snapshot_ret;
#endif
    multi_heap_info_t internal_info = {0};
    multi_heap_info_t dma_info = {0};
    multi_heap_info_t psram_info = {0};
    char apps_text[48] = {0};
    char storage_text[48] = {0};
    char memory_text[48] = {0};
    char ota_slot_text[64] = {0};

    ensure_about_system_cards();
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    snapshot_ret = app_center_get_cached_manager_snapshot(&snapshot);
    if (snapshot_ret == ESP_OK) {
        app_center_manager_format_app_count(snapshot.installed_count, apps_text, sizeof(apps_text));
        if (snapshot.spiffs_available) {
            app_center_manager_format_bytes_pair(snapshot.spiffs_used_bytes, snapshot.spiffs_total_bytes, storage_text,
                                                 sizeof(storage_text));
        } else {
            snprintf(storage_text, sizeof(storage_text), "Unavailable");
        }
        app_center_manager_format_ota_slot(snapshot.firmware_app_name, snapshot.firmware_app_installed, ota_slot_text,
                                           sizeof(ota_slot_text));
    } else {
        snprintf(apps_text, sizeof(apps_text), "Updating...");
        snprintf(storage_text, sizeof(storage_text), "Updating...");
        snprintf(ota_slot_text, sizeof(ota_slot_text), "Updating...");
    }
#else
    snprintf(apps_text, sizeof(apps_text), "Phone Control");
    snprintf(storage_text, sizeof(storage_text), "Unavailable");
    snprintf(ota_slot_text, sizeof(ota_slot_text), "Firmware app");
#endif

    heap_caps_get_info(&internal_info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&dma_info, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    heap_caps_get_info(&psram_info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    (void)dma_info.total_free_bytes;
    (void)psram_info.total_free_bytes;
#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)
    app_center_manager_format_memory(internal_info.total_free_bytes, internal_info.largest_free_block, memory_text,
                                     sizeof(memory_text));
#else
    format_about_memory(internal_info.total_free_bytes, internal_info.largest_free_block, memory_text,
                        sizeof(memory_text));
#endif

    if (ui_about_apps_value != NULL) {
        lv_label_set_text(ui_about_apps_value, apps_text);
    }
    if (ui_about_storage_value != NULL) {
        lv_label_set_text(ui_about_storage_value, storage_text);
    }
    if (ui_about_memory_value != NULL) {
        lv_label_set_text(ui_about_memory_value, memory_text);
    }
    if (ui_about_ota_slot_value != NULL) {
        lv_label_set_text(ui_about_ota_slot_value, ota_slot_text);
    }
    if (ui_about_resource_bundle_value != NULL) {
        lv_label_set_text(ui_about_resource_bundle_value,
                          safe_text(s_state.about.resource_bundle_version, "Legacy / Unversioned"));
    }
}

void open_set_page(void);

static void init_settings_groups(void) {
    if (ui_Page_Set != NULL) {
        lv_obj_t *set_objects[] = {ui_setback, ui_setdown, ui_setwifi, ui_setvol,
                                   ui_setbri,  ui_setrgb,  ui_setdev,  ui_setfac};
        addObjToGroup(&group_page_set, set_objects, (uint8_t)(sizeof(set_objects) / sizeof(set_objects[0])));
    }
    if (ui_Page_Slider != NULL) {
        lv_obj_t *volume_objects[] = {ui_vslider, ui_bvback};
        lv_obj_t *brightness_objects[] = {ui_bslider, ui_bvback};
        addObjToGroup(&group_page_volume, volume_objects,
                      (uint8_t)(sizeof(volume_objects) / sizeof(volume_objects[0])));
        addObjToGroup(&group_page_brightness, brightness_objects,
                      (uint8_t)(sizeof(brightness_objects) / sizeof(brightness_objects[0])));
    }
    if (ui_Page_About != NULL) {
        lv_obj_t *about_objects[] = {ui_aboutdevname,  ui_aboutespversion, ui_aboutaiversion, ui_aboutsn,
                                     ui_abouteui,      ui_aboutblemac,     ui_aboutwifimac,   ui_about_apps,
                                     ui_about_storage, ui_about_memory,    ui_about_ota_slot, ui_about_resource_bundle,
                                     ui_Paboutb};
        addObjToGroup(&group_page_about, about_objects, (uint8_t)(sizeof(about_objects) / sizeof(about_objects[0])));
    }
    if (ui_Page_Swipe != NULL) {
        lv_obj_t *swipe_objects[] = {ui_spback};
        addObjToGroup(&group_page_swipe, swipe_objects, (uint8_t)(sizeof(swipe_objects) / sizeof(swipe_objects[0])));
    }
}

void ensure_page_initialized(lv_obj_t **page, void (*init_fn)(void)) {
    if (page != NULL && *page == NULL && init_fn != NULL) {
        init_fn();
        if (page == &ui_Page_About) {
            ensure_about_system_cards();
        }
        init_settings_groups();
    }
}

static void release_page_for_close(lv_obj_t **page) {
    lv_obj_t *active = lv_disp_get_scr_act(NULL);

    if (page == NULL || *page == NULL) {
        return;
    }
    if (*page != active) {
        lv_obj_del(*page);
    }
    *page = NULL;
}

static void release_settings_pages_for_close(void) {
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    settings_perf_monitor_uninstall();
#endif
    settings_focus_background_cache_release();
    release_page_for_close(&ui_Page_Set);
    s_set_scroll_anim_panel = NULL;
    s_set_scroll_center_y = 0;
    s_set_defocused_fill_is_redundant = false;
    release_page_for_close(&ui_Page_Slider);
    release_page_for_close(&ui_Page_About);
    reset_about_system_cards();
    release_page_for_close(&ui_Page_Swipe);
    release_page_for_close(&ui_Page_Sleep);
    release_page_for_close(&ui_Page_Connect);
    release_page_for_close(&ui_Page_Network);
    ui_wifi_disconnect = NULL;
    ui_wifi_disconnect_label = NULL;
    s_network_disconnect_focusable = false;
}

static void load_page(lv_obj_t **page, void (*init_fn)(void), settings_page_t page_kind, GroupInfo *group_info,
                      pm_operation_t operation, uint8_t focus_index, bool editing, bool wrap,
                      bool auto_delete_previous_screen) {
    lv_obj_t *new_page = NULL;

    if (page == NULL || init_fn == NULL) {
        return;
    }
    lv_pm_open_page_internal(s_group, group_info, operation, page, LV_SCR_LOAD_ANIM_NONE, 0, 0, init_fn,
                             auto_delete_previous_screen);
    new_page = *page;
    if (new_page == NULL) {
        return;
    }

    s_page = page_kind;
    rebind_group_info(group_info, focus_index, editing, wrap);
}

static void open_set_page_with_previous_screen_delete(bool auto_delete_previous_screen) {
    ensure_page_initialized(&ui_Page_Set, ui_Page_Set_screen_init);
    configure_set_page();
    settings_enable_set_scroll_anim();
    if (s_last_set_focus_index >= SETTINGS_SET_FOCUS_COUNT) {
        s_last_set_focus_index = SETTINGS_SET_FOCUS_BACK;
    }
    load_page(&ui_Page_Set, ui_Page_Set_screen_init, SETTINGS_PAGE_SET, &group_page_set, PM_ADD_OBJS_TO_GROUP,
              s_last_set_focus_index, false, true, auto_delete_previous_screen);
    focus_set_row(s_last_set_focus_index, LV_ANIM_OFF);
}

void open_set_page(void) {
    open_set_page_with_previous_screen_delete(false);
}

static void open_network_page_with_previous_screen_delete(bool auto_delete_previous_screen) {
    ensure_page_initialized(&ui_Page_Network, ui_Page_Network_screen_init);
    update_network_page();
    load_page(&ui_Page_Network, ui_Page_Network_screen_init, SETTINGS_PAGE_NETWORK, &group_page_network,
              PM_ADD_OBJS_TO_GROUP, 0, false, true, auto_delete_previous_screen);
    if (s_state.wifi_configured && ui_wifi_disconnect != NULL) {
        lv_group_focus_obj(ui_wifi_disconnect);
    } else if (ui_wificancel != NULL) {
        lv_group_focus_obj(ui_wificancel);
    }
}

static void update_slider_value_label(int value) {
    char text[8];

    snprintf(text, sizeof(text), "%d", value);
    if (ui_bvbv != NULL) {
        lv_label_set_text(ui_bvbv, text);
    }
}

static void update_about_scrolling_label(lv_obj_t *label, const char *text) {
    if (label == NULL || text == NULL) {
        return;
    }

    if (lv_label_get_long_mode(label) != LV_LABEL_LONG_SCROLL_CIRCULAR) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    const char *current_text = lv_label_get_text(label);
    if (current_text == NULL || strcmp(current_text, text) != 0) {
        lv_label_set_text(label, text);
    }
}

void update_about_labels(void) {
    if (ui_devnamet2 != NULL) {
        lv_label_set_text(ui_devnamet2, safe_text(s_state.about.device_name, "WatcheRobot"));
    }
    if (ui_espversiont2 != NULL) {
        lv_label_set_text(ui_espversiont2, safe_text(s_state.about.firmware_version, "-"));
    }
    if (ui_aiversion1 != NULL) {
        lv_label_set_text(ui_aiversion1, "IDF Version :");
    }
    if (ui_aiversion2 != NULL) {
        lv_label_set_text(ui_aiversion2, safe_text(s_state.about.idf_version, "-"));
    }
    if (ui_snt1 != NULL) {
        lv_label_set_text(ui_snt1, "ESP32 Branch :");
    }
    update_about_scrolling_label(ui_snt2, safe_text(s_state.about.esp32_git_ref, "-"));
    if (ui_euit1 != NULL) {
        lv_label_set_text(ui_euit1, "STM32 Branch :");
    }
    update_about_scrolling_label(ui_euit2, safe_text(s_state.about.stm32_git_ref, "-"));
    if (ui_blet2 != NULL) {
        lv_label_set_text(ui_blet2, safe_text(s_state.about.ble_mac, "-"));
    }
    if (ui_wifit2 != NULL) {
        lv_label_set_text(ui_wifit2, safe_text(s_state.about.wifi_mac, "-"));
    }
    update_about_system_labels();
}

void Page_ConnAPP_Mate(void) {
    if (ui_conn_arcr != NULL) {
        lv_arc_set_value(ui_conn_arcr, 0);
    }
    if (ui_conn_panel1 != NULL) {
        lv_obj_clear_flag(ui_conn_panel1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_conn_panel2 != NULL) {
        lv_obj_add_flag(ui_conn_panel2, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_arrow1 != NULL) {
        lv_obj_clear_flag(ui_arrow1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_arrow2 != NULL) {
        lv_obj_add_flag(ui_arrow2, LV_OBJ_FLAG_HIDDEN);
    }
}

void Page_ConnAPP_BLE(void) {
    if (ui_conn_arcr != NULL) {
        lv_arc_set_value(ui_conn_arcr, 100);
    }
    if (ui_conn_panel1 != NULL) {
        lv_obj_add_flag(ui_conn_panel1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_conn_panel2 != NULL) {
        lv_obj_clear_flag(ui_conn_panel2, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_arrow1 != NULL) {
        lv_obj_add_flag(ui_arrow1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_arrow2 != NULL) {
        lv_obj_clear_flag(ui_arrow2, LV_OBJ_FLAG_HIDDEN);
    }
}

void note_original_page_opened(settings_page_t page_kind, GroupInfo *group_info, uint8_t focus_index, bool editing,
                               bool wrap) {
    s_page = page_kind;
    rebind_group_info(group_info, focus_index, editing, wrap);
}

void preserve_slider_editing_on_touch(lv_event_t *event) {
    lv_event_code_t event_code;
    lv_indev_t *indev;

    /* Pointer focus leaves the LVGL group edit mode. Restore it only when the slider was already being edited. */
    if (event == NULL) {
        return;
    }

    event_code = lv_event_get_code(event);
    if (event_code == LV_EVENT_RELEASED || event_code == LV_EVENT_PRESS_LOST) {
        s_restore_slider_editing_after_touch_focus = false;
        return;
    }

    indev = lv_indev_get_act();
    if (indev == NULL || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
        return;
    }

    if (event_code == LV_EVENT_PRESSED) {
        s_restore_slider_editing_after_touch_focus = s_group != NULL && lv_group_get_editing(s_group);
        return;
    }

    if (event_code == LV_EVENT_FOCUSED && s_restore_slider_editing_after_touch_focus && s_group != NULL) {
        lv_group_set_editing(s_group, true);
    }
}

bool factory_settings_ui_handle_network_cancel(void) {
    if (!s_network_standalone) {
        return false;
    }
    if (s_callbacks.on_back != NULL) {
        s_callbacks.on_back(s_callbacks.user_ctx);
    }
    return true;
}

static void network_disconnect_event_cb(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);
    bool applied = false;

    if (event_code != LV_EVENT_CLICKED) {
        return;
    }
    if (s_callbacks.on_wifi_disconnect != NULL) {
        applied = s_callbacks.on_wifi_disconnect(s_callbacks.user_ctx);
    }
    if (!applied && ui_wifibtnt != NULL) {
        lv_label_set_text(ui_wifibtnt, "Disconnect failed");
    }
}

static void ensure_network_disconnect_button(void) {
    if (ui_Page_Network == NULL || ui_wifi_disconnect != NULL) {
        return;
    }

    ui_wifi_disconnect = lv_btn_create(ui_Page_Network);
    lv_obj_set_width(ui_wifi_disconnect, NETWORK_DISCONNECT_BUTTON_WIDTH);
    lv_obj_set_height(ui_wifi_disconnect, NETWORK_DISCONNECT_BUTTON_HEIGHT);
    lv_obj_set_x(ui_wifi_disconnect, 0);
    lv_obj_set_y(ui_wifi_disconnect, NETWORK_DISCONNECT_BUTTON_Y);
    lv_obj_set_align(ui_wifi_disconnect, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_wifi_disconnect, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_wifi_disconnect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_wifi_disconnect, 23, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_wifi_disconnect, lv_color_hex(0x30343A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_wifi_disconnect, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_wifi_disconnect, lv_color_hex(SETTINGS_GREEN), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_wifi_disconnect, 255, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_wifi_disconnect, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ui_wifi_disconnect, network_disconnect_event_cb, LV_EVENT_ALL, NULL);

    ui_wifi_disconnect_label = lv_label_create(ui_wifi_disconnect);
    lv_label_set_text(ui_wifi_disconnect_label, "Disconnect");
    lv_obj_center(ui_wifi_disconnect_label);
    lv_obj_set_style_text_color(ui_wifi_disconnect_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_wifi_disconnect_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void configure_network_group(bool disconnect_focusable) {
    if (disconnect_focusable) {
        lv_obj_t *network_objects[] = {ui_wifi_disconnect, ui_wificancel};
        addObjToGroup(&group_page_network, network_objects,
                      (uint8_t)(sizeof(network_objects) / sizeof(network_objects[0])));
    } else {
        lv_obj_t *network_objects[] = {ui_wificancel};
        addObjToGroup(&group_page_network, network_objects,
                      (uint8_t)(sizeof(network_objects) / sizeof(network_objects[0])));
    }
    s_network_disconnect_focusable = disconnect_focusable;
}

static void update_network_page(void) {
    const bool configured = s_state.wifi_configured;
    const bool connected = configured && s_state.wifi_connected;
    bool disconnect_focusable;
    const char *ssid = safe_text(s_state.wifi_ssid, "-");
    const char *ip = safe_text(s_state.wifi_ip, "-");
    const char *ble_mac = safe_text(s_state.ble_mac, "-");
    const bool ble_connected = s_state.ble_connected;
    const bool ble_advertising = s_state.ble_advertising;
    const char *network_wait_title = safe_text(s_state.network_wait_title, "Set up in App over Bluetooth");
    const char *network_wait_hint =
        safe_text(s_state.network_wait_hint, "Open the app and select this BLE MAC to configure Wi-Fi.");
    const char *network_connected_title = safe_text(s_state.network_connected_title, "App connected");
    const char *network_connected_hint =
        safe_text(s_state.network_connected_hint, "Send Wi-Fi credentials from the app.");
    const uint32_t ble_visual_color = s_state.network_ble_mac_color != 0U ? s_state.network_ble_mac_color : 0x5AA2FF;
    const uint32_t ble_mac_color = s_state.network_ble_mac_color != 0U
                                       ? s_state.network_ble_mac_color
                                       : (uint32_t)(ble_connected || ble_advertising ? SETTINGS_GREEN : 0xE1B94B);
    bool focus_changed = false;

    ensure_network_disconnect_button();
    disconnect_focusable = configured && ui_wifi_disconnect != NULL;

    if (ui_wifip1 != NULL) {
        lv_obj_clear_flag(ui_wifip1, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_wifip3 != NULL) {
        lv_obj_add_flag(ui_wifip3, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_wifiicon != NULL) {
        if (s_state.network_use_ble_icon) {
            lv_img_set_src(ui_wifiicon, &ui_img_ble_png);
            lv_obj_set_style_img_recolor(ui_wifiicon, lv_color_hex(ble_visual_color), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(ui_wifiicon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_img_set_src(ui_wifiicon, &ui_img_wifi_3_png);
            lv_obj_set_style_img_recolor_opa(ui_wifiicon, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    if (ui_wifissid != NULL) {
        lv_obj_set_width(ui_wifissid, 340);
        lv_label_set_long_mode(ui_wifissid, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(ui_wifissid, &watcher_font_wifi_cjk16, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_wifimqtt != NULL) {
        lv_obj_set_width(ui_wifimqtt, 340);
        lv_label_set_long_mode(ui_wifimqtt, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(ui_wifimqtt, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_wifibtnt != NULL) {
        lv_obj_set_width(ui_wifibtnt, 350);
        lv_label_set_long_mode(ui_wifibtnt, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(ui_wifibtnt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui_wifibtnt, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_y(ui_wifibtnt, configured ? 34 : 80);
    }

    if (configured) {
        if (ui_wifissid != NULL) {
            lv_label_set_text(ui_wifissid, ssid);
        }
        if (ui_wifimqtt != NULL) {
            if (connected) {
                char ip_text[96];
                lv_snprintf(ip_text, sizeof(ip_text), "Connected  IP: %s", ip);
                lv_label_set_text(ui_wifimqtt, ip_text);
                lv_obj_set_style_text_color(ui_wifimqtt, lv_color_hex(SETTINGS_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
            } else if (ble_connected) {
                lv_label_set_text(ui_wifimqtt, "Wi-Fi saved");
                lv_obj_set_style_text_color(ui_wifimqtt, lv_color_hex(0xE1B94B), LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                const bool connecting = strcmp(safe_text(s_state.wifi_status, ""), "Connecting") == 0;
                lv_label_set_text(ui_wifimqtt, connecting ? "Connecting" : "Offline");
                lv_obj_set_style_text_color(ui_wifimqtt, lv_color_hex(connecting ? 0xE1B94B : 0xD54941),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
        if (ui_wifibtnt != NULL) {
            lv_label_set_text(ui_wifibtnt, ble_connected
                                               ? "App connected. Wi-Fi will connect after Bluetooth disconnects."
                                               : "Disconnect clears saved Wi-Fi.");
        }
        if (ui_wifi_disconnect != NULL) {
            lv_obj_clear_flag(ui_wifi_disconnect, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (ui_wifissid != NULL) {
            lv_label_set_text(ui_wifissid, ble_connected ? network_connected_title : network_wait_title);
        }
        if (ui_wifimqtt != NULL) {
            char mac_text[96];
            lv_snprintf(mac_text, sizeof(mac_text), "BLE MAC: %s", ble_mac);
            lv_label_set_text(ui_wifimqtt, mac_text);
            lv_obj_set_style_text_color(ui_wifimqtt, lv_color_hex(ble_mac_color), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (ui_wifibtnt != NULL) {
            lv_label_set_text(ui_wifibtnt, ble_connected ? network_connected_hint : network_wait_hint);
        }
        if (ui_wifi_disconnect != NULL) {
            lv_obj_add_flag(ui_wifi_disconnect, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (disconnect_focusable != s_network_disconnect_focusable || group_page_network.obj_count == 0) {
        lv_obj_t *focused = s_group != NULL ? lv_group_get_focused(s_group) : NULL;
        const uint8_t next_focus =
            disconnect_focusable && focused != ui_wificancel ? 0 : (disconnect_focusable ? 1 : 0);

        configure_network_group(disconnect_focusable);
        if (s_page == SETTINGS_PAGE_NETWORK) {
            rebind_group_info(&group_page_network, next_focus, false, true);
            focus_changed = true;
        }
    }
    if (s_page == SETTINGS_PAGE_NETWORK && !focus_changed && s_group != NULL && lv_group_get_focused(s_group) == NULL) {
        rebind_group_info(&group_page_network, 0, false, true);
    }
}

void open_network_page(void) {
    open_network_page_with_previous_screen_delete(false);
}

int get_ble_switch(int caller) {
    (void)caller;
    return ui_setblesw != NULL && lv_obj_has_state(ui_setblesw, LV_STATE_CHECKED) ? 1 : 0;
}

int set_ble_switch(int caller, int value) {
    const bool enabled = value != 0;
    const bool previous_checked = ui_setblesw != NULL && lv_obj_has_state(ui_setblesw, LV_STATE_CHECKED);
    bool applied = true;

    (void)caller;
    if (s_callbacks.on_bluetooth_changed != NULL) {
        applied = s_callbacks.on_bluetooth_changed(enabled, s_callbacks.user_ctx);
    }
    if (!applied) {
        if (ui_setblesw != NULL) {
            if (previous_checked) {
                lv_obj_add_state(ui_setblesw, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(ui_setblesw, LV_STATE_CHECKED);
            }
            lv_obj_clear_state(ui_setblesw, LV_STATE_DISABLED);
        }
        return -1;
    }
    if (ui_setblesw != NULL) {
        if (enabled) {
            lv_obj_add_state(ui_setblesw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_setblesw, LV_STATE_CHECKED);
        }
        schedule_ble_switch_unlock();
    }
    return 0;
}

int get_rgb_switch(int caller) {
    (void)caller;
    return s_state.rgb_enabled ? 1 : 0;
}

int set_rgb_switch(int caller, int value) {
    const bool enabled = value != 0;
    const bool previous_enabled = s_state.rgb_enabled;
    bool applied = true;

    (void)caller;
    if (s_callbacks.on_rgb_changed != NULL) {
        applied = s_callbacks.on_rgb_changed(enabled, s_callbacks.user_ctx);
    }
    if (!applied) {
        s_state.rgb_enabled = previous_enabled;
        update_set_switches();
        if (ui_setrgbsw != NULL) {
            lv_obj_clear_state(ui_setrgbsw, LV_STATE_DISABLED);
        }
        return -1;
    }
    s_state.rgb_enabled = enabled;
    update_set_switches();
    return 0;
}

int set_sound(int caller, int value) {
    (void)caller;
    s_state.sound_percent = clamp_percent(value, 0);
    if (s_callbacks.on_sound_changed != NULL) {
        s_callbacks.on_sound_changed(s_state.sound_percent, s_callbacks.user_ctx);
    }
    return 0;
}

int set_brightness(int caller, int value) {
    (void)caller;
    s_state.brightness_percent = clamp_percent(value, 1);
    if (s_callbacks.on_brightness_changed != NULL) {
        s_callbacks.on_brightness_changed(s_state.brightness_percent, s_callbacks.user_ctx);
    }
    return 0;
}

int set_screenoff_time(int caller, int value) {
    (void)caller;
    g_screenoff_time = value;
    return 0;
}

int get_screenoff_time(int caller) {
    (void)caller;
    return g_screenoff_time;
}

int set_screenoff_switch(int caller, int value) {
    (void)caller;
    g_screenoff_switch = value != 0 ? 1 : 0;
    return 0;
}

int get_screenoff_switch(int caller) {
    (void)caller;
    return g_screenoff_switch;
}

void Page_shutdown(void) {
    if (g_shutdown == 1) {
        lv_label_set_text(ui_sptitle, "Shut down");
        lv_label_set_text(ui_sptext, "Swipe to shut down");
    } else if (g_shutdown == 0) {
        lv_label_set_text(ui_sptitle, "Reboot");
        lv_label_set_text(ui_sptext, "Swipe to reboot");
    }
}

void Page_facreset(void) {
    lv_label_set_text(ui_sptitle, "Factory reset");
    lv_label_set_text(ui_sptext, "Swipe to reset");
}

static unsigned long count_lvgl_tree_nodes(lv_obj_t *root) {
    if (root == NULL) {
        return 0UL;
    }

    unsigned long total = 1UL;
    uint32_t child_count = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        total += count_lvgl_tree_nodes(lv_obj_get_child(root, (int32_t)i));
    }
    return total;
}

static bool factory_settings_ui_prepare_session(const factory_settings_state_t *state,
                                                const factory_settings_callbacks_t *callbacks) {
    if (state == NULL) {
        return false;
    }
    s_state = *state;
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
    volbri.vs_value = clamp_percent(s_state.sound_percent, 0);
    volbri.bs_value = clamp_percent(s_state.brightness_percent, 1);
    g_screenoff_time = 0;
    g_screenoff_switch = 1;
    g_swipeid = 0;
    g_shutdown = 0;
    s_last_set_focus_index = SETTINGS_SET_FOCUS_BACK;
    return true;
}

void factory_settings_ui_build_with_previous_screen_delete(lv_obj_t *screen, const factory_settings_state_t *state,
                                                           const factory_settings_callbacks_t *callbacks,
                                                           bool auto_delete_previous_screen) {
    (void)screen;

    if (!factory_settings_ui_prepare_session(state, callbacks)) {
        return;
    }
    s_network_standalone = false;
    open_set_page_with_previous_screen_delete(auto_delete_previous_screen);
}

void factory_settings_ui_build_wifi_detail_with_previous_screen_delete(lv_obj_t *screen,
                                                                       const factory_settings_state_t *state,
                                                                       const factory_settings_callbacks_t *callbacks,
                                                                       bool auto_delete_previous_screen) {
    (void)screen;

    if (!factory_settings_ui_prepare_session(state, callbacks)) {
        return;
    }
    remember_set_focus(SETTINGS_SET_FOCUS_WIFI);
    if (s_callbacks.on_wifi != NULL) {
        s_callbacks.on_wifi(s_callbacks.user_ctx);
    }
    open_network_page_with_previous_screen_delete(auto_delete_previous_screen);
}

void factory_settings_ui_build(lv_obj_t *screen, const factory_settings_state_t *state,
                               const factory_settings_callbacks_t *callbacks) {
    factory_settings_ui_build_with_previous_screen_delete(screen, state, callbacks, true);
}

void factory_settings_ui_build_network_page(lv_obj_t *screen, const factory_settings_state_t *state,
                                            const factory_settings_callbacks_t *callbacks, lv_group_t **group,
                                            bool auto_delete_previous_screen) {
    (void)screen;

    if (state == NULL) {
        return;
    }
    s_network_standalone = true;
    if (!factory_settings_ui_prepare_session(state, callbacks)) {
        return;
    }
    if (group != NULL) {
        if (*group == NULL) {
            *group = lv_group_create();
        }
        s_group = *group;
    }
    ensure_page_initialized(&ui_Page_Network, ui_Page_Network_screen_init);
    update_network_page();
    lv_pm_open_page_internal(s_group, &group_page_network, PM_ADD_OBJS_TO_GROUP, &ui_Page_Network,
                             LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Network_screen_init, auto_delete_previous_screen);
    note_original_page_opened(SETTINGS_PAGE_NETWORK, &group_page_network, 0, false, true);
    if (ui_wificancel != NULL) {
        lv_group_focus_obj(ui_wificancel);
    }
}

void factory_settings_ui_bind_group(lv_group_t **group) {
    if (group == NULL) {
        return;
    }
    if (*group == NULL) {
        *group = lv_group_create();
    }
    if (*group == NULL) {
        return;
    }

    s_group = *group;
    if (s_page == SETTINGS_PAGE_SET) {
        rebind_group_info(&group_page_set, s_last_set_focus_index, false, true);
        focus_set_row(s_last_set_focus_index, LV_ANIM_OFF);
    } else if (s_page == SETTINGS_PAGE_SLIDER) {
        rebind_group_info(s_slider_kind == FACTORY_SETTINGS_SLIDER_SOUND ? &group_page_volume : &group_page_brightness,
                          0, true, true);
    } else if (s_page == SETTINGS_PAGE_ABOUT) {
        rebind_group_info(&group_page_about, 0, false, false);
    } else if (s_page == SETTINGS_PAGE_SWIPE) {
        rebind_group_info(&group_page_swipe, 0, false, true);
    } else if (s_page == SETTINGS_PAGE_CONNECT) {
        rebind_group_info(&group_page_connectapp, 0, false, true);
    } else if (s_page == SETTINGS_PAGE_NETWORK) {
        rebind_group_info(&group_page_network, 0, false, true);
    }
}

void factory_settings_ui_log_debug_snapshot(const char *stage) {
    lv_obj_t *active = lv_disp_get_scr_act(NULL);
    lv_mem_monitor_t mem = {0};

    lv_mem_monitor(&mem);
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_core stage=%s page=%d active=%p active_children=%lu active_tree=%lu group=%p",
             stage != NULL ? stage : "unknown", (int)s_page, active,
             active != NULL ? (unsigned long)lv_obj_get_child_cnt(active) : 0UL, count_lvgl_tree_nodes(active),
             s_group);
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_pages stage=%s set=%p/%lu slider=%p/%lu about=%p/%lu swipe=%p/%lu",
             stage != NULL ? stage : "unknown", ui_Page_Set, count_lvgl_tree_nodes(ui_Page_Set), ui_Page_Slider,
             count_lvgl_tree_nodes(ui_Page_Slider), ui_Page_About, count_lvgl_tree_nodes(ui_Page_About), ui_Page_Swipe,
             count_lvgl_tree_nodes(ui_Page_Swipe));
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_more_pages stage=%s sleep=%p/%lu connect=%p/%lu network=%p/%lu",
             stage != NULL ? stage : "unknown", ui_Page_Sleep, count_lvgl_tree_nodes(ui_Page_Sleep), ui_Page_Connect,
             count_lvgl_tree_nodes(ui_Page_Connect), ui_Page_Network, count_lvgl_tree_nodes(ui_Page_Network));
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_groups stage=%s set=%u vol=%u bri=%u about=%u swipe=%u sleep=%u conn=%u net=%u",
             stage != NULL ? stage : "unknown", (unsigned)group_page_set.obj_count,
             (unsigned)group_page_volume.obj_count, (unsigned)group_page_brightness.obj_count,
             (unsigned)group_page_about.obj_count, (unsigned)group_page_swipe.obj_count,
             (unsigned)group_page_sleep.obj_count, (unsigned)group_page_connectapp.obj_count,
             (unsigned)group_page_network.obj_count);
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_lv_mem stage=%s total=%lu free=%lu used_pct=%u frag_pct=%u free_biggest=%lu",
             stage != NULL ? stage : "unknown", (unsigned long)mem.total_size, (unsigned long)mem.free_size,
             (unsigned)mem.used_pct, (unsigned)mem.frag_pct, (unsigned long)mem.free_biggest_size);
#if CONFIG_WATCHER_DEBUG_CLI_ENABLE
    factory_settings_perf_snapshot_t perf = {0};
    factory_settings_perf_stats_snapshot(&s_set_perf_stats, &perf);
    ESP_LOGW(SETTINGS_UI_DIAG_TAG,
             "evt=settings_ui_diag_perf stage=%s frames=%lu pixels=%lu p95_ms=%lu max_ms=%lu over_30ms=%lu "
             "max_consecutive_over_30ms=%lu",
             stage != NULL ? stage : "unknown", (unsigned long)perf.frame_count, (unsigned long)perf.pixel_count,
             (unsigned long)perf.p95_ms, (unsigned long)perf.max_ms, (unsigned long)perf.over_budget_count,
             (unsigned long)perf.max_consecutive_over_budget);
#endif
}

void factory_settings_ui_reset(void) {
    cancel_ble_switch_unlock();
    release_settings_pages_for_close();
    s_group = NULL;
    s_page = SETTINGS_PAGE_SET;
    s_network_standalone = false;
    s_slider_kind = FACTORY_SETTINGS_SLIDER_SOUND;
    s_confirm_item = FACTORY_SETTINGS_ITEM_REBOOT;
    s_last_set_focus_index = SETTINGS_SET_FOCUS_BACK;
    s_set_scroll_anim_panel = NULL;
    s_set_scroll_center_y = 0;
    s_set_defocused_fill_is_redundant = false;
    s_restore_slider_editing_after_touch_focus = false;
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    memset(&group_page_set, 0, sizeof(group_page_set));
    memset(&group_page_volume, 0, sizeof(group_page_volume));
    memset(&group_page_brightness, 0, sizeof(group_page_brightness));
    memset(&group_page_about, 0, sizeof(group_page_about));
    memset(&group_page_swipe, 0, sizeof(group_page_swipe));
    memset(&group_page_sleep, 0, sizeof(group_page_sleep));
    memset(&group_page_connectapp, 0, sizeof(group_page_connectapp));
    memset(&group_page_network, 0, sizeof(group_page_network));
    memset(&g_page_record, 0, sizeof(g_page_record));
}

void factory_settings_ui_click_focused(void) {
    lv_obj_t *focused = s_group != NULL ? lv_group_get_focused(s_group) : NULL;

    if (focused == NULL) {
        return;
    }

    if (s_page == SETTINGS_PAGE_SLIDER) {
        if (focused == ui_vslider || focused == ui_bslider) {
            lv_event_send(focused, LV_EVENT_RELEASED, NULL);
            open_set_page();
            return;
        }
        lv_event_send(focused, LV_EVENT_CLICKED, NULL);
        return;
    }

    if (s_page == SETTINGS_PAGE_ABOUT) {
        if (focused == ui_Paboutb) {
            lv_event_send(focused, LV_EVENT_CLICKED, NULL);
        } else if (s_group != NULL) {
            lv_group_focus_next(s_group);
        }
        return;
    }

    if (s_page == SETTINGS_PAGE_SWIPE) {
        lv_event_send(focused, LV_EVENT_CLICKED, NULL);
        return;
    }

    if (s_page == SETTINGS_PAGE_CONNECT || s_page == SETTINGS_PAGE_NETWORK) {
        lv_event_send(focused, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_event_send(focused, LV_EVENT_CLICKED, NULL);
}

bool factory_settings_ui_open_wifi_detail(void) {
    if (s_page != SETTINGS_PAGE_SET) {
        open_set_page();
    }
    if (ui_setwifi == NULL) {
        return false;
    }

    focus_set_row(SETTINGS_SET_FOCUS_WIFI, LV_ANIM_OFF);
    lv_event_send(ui_setwifi, LV_EVENT_CLICKED, NULL);
    return true;
}

bool factory_settings_ui_debug_open_page(const char *page_id) {
    settings_set_focus_t focus = SETTINGS_SET_FOCUS_COUNT;
    lv_obj_t *obj = NULL;

    if (page_id == NULL || page_id[0] == '\0') {
        return false;
    }

    if (strcmp(page_id, "set") == 0 || strcmp(page_id, "settings") == 0) {
        open_set_page();
        return true;
    }
    if (strcmp(page_id, "wifi") == 0 || strcmp(page_id, "network") == 0) {
        return factory_settings_ui_open_wifi_detail();
    }
    if (strcmp(page_id, "connect") == 0 || strcmp(page_id, "app") == 0 || strcmp(page_id, "app_center") == 0 ||
        strcmp(page_id, "sleep") == 0 || strcmp(page_id, "screen_off") == 0) {
        return false;
    }
    if (strcmp(page_id, "sound") == 0 || strcmp(page_id, "volume") == 0) {
        focus = SETTINGS_SET_FOCUS_SOUND;
    } else if (strcmp(page_id, "brightness") == 0 || strcmp(page_id, "display") == 0) {
        focus = SETTINGS_SET_FOCUS_BRIGHTNESS;
    } else if (strcmp(page_id, "about") == 0) {
        focus = SETTINGS_SET_FOCUS_ABOUT;
    } else if (strcmp(page_id, "reboot") == 0) {
        focus = SETTINGS_SET_FOCUS_REBOOT;
    } else if (strcmp(page_id, "factory_reset") == 0 || strcmp(page_id, "factory") == 0) {
        focus = SETTINGS_SET_FOCUS_FACTORY_RESET;
    } else {
        return false;
    }

    if (s_page != SETTINGS_PAGE_SET) {
        open_set_page();
    }
    focus_set_row((uint8_t)focus, LV_ANIM_OFF);
    obj = set_focus_obj((uint8_t)focus);
    if (obj == NULL) {
        return false;
    }
    lv_event_send(obj, LV_EVENT_CLICKED, NULL);
    return true;
}

static bool debug_target_to_set_focus(const char *target_id, settings_set_focus_t *out_focus) {
    settings_set_focus_t focus;

    if (target_id == NULL || target_id[0] == '\0' || out_focus == NULL) {
        return false;
    }

    if (strcmp(target_id, "back") == 0) {
        focus = SETTINGS_SET_FOCUS_BACK;
    } else if (strcmp(target_id, "reboot") == 0) {
        focus = SETTINGS_SET_FOCUS_REBOOT;
    } else if (strcmp(target_id, "wifi") == 0 || strcmp(target_id, "wi-fi") == 0) {
        focus = SETTINGS_SET_FOCUS_WIFI;
    } else if (strcmp(target_id, "sound") == 0 || strcmp(target_id, "volume") == 0) {
        focus = SETTINGS_SET_FOCUS_SOUND;
    } else if (strcmp(target_id, "brightness") == 0 || strcmp(target_id, "display") == 0) {
        focus = SETTINGS_SET_FOCUS_BRIGHTNESS;
    } else if (strcmp(target_id, "rgb") == 0 || strcmp(target_id, "rgb_light") == 0) {
        focus = SETTINGS_SET_FOCUS_RGB;
    } else if (strcmp(target_id, "about") == 0) {
        focus = SETTINGS_SET_FOCUS_ABOUT;
    } else if (strcmp(target_id, "factory_reset") == 0 || strcmp(target_id, "factory") == 0) {
        focus = SETTINGS_SET_FOCUS_FACTORY_RESET;
    } else {
        return false;
    }

    *out_focus = focus;
    return true;
}

bool factory_settings_ui_debug_focus(const char *target_id) {
    settings_set_focus_t focus;
    lv_obj_t *obj = NULL;

    if (!debug_target_to_set_focus(target_id, &focus)) {
        return false;
    }
    if (s_page != SETTINGS_PAGE_SET) {
        open_set_page();
    }
    obj = set_focus_obj((uint8_t)focus);
    if (obj == NULL || s_group == NULL) {
        return false;
    }
    lv_group_focus_obj(obj);
    focus_set_row((uint8_t)focus, LV_ANIM_OFF);
    return true;
}

bool factory_settings_ui_debug_rotate(int steps) {
    if (s_group == NULL) {
        return false;
    }
    while (steps > 0) {
        lv_group_focus_next(s_group);
        --steps;
    }
    while (steps < 0) {
        lv_group_focus_prev(s_group);
        ++steps;
    }
    return true;
}

bool factory_settings_ui_debug_click(void) {
    if (s_group == NULL || lv_group_get_focused(s_group) == NULL) {
        return false;
    }
    factory_settings_ui_click_focused();
    return true;
}

const char *factory_settings_ui_debug_page_name(void) {
    switch (s_page) {
    case SETTINGS_PAGE_SET:
        return "set";
    case SETTINGS_PAGE_SLIDER:
        return s_slider_kind == FACTORY_SETTINGS_SLIDER_SOUND ? "sound" : "brightness";
    case SETTINGS_PAGE_ABOUT:
        return "about";
    case SETTINGS_PAGE_SWIPE:
        return s_confirm_item == FACTORY_SETTINGS_ITEM_FACTORY_RESET ? "factory_reset" : "reboot";
    case SETTINGS_PAGE_CONNECT:
        return "connect";
    case SETTINGS_PAGE_NETWORK:
        return "network";
    default:
        return "unknown";
    }
}

const char *factory_settings_ui_debug_focus_name(void) {
    lv_obj_t *focused = s_group != NULL ? lv_group_get_focused(s_group) : NULL;
    int focus_index;

    if (focused == NULL) {
        return "none";
    }
    if (s_page == SETTINGS_PAGE_SET) {
        focus_index = find_set_focus_index(focused);
        if (focus_index >= 0) {
            return set_focus_name((settings_set_focus_t)focus_index);
        }
    }
    if (focused == ui_vslider) {
        return "sound_slider";
    }
    if (focused == ui_bslider) {
        return "brightness_slider";
    }
    if (focused == ui_bvback) {
        return "slider_back";
    }
    if (focused == ui_Paboutb) {
        return "about_back";
    }
    if (focused == ui_spback) {
        return "swipe_back";
    }
    if (focused == ui_wificancel) {
        return "network_back";
    }
    if (focused == ui_wifi_disconnect) {
        return "network_disconnect";
    }
    if (focused == ui_connp1) {
        return "connect_mate";
    }
    if (focused == ui_connp2) {
        return "connect_ble";
    }
    if (focused == ui_conncancel) {
        return "connect_back";
    }
    return "unknown";
}

void factory_settings_ui_update_state(const factory_settings_state_t *state) {
    if (state == NULL) {
        return;
    }
    s_state = *state;

    if (s_page == SETTINGS_PAGE_SET) {
        update_set_switches();
    } else if (s_page == SETTINGS_PAGE_SLIDER) {
        int value = s_slider_kind == FACTORY_SETTINGS_SLIDER_SOUND ? clamp_percent(s_state.sound_percent, 0)
                                                                   : clamp_percent(s_state.brightness_percent, 1);
        update_slider_value_label(value);
    } else if (s_page == SETTINGS_PAGE_ABOUT) {
        update_about_labels();
    } else if (s_page == SETTINGS_PAGE_NETWORK) {
        update_network_page();
    }
}
