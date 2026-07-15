#include "factory_settings_ui_internal.h"

#include <inttypes.h>
#include <string.h>

// Factory Settings event callbacks copied in factory style, backed by the thin WatcheRobot adapter.

static void apply_slider_selection_style(factory_settings_slider_t kind) {
    lv_obj_t *panel = kind == FACTORY_SETTINGS_SLIDER_SOUND ? ui_vp : ui_bp;
    lv_obj_t *slider = kind == FACTORY_SETTINGS_SLIDER_SOUND ? ui_vslider : ui_bslider;
    const uint32_t selection_color =
        kind == FACTORY_SETTINGS_SLIDER_SOUND ? SETTINGS_SOUND_SELECTION_COLOR : SETTINGS_BRIGHTNESS_SELECTION_COLOR;

    if (panel != NULL) {
        lv_obj_set_style_border_color(panel, lv_color_hex(selection_color), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (slider != NULL) {
        lv_obj_set_style_outline_color(slider, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_EDITED);
    }
    if (ui_bvback != NULL) {
        lv_obj_set_style_border_color(ui_bvback, lv_color_hex(selection_color), LV_PART_MAIN | LV_STATE_FOCUSED);
    }
}

void sgesup_cb(lv_event_t *event) {
    (void)event;
    if (s_group != NULL) {
        lv_group_focus_next(s_group);
    }
}

void sgesdown_cb(lv_event_t *event) {
    (void)event;
    if (s_group != NULL) {
        lv_group_focus_prev(s_group);
    }
}

void sclick_cb(lv_event_t *event) {
    (void)event;
    factory_settings_ui_click_focused();
}

void setsl_cb(lv_event_t *event) {
    (void)event;
}

void backmenu_cb(lv_event_t *event) {
    (void)event;
    if (s_callbacks.on_back != NULL) {
        s_callbacks.on_back(s_callbacks.user_ctx);
    }
}

void backset_cb(lv_event_t *event) {
    (void)event;
    if (s_group != NULL) {
        lv_group_set_wrap(s_group, true);
    }
    open_set_page();
}

void setappc_cb(lv_event_t *event) {
    (void)event;
    ensure_page_initialized(&ui_Page_Connect, ui_Page_Connect_screen_init);
    Page_ConnAPP_Mate();
    lv_pm_open_page(s_group, &group_page_connectapp, PM_ADD_OBJS_TO_GROUP, &ui_Page_Connect, LV_SCR_LOAD_ANIM_NONE, 0,
                    0, &ui_Page_Connect_screen_init);
    note_original_page_opened(SETTINGS_PAGE_CONNECT, &group_page_connectapp, 0, false, true);
    lv_group_focus_obj(ui_connp1);
}

void setappf_cb(lv_event_t *event) {
    (void)event;
}

void setappdf_cb(lv_event_t *event) {
    (void)event;
}

void setwific_cb(lv_event_t *event) {
    (void)event;

    remember_set_focus(SETTINGS_SET_FOCUS_WIFI);
    if (s_callbacks.on_wifi != NULL) {
        s_callbacks.on_wifi(s_callbacks.user_ctx);
    }
    open_network_page();
}

void setwifif_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setwifi, ui_setwifit);
}

void setwifidf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setwifi, ui_setwifit);
}

void setblec_cb(lv_event_t *event) {
    (void)event;
    static int btn_state;

    if (ui_setblesw != NULL && lv_obj_has_state(ui_setblesw, LV_STATE_DISABLED)) {
        return;
    }

    btn_state = get_ble_switch(UI_CALLER);
    switch (btn_state) {
    case 0:
        if (set_ble_switch(UI_CALLER, 1) == 0) {
            lv_obj_add_state(ui_setblesw, LV_STATE_CHECKED);
        }
        break;
    case 1:
        if (set_ble_switch(UI_CALLER, 0) == 0) {
            lv_obj_clear_state(ui_setblesw, LV_STATE_CHECKED);
        }
        break;
    default:
        break;
    }
}

