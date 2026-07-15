#include "launcher_factory_home.h"

#include <stdint.h>
#include <string.h>
#include "factory_home_ui/ui.h"

static lv_obj_t *s_page_home = NULL;
static lv_obj_t *s_mainlist = NULL;
static lv_obj_t *s_maincontent = NULL;
static lv_obj_t *s_devicep = NULL;
static lv_obj_t *s_maintime = NULL;
static lv_obj_t *s_mainwifi = NULL;
static lv_obj_t *s_mainb = NULL;
static lv_obj_t *s_btpert = NULL;
static lv_obj_t *s_mainble = NULL;
static lv_obj_t *s_maintitle = NULL;
static lv_obj_t *s_mcontrolp = NULL;
static lv_obj_t *s_battery_warn_page = NULL;
static const launcher_factory_home_entry_t *s_entries = NULL;
static launcher_factory_home_callbacks_t s_callbacks;
static size_t s_entry_count = 0;
static int s_focused_index = 0;
static lv_group_t *s_group = NULL;

static lv_obj_t *s_slots[LAUNCHER_FACTORY_HOME_ENTRY_COUNT] = {NULL};
static lv_obj_t *s_buttons[LAUNCHER_FACTORY_HOME_ENTRY_COUNT] = {NULL};

static bool entry_index_is_visible(int index) {
    return index >= 0 && (size_t)index < s_entry_count;
}