void setblef_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setble, ui_setblet);
    if (ui_setblesw != NULL) {
        lv_obj_clear_flag(ui_setblesw, LV_OBJ_FLAG_HIDDEN);
    }
}

void setbledf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setble, ui_setblet);
    if (ui_setblesw != NULL) {
        lv_obj_add_flag(ui_setblesw, LV_OBJ_FLAG_HIDDEN);
    }
}

void setvolc_cb(lv_event_t *event) {
    (void)event;
    remember_set_focus(SETTINGS_SET_FOCUS_SOUND);
    ensure_page_initialized(&ui_Page_Slider, ui_Page_Slider_screen_init);
    s_slider_kind = FACTORY_SETTINGS_SLIDER_SOUND;
    volbri.vs_value = clamp_percent(s_state.sound_percent, 0);
    volbri.bs_value = clamp_percent(s_state.brightness_percent, 1);
    lv_slider_set_value(ui_vslider, volbri.vs_value, LV_ANIM_OFF);
    lv_slider_set_value(ui_bslider, volbri.bs_value, LV_ANIM_OFF);

    lv_label_set_text(ui_bvtitle, "Sound");
    lv_label_set_text(ui_bvbt, "Volume");
    lv_img_set_src(ui_bvbimg, &ui_img_volicon_png);
    lv_obj_add_flag(ui_bp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_vp, LV_OBJ_FLAG_HIDDEN);
    apply_slider_selection_style(FACTORY_SETTINGS_SLIDER_SOUND);

    lv_pm_open_page(s_group, &group_page_volume, PM_ADD_OBJS_TO_GROUP, &ui_Page_Slider, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_Slider_screen_init);
    note_original_page_opened(SETTINGS_PAGE_SLIDER, &group_page_volume, 0, true, true);
    lv_event_send(ui_vslider, LV_EVENT_VALUE_CHANGED, NULL);
}

void setvolf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setvol, ui_setvolt);
}

void setvoldf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setvol, ui_setvolt);
}

void setbric_cb(lv_event_t *event) {
    (void)event;
    remember_set_focus(SETTINGS_SET_FOCUS_BRIGHTNESS);
    ensure_page_initialized(&ui_Page_Slider, ui_Page_Slider_screen_init);
    s_slider_kind = FACTORY_SETTINGS_SLIDER_BRIGHTNESS;
    volbri.vs_value = clamp_percent(s_state.sound_percent, 0);
    volbri.bs_value = clamp_percent(s_state.brightness_percent, 1);
    lv_slider_set_value(ui_vslider, volbri.vs_value, LV_ANIM_OFF);
    lv_slider_set_value(ui_bslider, volbri.bs_value, LV_ANIM_OFF);

    lv_label_set_text(ui_bvtitle, "Display");
    lv_label_set_text(ui_bvbt, "Brightness");
    lv_img_set_src(ui_bvbimg, &ui_img_brighticon_png);
    lv_obj_clear_flag(ui_bp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_vp, LV_OBJ_FLAG_HIDDEN);
    apply_slider_selection_style(FACTORY_SETTINGS_SLIDER_BRIGHTNESS);

    lv_pm_open_page(s_group, &group_page_brightness, PM_ADD_OBJS_TO_GROUP, &ui_Page_Slider, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_Slider_screen_init);
    note_original_page_opened(SETTINGS_PAGE_SLIDER, &group_page_brightness, 0, true, true);
    lv_event_send(ui_bslider, LV_EVENT_VALUE_CHANGED, NULL);
}

void setbrif_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setbri, ui_setbrit);
}

void setbridf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setbri, ui_setbrit);
}

void settimec_cb(lv_event_t *event) {
    (void)event;
    ensure_page_initialized(&ui_Page_Sleep, ui_Page_Sleep_screen_init);
    lv_pm_open_page(s_group, &group_page_sleep, PM_ADD_OBJS_TO_GROUP, &ui_Page_Sleep, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_Sleep_screen_init);
    note_original_page_opened(SETTINGS_PAGE_SLEEP, &group_page_sleep, 0, false, true);
    lv_roller_set_selected(ui_sleeptimeroller, g_screenoff_time, LV_ANIM_OFF);
    if (g_screenoff_switch != 0) {
        lv_obj_add_state(ui_sleepswitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(ui_sleepswitch, LV_STATE_CHECKED);
    }
}

void settimef_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_settime, ui_settimt);
}

void settimedf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_settime, ui_settimt);
}

void setrgbc_cb(lv_event_t *event) {
    static uint32_t s_last_rgb_toggle_ms = 0;
    uint32_t now_ms = lv_tick_get();
    bool target_enabled;

    (void)event;

    if (s_last_rgb_toggle_ms != 0 && lv_tick_elaps(s_last_rgb_toggle_ms) < SETTINGS_RGB_TOGGLE_DEBOUNCE_MS) {
        return;
    }
    s_last_rgb_toggle_ms = now_ms;

    target_enabled = !s_state.rgb_enabled;
    if (set_rgb_switch(UI_CALLER, target_enabled ? 1 : 0) == 0 && ui_setrgbsw != NULL) {
        if (target_enabled) {
            lv_obj_add_state(ui_setrgbsw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_setrgbsw, LV_STATE_CHECKED);
        }
    }
}

void setrgbf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setrgb, ui_setrgbt);
    if (ui_setrgbsw != NULL) {
        lv_obj_clear_flag(ui_setrgbsw, LV_OBJ_FLAG_HIDDEN);
    }
}

void setrgbdf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setrgb, ui_setrgbt);
    if (ui_setrgbsw != NULL) {
        lv_obj_add_flag(ui_setrgbsw, LV_OBJ_FLAG_HIDDEN);
    }
}

void setwwc_cb(lv_event_t *event) {
    (void)event;
}

void setwwf_cb(lv_event_t *event) {
    (void)event;
}

void setwwdf_cb(lv_event_t *event) {
    (void)event;
}

void setdevc_cb(lv_event_t *event) {
    (void)event;
    remember_set_focus(SETTINGS_SET_FOCUS_ABOUT);
    ensure_page_initialized(&ui_Page_About, ui_Page_About_screen_init);
    update_about_labels();
    lv_pm_open_page(s_group, &group_page_about, PM_ADD_OBJS_TO_GROUP, &ui_Page_About, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_About_screen_init);
    note_original_page_opened(SETTINGS_PAGE_ABOUT, &group_page_about, 0, false, false);
    lv_group_focus_obj(ui_aboutdevname);
    lv_group_set_wrap(s_group, false);
}

void setdevf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setdev, ui_setdevt);
}

void setdevdf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setdev, ui_setdevt);
}

void setdownc_cb(lv_event_t *event) {
    (void)event;
    remember_set_focus(SETTINGS_SET_FOCUS_REBOOT);
    ensure_page_initialized(&ui_Page_Swipe, ui_Page_Swipe_screen_init);
    g_swipeid = 0;
    g_shutdown = 0;
    s_confirm_item = FACTORY_SETTINGS_ITEM_REBOOT;
    lv_pm_open_page(s_group, &group_page_swipe, PM_CLEAR_GROUP, &ui_Page_Swipe, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_Swipe_screen_init);
    lv_slider_set_value(ui_spsilder, 0, LV_ANIM_ON);
    Page_shutdown();
    lv_group_add_obj(s_group, ui_spback);
    note_original_page_opened(SETTINGS_PAGE_SWIPE, &group_page_swipe, 0, false, true);
}

void setdownf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setdown, ui_setdownt);
}

void setdowndf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setdown, ui_setdownt);
}

void setbackf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setback, ui_setbackt);
    if (ui_setback != NULL) {
        lv_obj_set_width(ui_setback, 300);
    }
    if (ui_Image1 != NULL) {
        lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
}