static void transparent_panel(lv_obj_t *obj, uint32_t border_color) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void apply_factory_button_style(lv_obj_t *button, const launcher_factory_home_entry_t *entry) {
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(button, entry->default_img, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(button, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_img_src(button, entry->focused_img, LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_img_src(button, entry->focused_img, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(button, 0, LV_PART_MAIN | LV_STATE_FOCUSED);

    lv_obj_set_style_bg_img_src(button, entry->focused_img, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(button, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

static void update_checked_state(void) {
    for (int i = 0; i < (int)s_entry_count; ++i) {
        if (s_buttons[i] == NULL) {
            continue;
        }
        if (i == s_focused_index) {
            lv_obj_add_state(s_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_buttons[i], LV_STATE_CHECKED);
        }
    }
}

static void update_title(int index) {
    if (s_maintitle == NULL || s_entries == NULL || !entry_index_is_visible(index)) {
        return;
    }

    lv_obj_set_y(s_maintitle, strchr(s_entries[index].title, '\n') != NULL ? 0 : 20);
    lv_label_set_text(s_maintitle, s_entries[index].title);
}

static void main_scroll_cb(lv_event_t *event) {
    lv_obj_t *cont = lv_event_get_target(event);
    lv_area_t cont_area;
    int32_t cont_y_center;
    int32_t radius = 245;
    uint32_t child_count;

    lv_obj_get_coords(cont, &cont_area);
    cont_y_center = lv_area_get_height(&cont_area) / 2;
    child_count = lv_obj_get_child_cnt(cont);

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_area_t child_area;
        int32_t child_y_center;
        int32_t diff_y;
        int32_t x;

        lv_obj_get_coords(child, &child_area);
        child_y_center = child_area.y1 + lv_area_get_height(&child_area) / 2;
        diff_y = child_y_center - cont_y_center;
        diff_y = LV_ABS(diff_y);

        if (diff_y >= radius) {
            x = -radius;
        } else {
            uint32_t x_sqr = radius * radius - diff_y * diff_y;
            lv_sqrt_res_t res;
            lv_sqrt(x_sqr, &res, 0x8000);
            x = -(radius - res.i);
        }

        lv_obj_set_style_translate_x(child, x, 0);
        lv_obj_set_style_opa(child, 250 + x + x, 0);
    }
}

static void main_button_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    int index = (int)(intptr_t)lv_event_get_user_data(event);

    if (!entry_index_is_visible(index)) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        s_focused_index = index;
        update_checked_state();
        if (s_callbacks.on_clicked != NULL) {
            s_callbacks.on_clicked(index, s_callbacks.user_ctx);
        }
    }

    if (code == LV_EVENT_FOCUSED) {
        s_focused_index = index;
        update_title(index);
        update_checked_state();
        if (s_callbacks.on_focused != NULL) {
            s_callbacks.on_focused(index, s_callbacks.user_ctx);
        }
    }
}

static void page_home_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        if (s_group != NULL) {
            lv_group_focus_next(s_group);
        }
    }
    if (code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        if (s_group != NULL) {
            lv_group_focus_prev(s_group);
        }
    }
    if (code == LV_EVENT_CLICKED) {
        launcher_factory_home_click_focused();
    }
}

static lv_obj_t *create_slot(lv_obj_t *parent, int index) {
    lv_obj_t *slot = lv_obj_create(parent);

    lv_obj_set_width(slot, 90);
    lv_obj_set_height(slot, 90);
    if (index == 2 || index == 3) {
        lv_obj_set_align(slot, LV_ALIGN_LEFT_MID);
    } else {
        lv_obj_set_align(slot, LV_ALIGN_CENTER);
    }
    if (index == 3) {
        lv_obj_set_x(slot, 15);
        lv_obj_set_y(slot, 110);
    }
    transparent_panel(slot, index == 3 ? 0xFFFFFF : 0x000000);
    return slot;
}

static lv_obj_t *create_button(lv_obj_t *slot, int index) {
    lv_obj_t *button = lv_btn_create(slot);

    lv_obj_set_width(button, 80);
    lv_obj_set_height(button, 80);
    lv_obj_set_align(button, LV_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    apply_factory_button_style(button, &s_entries[index]);
    lv_obj_add_event_cb(button, main_button_event_cb, LV_EVENT_ALL, (void *)(intptr_t)index);
    return button;
}

static void scroll_anim_enable(void) {
    lv_obj_set_scroll_snap_y(s_mainlist, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(s_mainlist, LV_DIR_VER);
    lv_obj_add_event_cb(s_mainlist, main_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_scroll_to_view(lv_obj_get_child(s_mainlist, 0), LV_ANIM_OFF);
    lv_event_send(s_mainlist, LV_EVENT_SCROLL, NULL);
}

void launcher_factory_home_build(lv_obj_t *screen,
                                 const launcher_factory_home_entry_t entries[LAUNCHER_FACTORY_HOME_ENTRY_COUNT],
                                 size_t entry_count,
                                 const launcher_factory_home_callbacks_t *callbacks) {
    lv_obj_t *battery_warn_img = NULL;

    if (screen == NULL || entries == NULL || entry_count == 0) {
        return;
    }

    launcher_factory_home_reset();
    s_page_home = screen;
    s_entries = entries;
    s_entry_count = entry_count > LAUNCHER_FACTORY_HOME_ENTRY_COUNT ? LAUNCHER_FACTORY_HOME_ENTRY_COUNT : entry_count;
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    lv_obj_clear_flag(s_page_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_page_home, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_page_home, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(s_page_home, &ui_img_page_main_png, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_mainlist = lv_obj_create(s_page_home);
    lv_obj_set_width(s_mainlist, 130);
    lv_obj_set_height(s_mainlist, 399);
    lv_obj_set_x(s_mainlist, 140);
    lv_obj_set_y(s_mainlist, 0);
    lv_obj_set_align(s_mainlist, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_mainlist, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mainlist, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);
    lv_obj_add_flag(s_mainlist, LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(s_mainlist, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_mainlist, LV_DIR_VER);
    lv_obj_set_style_bg_color(s_mainlist, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_mainlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_mainlist, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_mainlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    for (int i = 0; i < (int)s_entry_count; ++i) {
        s_slots[i] = create_slot(s_mainlist, i);
        s_buttons[i] = create_button(s_slots[i], i);
    }

    s_maincontent = lv_obj_create(s_page_home);
    lv_obj_set_width(s_maincontent, 263);
    lv_obj_set_height(s_maincontent, 240);
    lv_obj_set_x(s_maincontent, 10);
    lv_obj_set_y(s_maincontent, 0);
    lv_obj_set_align(s_maincontent, LV_ALIGN_LEFT_MID);
    lv_obj_set_flex_flow(s_maincontent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_maincontent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    transparent_panel(s_maincontent, 0x000000);

    s_devicep = lv_obj_create(s_maincontent);
    lv_obj_set_width(s_devicep, 256);
    lv_obj_set_height(s_devicep, 50);
    lv_obj_set_x(s_devicep, -19);
    lv_obj_set_y(s_devicep, -1);
    lv_obj_set_align(s_devicep, LV_ALIGN_LEFT_MID);
    lv_obj_set_flex_flow(s_devicep, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_devicep, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    transparent_panel(s_devicep, 0x000000);

    s_maintime = lv_label_create(s_devicep);
    lv_obj_set_width(s_maintime, LV_SIZE_CONTENT);
    lv_obj_set_height(s_maintime, LV_SIZE_CONTENT);
    lv_obj_set_align(s_maintime, LV_ALIGN_CENTER);
    lv_label_set_text(s_maintime, "--:--");
    lv_obj_set_style_text_color(s_maintime, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_maintime, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_maintime, &ui_font_semibold28, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_mainwifi = lv_img_create(s_devicep);
    lv_img_set_src(s_mainwifi, &ui_img_no_wifi_png);
    lv_obj_set_width(s_mainwifi, LV_SIZE_CONTENT);
    lv_obj_set_height(s_mainwifi, LV_SIZE_CONTENT);
    lv_obj_set_align(s_mainwifi, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_mainwifi, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_mainwifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_img_recolor(s_mainwifi, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(s_mainwifi, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_mainb = lv_img_create(s_devicep);
    lv_img_set_src(s_mainb, &ui_img_battery_frame_png);
    lv_obj_set_width(s_mainb, LV_SIZE_CONTENT);
    lv_obj_set_height(s_mainb, LV_SIZE_CONTENT);
    lv_obj_set_align(s_mainb, LV_ALIGN_LEFT_MID);
    lv_obj_add_flag(s_mainb, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_mainb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_img_recolor(s_mainb, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(s_mainb, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_btpert = lv_label_create(s_mainb);
    lv_obj_set_width(s_btpert, LV_SIZE_CONTENT);
    lv_obj_set_height(s_btpert, LV_SIZE_CONTENT);
    lv_obj_set_x(s_btpert, 9);
    lv_obj_set_y(s_btpert, -1);
    lv_obj_set_align(s_btpert, LV_ALIGN_LEFT_MID);
    lv_label_set_text(s_btpert, "--");
    lv_obj_set_style_text_color(s_btpert, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_btpert, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_btpert, &ui_font_Font12, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_mainble = lv_img_create(s_devicep);
    lv_img_set_src(s_mainble, &ui_img_ble_png);
    lv_obj_set_width(s_mainble, LV_SIZE_CONTENT);
    lv_obj_set_height(s_mainble, LV_SIZE_CONTENT);
    lv_obj_set_align(s_mainble, LV_ALIGN_LEFT_MID);
    lv_obj_add_flag(s_mainble, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_mainble, LV_OBJ_FLAG_SCROLLABLE);
    lv_img_set_zoom(s_mainble, 200);
    lv_obj_set_style_img_recolor(s_mainble, lv_color_hex(0x171515), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(s_mainble, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_maintitle = lv_label_create(s_page_home);
    lv_obj_set_width(s_maintitle, LV_SIZE_CONTENT);
    lv_obj_set_height(s_maintitle, LV_SIZE_CONTENT);
    lv_obj_set_x(s_maintitle, 50);
    lv_obj_set_y(s_maintitle, 0);
    lv_obj_set_align(s_maintitle, LV_ALIGN_LEFT_MID);
    lv_label_set_text(s_maintitle, s_entries[0].title);
    lv_obj_set_style_text_color(s_maintitle, lv_color_hex(0xA9DE2C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_maintitle, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_maintitle, &ui_font_semibold42, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_mcontrolp = lv_obj_create(s_page_home);
    lv_obj_set_width(s_mcontrolp, 412);
    lv_obj_set_height(s_mcontrolp, 412);
    lv_obj_set_x(s_mcontrolp, 0);
    lv_obj_set_y(s_mcontrolp, 1);
    lv_obj_set_align(s_mcontrolp, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_mcontrolp, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_mcontrolp, LV_OBJ_FLAG_SCROLLABLE);
    transparent_panel(s_mcontrolp, 0x000000);

    lv_obj_add_event_cb(s_page_home, page_home_event_cb, LV_EVENT_ALL, NULL);

    s_battery_warn_page = lv_obj_create(screen);
    lv_obj_set_width(s_battery_warn_page, 412);
    lv_obj_set_height(s_battery_warn_page, 412);
    lv_obj_set_align(s_battery_warn_page, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_battery_warn_page, LV_OBJ_FLAG_SCROLLABLE);
    transparent_panel(s_battery_warn_page, 0x000000);
    lv_obj_add_flag(s_battery_warn_page, LV_OBJ_FLAG_HIDDEN);

    battery_warn_img = lv_img_create(s_battery_warn_page);
    lv_img_set_src(battery_warn_img, &ui_img_battery_warn_png);
    lv_obj_set_width(battery_warn_img, LV_SIZE_CONTENT);
    lv_obj_set_height(battery_warn_img, LV_SIZE_CONTENT);
    lv_obj_set_align(battery_warn_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(battery_warn_img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(battery_warn_img, LV_OBJ_FLAG_SCROLLABLE);

    scroll_anim_enable();
    update_title(0);
    update_checked_state();
}

void launcher_factory_home_reset(void) {
    s_page_home = NULL;
    s_mainlist = NULL;
    s_maincontent = NULL;
    s_devicep = NULL;
    s_maintime = NULL;
    s_mainwifi = NULL;
    s_mainb = NULL;
    s_btpert = NULL;
    s_mainble = NULL;
    s_maintitle = NULL;
    s_mcontrolp = NULL;
    s_battery_warn_page = NULL;
    s_entries = NULL;
    s_entry_count = 0;
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_buttons, 0, sizeof(s_buttons));
    s_focused_index = 0;
    s_group = NULL;
}

void launcher_factory_home_bind_group(lv_group_t **group, int focused_index) {
    lv_indev_t *indev = NULL;

    if (group == NULL) {
        return;
    }

    if (*group == NULL) {
        *group = lv_group_create();
        if (*group == NULL) {
            return;
        }
    } else {
        lv_group_remove_all_objs(*group);
    }

    s_group = *group;
    lv_group_set_wrap(*group, true);
    for (int i = 0; i < (int)s_entry_count; ++i) {
        if (s_buttons[i] != NULL) {
            lv_group_add_obj(*group, s_buttons[i]);
        }
    }

    launcher_factory_home_focus_index(focused_index, false);
    lv_group_set_editing(*group, false);

    indev = lv_indev_get_next(NULL);
    while (indev != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(indev, *group);
        }
        indev = lv_indev_get_next(indev);
    }
}

void launcher_factory_home_focus_index(int index, bool animate) {
    if (!entry_index_is_visible(index) || s_buttons[index] == NULL) {
        return;
    }

    s_focused_index = index;
    lv_group_focus_obj(s_buttons[index]);
    lv_obj_scroll_to_view(lv_obj_get_parent(s_buttons[index]), animate ? LV_ANIM_ON : LV_ANIM_OFF);
    if (s_mainlist != NULL) {
        lv_event_send(s_mainlist, LV_EVENT_SCROLL, NULL);
    }
    update_title(index);
    update_checked_state();
}

void launcher_factory_home_click_focused(void) {
    lv_obj_t *focused = s_group != NULL ? lv_group_get_focused(s_group) : NULL;

    if (focused != NULL) {
        lv_event_send(focused, LV_EVENT_CLICKED, NULL);
    }
}

void launcher_factory_home_update_status(const char *time_text,
                                         const char *battery_text,
                                         launcher_home_battery_state_t battery_state,
                                         bool wifi_connected,
                                         bool ble_connected) {
    if (s_maintime != NULL && time_text != NULL) {
        lv_label_set_text(s_maintime, time_text);
    }
    if (s_mainb != NULL) {
        if (battery_state == LAUNCHER_HOME_BATTERY_CHARGING) {
            lv_img_set_src(s_mainb, &ui_img_battery_charging_png);
        } else {
            lv_img_set_src(s_mainb, &ui_img_battery_frame_png);
        }
    }
    if (s_btpert != NULL && battery_text != NULL) {
        lv_label_set_text(s_btpert, battery_text);
        if (battery_state == LAUNCHER_HOME_BATTERY_CHARGING) {
            lv_obj_add_flag(s_btpert, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_btpert, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_battery_warn_page != NULL) {
        if (battery_state == LAUNCHER_HOME_BATTERY_LOW) {
            lv_obj_clear_flag(s_battery_warn_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_battery_warn_page);
        } else {
            lv_obj_add_flag(s_battery_warn_page, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_mainwifi != NULL) {
        lv_img_set_src(s_mainwifi, wifi_connected ? &ui_img_wifi_3_png : &ui_img_no_wifi_png);
        lv_obj_set_style_img_recolor(s_mainwifi, lv_color_hex(wifi_connected ? 0xA9DE2C : 0x000000),
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(s_mainwifi, wifi_connected ? 255 : 0,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_mainble != NULL) {
        lv_obj_set_style_img_recolor(s_mainble, lv_color_hex(ble_connected ? 0xA9DE2C : 0x171515),
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(s_mainble, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}