void setbackdf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setback, ui_setbackt);
    if (ui_Image1 != NULL) {
        lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
}

void setfac_cb(lv_event_t *event) {
    (void)event;
    remember_set_focus(SETTINGS_SET_FOCUS_FACTORY_RESET);
    ensure_page_initialized(&ui_Page_Swipe, ui_Page_Swipe_screen_init);
    g_swipeid = 1;
    s_confirm_item = FACTORY_SETTINGS_ITEM_FACTORY_RESET;
    lv_pm_open_page(s_group, &group_page_swipe, PM_CLEAR_GROUP, &ui_Page_Swipe, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    &ui_Page_Swipe_screen_init);
    lv_slider_set_value(ui_spsilder, 0, LV_ANIM_ON);
    Page_facreset();
    lv_group_add_obj(s_group, ui_spback);
    note_original_page_opened(SETTINGS_PAGE_SWIPE, &group_page_swipe, 0, false, true);
}

void setfacf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_focused(ui_setfac, ui_setfact);
}

void setfacdf_cb(lv_event_t *event) {
    (void)event;
    set_obj_style_defocused(ui_setfac, ui_setfact);
}

void volvc_cb(lv_event_t *event) {
    (void)event;
    volbri.vs_value = lv_slider_get_value(ui_vslider);
    const int value = clamp_percent((int)volbri.vs_value, 0);

    static char vs_buffer[10];
    lv_snprintf(vs_buffer, sizeof(vs_buffer), "%d", value);
    lv_label_set_text(ui_bvbv, vs_buffer);
    if (s_state.sound_percent != value) {
        set_sound(UI_CALLER, value);
    } else {
        s_state.sound_percent = value;
    }
}

void volre_cb(lv_event_t *event) {
    (void)event;
    set_sound(UI_CALLER, volbri.vs_value);
}

void volfs_cb(lv_event_t *event) {
    (void)event;
    apply_slider_selection_style(s_slider_kind);
}

void voldf_cb(lv_event_t *event) {
    (void)event;
    apply_slider_selection_style(s_slider_kind);
}

void brivc_cb(lv_event_t *event) {
    (void)event;
    volbri.bs_value = lv_slider_get_value(ui_bslider);
    const int value = clamp_percent((int)volbri.bs_value, 1);

    static char bs_buffer[10];
    lv_snprintf(bs_buffer, sizeof(bs_buffer), "%d", value);
    lv_label_set_text(ui_bvbv, bs_buffer);
    if (s_state.brightness_percent != value) {
        set_brightness(UI_CALLER, value);
    } else {
        s_state.brightness_percent = value;
    }
}

void brire_cb(lv_event_t *event) {
    (void)event;
    set_brightness(UI_CALLER, volbri.bs_value);
}

void brifs_cb(lv_event_t *event) {
    (void)event;
    apply_slider_selection_style(s_slider_kind);
}

void bridf_cb(lv_event_t *event) {
    (void)event;
    apply_slider_selection_style(s_slider_kind);
}

void abdnf_cb(lv_event_t *event) {
    (void)event;
}

void abdndf_cb(lv_event_t *event) {
    (void)event;
}

void absvf_cb(lv_event_t *event) {
    (void)event;
}

void absvdf_cb(lv_event_t *event) {
    (void)event;
}

void absnf_cb(lv_event_t *event) {
    (void)event;
}

void absndf_cb(lv_event_t *event) {
    (void)event;
}

void abeuif_cb(lv_event_t *event) {
    (void)event;
}

void abeuidf_cb(lv_event_t *event) {
    (void)event;
}

void abblef_cb(lv_event_t *event) {
    (void)event;
}

void abbledf_cb(lv_event_t *event) {
    (void)event;
}

void slidervc_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    int32_t value;

    if (ui_spsilder == NULL || code != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    value = lv_slider_get_value(ui_spsilder);
    const uint32_t color = value > 80 ? 0xFF5656 : 0xD47C2A;

    lv_obj_set_style_bg_color(ui_spsilder, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_spsilder, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_spsilder, lv_color_hex(color), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

void sliderr_cb(lv_event_t *event) {
    static int32_t slider_value;

    (void)event;
    if (ui_spsilder == NULL) {
        return;
    }
    slider_value = lv_slider_get_value(ui_spsilder);
    if (slider_value > 80) {
        switch (g_swipeid) {
        case 0:
            if (g_shutdown == 0 && s_callbacks.on_reboot != NULL) {
                s_callbacks.on_reboot(s_callbacks.user_ctx);
            }
            if (g_shutdown == 1 && s_callbacks.on_screen_off != NULL) {
                s_callbacks.on_screen_off(s_callbacks.user_ctx);
            }
            break;
        case 1:
            if (ui_swipep2 != NULL) {
                lv_obj_clear_flag(ui_swipep2, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_callbacks.on_factory_reset != NULL) {
                s_callbacks.on_factory_reset(s_callbacks.user_ctx);
            }
            break;
        default:
            break;
        }
    } else {
        lv_slider_set_value(ui_spsilder, 0, LV_ANIM_OFF);
    }
}

void sleeptimeset_cb(lv_event_t *event) {
    static uint16_t sleep_time_roller_id;
    lv_obj_t *obj = lv_event_get_target(event);

    if (obj != NULL) {
        sleep_time_roller_id = lv_roller_get_selected(obj);
        set_screenoff_time(UI_CALLER, sleep_time_roller_id);
        g_screenoff_time = get_screenoff_time(UI_CALLER);
    }
}

void setsleepsw_cb(lv_event_t *event) {
    (void)event;
    if (ui_sleepswitch != NULL) {
        lv_event_send(ui_sleepswitch, LV_EVENT_CLICKED, NULL);
    }
}

void sleepswitch_cb(lv_event_t *event) {
    (void)event;
    if (ui_sleepswitch == NULL) {
        return;
    }
    static int btn_state;
    btn_state = get_screenoff_switch(UI_CALLER);
    switch (btn_state) {
    case 0:
        set_screenoff_switch(UI_CALLER, 1);
        lv_obj_add_state(ui_sleepswitch, LV_STATE_CHECKED);
        g_screenoff_switch = get_screenoff_switch(UI_CALLER);
        break;
    case 1:
        set_screenoff_switch(UI_CALLER, 0);
        lv_obj_clear_state(ui_sleepswitch, LV_STATE_CHECKED);
        g_screenoff_switch = get_screenoff_switch(UI_CALLER);
        break;
    default:
        break;
    }
}

void ui_event_Page_Set(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        sgesup_cb(event);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        sgesdown_cb(event);
    }
    if (event_code == LV_EVENT_CLICKED) {
        sclick_cb(event);
    }
    if (event_code == LV_EVENT_SCREEN_LOADED) {
        setsl_cb(event);
    }
}

void ui_event_setback(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        setbackf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setbackdf_cb(event);
    }
    if (event_code == LV_EVENT_CLICKED) {
        backmenu_cb(event);
    }
}

void ui_event_setdown(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setdownc_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setdownf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setdowndf_cb(event);
    }
}

void ui_event_setapp(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        setappf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setappdf_cb(event);
    }
    if (event_code == LV_EVENT_CLICKED) {
        setappc_cb(event);
    }
}

void ui_event_setwifi(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setwific_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setwifif_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setwifidf_cb(event);
    }
}

void ui_event_setble(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setblec_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setblef_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setbledf_cb(event);
    }
}

void ui_event_setvol(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setvolc_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setvolf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setvoldf_cb(event);
    }
}

void ui_event_setbri(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setbric_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setbrif_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setbridf_cb(event);
    }
}

void ui_event_settime(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        settimec_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        settimef_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        settimedf_cb(event);
    }
}

void ui_event_setrgb(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setrgbc_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setrgbf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setrgbdf_cb(event);
    }
}

void ui_event_setww(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setwwc_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setwwf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setwwdf_cb(event);
    }
}

void ui_event_setdev(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        setdevf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setdevdf_cb(event);
    }
    if (event_code == LV_EVENT_CLICKED) {
        setdevc_cb(event);
    }
}

void ui_event_setfac(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setfac_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        setfacf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        setfacdf_cb(event);
    }
}

void ui_event_bvback(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        backset_cb(event);
    }
}

void ui_event_vslider(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    preserve_slider_editing_on_touch(event);

    if (event_code == LV_EVENT_VALUE_CHANGED) {
        volvc_cb(event);
    }
    if (event_code == LV_EVENT_RELEASED) {
        volre_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        volfs_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        voldf_cb(event);
    }
}

void ui_event_bslider(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    preserve_slider_editing_on_touch(event);

    if (event_code == LV_EVENT_VALUE_CHANGED) {
        brivc_cb(event);
    }
    if (event_code == LV_EVENT_RELEASED) {
        brire_cb(event);
    }
    if (event_code == LV_EVENT_FOCUSED) {
        brifs_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        bridf_cb(event);
    }
}

void ui_event_aboutdevname(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        abdnf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        abdndf_cb(event);
    }
}

void ui_event_aboutespversion(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        absvf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        absvdf_cb(event);
    }
}

void ui_event_aboutaiversion(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        absvf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        absvdf_cb(event);
    }
}

void ui_event_aboutsn(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        absnf_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        absndf_cb(event);
    }
}

void ui_event_abouteui(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        abeuif_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        abeuidf_cb(event);
    }
}

void ui_event_aboutblemac(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        abblef_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        abbledf_cb(event);
    }
}

void ui_event_aboutwifimac(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_FOCUSED) {
        abblef_cb(event);
    }
    if (event_code == LV_EVENT_DEFOCUSED) {
        abbledf_cb(event);
    }
}

void ui_event_Paboutb(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        backset_cb(event);
    }
}

void ui_event_spsilder(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_VALUE_CHANGED) {
        slidervc_cb(event);
    }
    if (event_code == LV_EVENT_RELEASED) {
        sliderr_cb(event);
    }
}

void ui_event_spback(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        backset_cb(event);
    }
}

void ui_event_sleeptimeroller(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_VALUE_CHANGED) {
        sleeptimeset_cb(event);
    }
}

void ui_event_slpback(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        backset_cb(event);
    }
}

void ui_event_sleepswitchp(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        setsleepsw_cb(event);
    }
}

void ui_event_sleepswitch(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        sleepswitch_cb(event);
    }
}

void ui_event_conncancel(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        backset_cb(event);
    }
}

void ui_event_connp1(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        Page_ConnAPP_BLE();
        if (s_group != NULL && ui_connp2 != NULL) {
            lv_group_focus_obj(ui_connp2);
        }
    }
    if (event_code == LV_EVENT_FOCUSED) {
        Page_ConnAPP_Mate();
    }
}

void ui_event_arrow1(lv_event_t *event) {
    ui_event_connp1(event);
}

void ui_event_connp2(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        Page_ConnAPP_Mate();
        if (s_group != NULL && ui_connp1 != NULL) {
            lv_group_focus_obj(ui_connp1);
        }
    }
    if (event_code == LV_EVENT_FOCUSED) {
        Page_ConnAPP_BLE();
    }
}

void ui_event_arrow2(lv_event_t *event) {
    ui_event_connp2(event);
}

void ui_event_wificancel(lv_event_t *event) {
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_CLICKED) {
        if (factory_settings_ui_handle_network_cancel()) {
            return;
        }
        if (s_callbacks.on_wifi_detail_closed != NULL) {
            s_callbacks.on_wifi_detail_closed(s_callbacks.user_ctx);
        }
        backset_cb(event);
    }
}
