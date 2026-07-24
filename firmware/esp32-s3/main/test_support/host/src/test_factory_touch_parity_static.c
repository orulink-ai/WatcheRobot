#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static bool rect_fits_round_screen_x2(int center_x2, int center_y2, int radius_x2, int x, int y, int width,
                                      int height) {
    const int left_x2 = center_x2 + (2 * x) - width;
    const int right_x2 = center_x2 + (2 * x) + width;
    const int top_y2 = center_y2 + (2 * y) - height;
    const int bottom_y2 = center_y2 + (2 * y) + height;
    const int corners[][2] = {
        {left_x2, top_y2},
        {right_x2, top_y2},
        {left_x2, bottom_y2},
        {right_x2, bottom_y2},
    };

    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        const int dx = corners[i][0] - center_x2;
        const int dy = corners[i][1] - center_y2;
        if ((dx * dx) + (dy * dy) > radius_x2 * radius_x2) {
            return false;
        }
    }
    return true;
}

static int check_app_center_status_layout_geometry(void) {
    static const struct {
        const char *name;
        int x;
        int y;
        int width;
        int height;
    } rects[] = {
        {"App.Center status icon", 0, -150, 44, 44},      {"App.Center status title", 0, -67, 350, 30},
        {"App.Center status accent", 0, -34, 350, 24},    {"App.Center status body", 0, -6, 350, 52},
        {"App.Center download progress", 0, 64, 184, 12}, {"App.Center download percent", 0, 88, 350, 24},
        {"App.Center download button", 0, 116, 184, 42},
    };
    int failures = 0;
    const int center_x2 = 412;
    const int center_y2 = 412;
    const int radius_x2 = 412;

    for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); ++i) {
        failures += expect_true(rect_fits_round_screen_x2(center_x2, center_y2, radius_x2, rects[i].x, rects[i].y,
                                                          rects[i].width, rects[i].height),
                                rects[i].name);
    }
    return failures;
}

static int check_app_center_list_detail_layout_geometry(void) {
    static const struct {
        const char *name;
        int x;
        int y;
        int width;
        int height;
    } rects[] = {
        {"App.Center list title", 0, -155, 180, 30},
        {"App.Center list status", 0, -126, 236, 24},
        {"App.Center list card", 0, -40, 206, 96},
        {"App.Center list card name", 0, -56, 174, 30},
        {"App.Center list card state", 0, -21, 174, 24},
        {"App.Center detail header title", 0, -168, 180, 30},
        {"App.Center detail header status", 0, -138, 236, 24},
        {"App.Center detail panel", 0, 14, 236, 194},
        {"App.Center detail name", 0, -52, 188, 30},
        {"App.Center detail state", 0, -19, 188, 24},
        {"App.Center detail body", 0, 15, 188, 42},
        {"App.Center detail meta", 0, 46, 188, 18},
    };
    int failures = 0;
    const int center_x2 = 412;
    const int center_y2 = 412;
    const int radius_x2 = 412;

    for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); ++i) {
        failures += expect_true(rect_fits_round_screen_x2(center_x2, center_y2, radius_x2, rects[i].x, rects[i].y,
                                                          rects[i].width, rects[i].height),
                                rects[i].name);
    }
    return failures;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *buffer;

    if (file == NULL) {
        fprintf(stderr, "FAIL: open %s\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buffer = (char *)malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1u, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static char *join_path(const char *root, const char *relative) {
    const size_t root_len = strlen(root);
    const size_t rel_len = strlen(relative);
    char *path = (char *)malloc(root_len + rel_len + 2u);

    if (path == NULL) {
        return NULL;
    }
    memcpy(path, root, root_len);
    path[root_len] = '/';
    memcpy(path + root_len + 1u, relative, rel_len + 1u);
    return path;
}

static bool file_exists(const char *root, const char *relative) {
    char *path = join_path(root, relative);
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    bool exists = file != NULL;

    if (file != NULL) {
        fclose(file);
    }
    free(path);
    return exists;
}

static char *remove_whitespace(const char *text, size_t length) {
    char *normalized = (char *)malloc(length + 1u);
    size_t write_index = 0;

    if (normalized == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!isspace((unsigned char)text[i])) {
            normalized[write_index++] = text[i];
        }
    }
    normalized[write_index] = '\0';
    return normalized;
}

static bool contains_ignoring_whitespace(const char *text, size_t text_length, const char *pattern) {
    char *normalized_text = remove_whitespace(text, text_length);
    char *normalized_pattern = remove_whitespace(pattern, strlen(pattern));
    bool found =
        normalized_text != NULL && normalized_pattern != NULL && strstr(normalized_text, normalized_pattern) != NULL;

    free(normalized_text);
    free(normalized_pattern);
    return found;
}

static int file_contains_all(const char *root, const char *relative, const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;

    failures += expect_true(text != NULL, relative);
    if (text != NULL) {
        for (size_t i = 0; i < count; ++i) {
            failures += expect_true(contains_ignoring_whitespace(text, strlen(text), patterns[i]), patterns[i]);
        }
    }
    free(text);
    free(path);
    return failures;
}

static int file_contains_none(const char *root, const char *relative, const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;

    failures += expect_true(text != NULL, relative);
    if (text != NULL) {
        for (size_t i = 0; i < count; ++i) {
            failures += expect_true(!contains_ignoring_whitespace(text, strlen(text), patterns[i]), patterns[i]);
        }
    }
    free(text);
    free(path);
    return failures;
}

static int check_group_binding_is_encoder_only(const char *root, const char *relative) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;
    const char *cursor;

    failures += expect_true(text != NULL, relative);
    if (text == NULL) {
        free(path);
        return failures;
    }

    cursor = text;
    while ((cursor = strstr(cursor, "lv_indev_set_group")) != NULL) {
        const char *window_start = cursor > text + 220 ? cursor - 220 : text;
        const size_t window_len = (size_t)(cursor - window_start);
        char window[221];

        memcpy(window, window_start, window_len);
        window[window_len] = '\0';
        failures += expect_true(strstr(window, "LV_INDEV_TYPE_ENCODER") != NULL,
                                "lv_indev_set_group must be guarded by LV_INDEV_TYPE_ENCODER");
        cursor += strlen("lv_indev_set_group");
    }

    free(text);
    free(path);
    return failures;
}

static int function_contains_all(const char *root, const char *relative, const char *function_marker,
                                 const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;
    const char *start;
    const char *end;

    failures += expect_true(text != NULL, relative);
    if (text == NULL) {
        free(path);
        return failures;
    }

    start = strstr(text, function_marker);
    failures += expect_true(start != NULL, function_marker);
    if (start != NULL) {
        end = strstr(start + strlen(function_marker), "\nstatic ");
        if (end == NULL) {
            end = text + strlen(text);
        }
        for (size_t i = 0; i < count; ++i) {
            failures +=
                expect_true(contains_ignoring_whitespace(start, (size_t)(end - start), patterns[i]), patterns[i]);
        }
    }

    free(text);
    free(path);
    return failures;
}

static int function_contains_none(const char *root, const char *relative, const char *function_marker,
                                  const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;
    const char *start;
    const char *end;

    failures += expect_true(text != NULL, relative);
    if (text == NULL) {
        free(path);
        return failures;
    }

    start = strstr(text, function_marker);
    failures += expect_true(start != NULL, function_marker);
    if (start != NULL) {
        end = strstr(start + strlen(function_marker), "\nstatic ");
        if (end == NULL) {
            end = text + strlen(text);
        }
        for (size_t i = 0; i < count; ++i) {
            failures +=
                expect_true(!contains_ignoring_whitespace(start, (size_t)(end - start), patterns[i]), patterns[i]);
        }
    }

    free(text);
    free(path);
    return failures;
}

static int patterns_in_order(const char *root, const char *relative, const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;
    char *normalized_text;
    const char *cursor;

    failures += expect_true(text != NULL, relative);
    if (text == NULL) {
        free(path);
        return failures;
    }

    normalized_text = remove_whitespace(text, strlen(text));
    failures += expect_true(normalized_text != NULL, "normalize ordered patterns source");
    cursor = normalized_text;
    for (size_t i = 0; i < count; ++i) {
        char *normalized_pattern = remove_whitespace(patterns[i], strlen(patterns[i]));
        const char *found = cursor != NULL && normalized_pattern != NULL ? strstr(cursor, normalized_pattern) : NULL;
        failures += expect_true(found != NULL, patterns[i]);
        if (found != NULL) {
            cursor = found + strlen(normalized_pattern);
        }
        free(normalized_pattern);
    }

    free(normalized_text);
    free(text);
    free(path);
    return failures;
}

static int function_patterns_in_order(const char *root, const char *relative, const char *function_marker,
                                      const char *const *patterns, size_t count) {
    int failures = 0;
    char *path = join_path(root, relative);
    char *text = path != NULL ? read_file(path) : NULL;
    char *normalized_function = NULL;
    const char *start;
    const char *end;
    const char *cursor;

    failures += expect_true(text != NULL, relative);
    if (text == NULL) {
        free(path);
        return failures;
    }

    start = strstr(text, function_marker);
    failures += expect_true(start != NULL, function_marker);
    if (start != NULL) {
        end = strstr(start + strlen(function_marker), "\nstatic ");
        if (end == NULL) {
            end = text + strlen(text);
        }
        normalized_function = remove_whitespace(start, (size_t)(end - start));
        failures += expect_true(normalized_function != NULL, "normalize ordered function source");
    }

    cursor = normalized_function;
    for (size_t i = 0; i < count; ++i) {
        char *normalized_pattern = remove_whitespace(patterns[i], strlen(patterns[i]));
        const char *found = cursor != NULL && normalized_pattern != NULL ? strstr(cursor, normalized_pattern) : NULL;
        failures += expect_true(found != NULL, patterns[i]);
        if (found != NULL) {
            cursor = found + strlen(normalized_pattern);
        }
        free(normalized_pattern);
    }

    free(normalized_function);
    free(text);
    free(path);
    return failures;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "../..";
    int failures = 0;
    const char *const touch_required[] = {
        "static void lvgl_port_touchpad_read",
        "esp_err_t read_result = esp_lcd_touch_read_data(touch_ctx->handle);",
        "if (read_result != ESP_OK)",
        "data->state = LV_INDEV_STATE_RELEASED;",
        "debug screen touch read failed",
        "esp_lcd_touch_get_coordinates(touch_ctx->handle",
        "touchpad_strength[0] > touch_ctx->sensitivity",
    };
    const char *const touch_bsp_init_required[] = {
        "static lv_indev_t *bsp_touch_indev_init(lv_disp_t *disp)",
        "i2c_param_config(BSP_TOUCH_I2C_NUM, &i2c_conf)",
        "i2c_driver_install(BSP_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, ESP_INTR_FLAG_SHARED)",
        "ESP_LCD_TOUCH_IO_I2C_SPD2010_CONFIG()",
        "esp_lcd_new_panel_io_i2c(BSP_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle)",
        "esp_lcd_touch_new_i2c_spd2010(tp_io_handle, &tp_cfg, &tp_handle)",
        "vTaskDelay(50 / portTICK_PERIOD_MS);",
        "esp_lcd_touch_read_data(tp_handle);",
        "vTaskDelay(100 / portTICK_PERIOD_MS);",
        ".sensitivity = CONFIG_LVGL_INPUT_DEVICE_SENSITIVITY",
        "return lvgl_port_add_touch(&touch);",
    };
    const char *const lvgl_task_required[] = {
        "static void lvgl_port_task(void *arg)",
        "uint32_t task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;",
        "lvgl_port_ctx.running = true;",
        "while (lvgl_port_ctx.running)",
        "if (lvgl_port_lock(0))",
        "task_delay_ms = lv_timer_handler();",
        "lvgl_port_unlock();",
        "if ((task_delay_ms > lvgl_port_ctx.task_max_sleep_ms) || (1 == task_delay_ms))",
        "task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;",
        "else if (task_delay_ms < 1)",
        "task_delay_ms = 1;",
        "vTaskDelay(pdMS_TO_TICKS(task_delay_ms));",
    };
    const char *const lvgl_flush_required[] = {
        "static bool lvgl_port_flush_ready_callback",
        "lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;",
        "lv_disp_flush_ready(disp_drv);",
        "static void lvgl_port_flush_callback",
        "esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1",
        "color_map);",
    };
    const char *const lvgl_direct_draw_wait_required[] = {
        "static esp_err_t lvgl_port_panel_draw_bitmap_locked",
        "lvgl_port_ctx.panel_direct_draw_pending = true;",
        "esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);",
        "xSemaphoreTake(lvgl_port_ctx.panel_trans_done, timeout_ticks)",
        "lvgl_port_ctx.panel_direct_draw_pending = false;",
    };
    const char *const forbidden_non_factory_touch_logic[] = {
        "LVGL_TOUCH_READ_FAIL_BACKOFF", "consecutive_failures", "suppress_until",
        "Touch read failed repeatedly", "click_guard_active",   "guard_clicks_after_transition",
    };
    const char *const forbidden_shell_touch_suppression[] = {
        "debug_touch_guard",
        "control_ingress_is_manual_touch_suppressed",
        "control_ingress_suppress_manual_touch_for_ms",
        "control_ingress_manual_touch_remaining_ms",
        "manual_touch",
    };
    const char *const home_gesture_required[] = {
        "static void page_home_event_cb",
        "lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP",
        "lv_indev_wait_release(lv_indev_get_act())",
        "lv_group_focus_next(s_group)",
        "lv_group_focus_prev(s_group)",
        "lv_obj_clear_flag(s_page_home, LV_OBJ_FLAG_GESTURE_BUBBLE);",
        "if (code == LV_EVENT_CLICKED)",
        "launcher_factory_home_click_focused();",
    };
    const char *const home_button_event_required[] = {
        "static void main_button_event_cb",
        "if (code == LV_EVENT_CLICKED)",
        "if (s_callbacks.on_clicked != NULL)",
        "s_callbacks.on_clicked(index, s_callbacks.user_ctx);",
        "if (code == LV_EVENT_FOCUSED)",
        "s_focused_index = index;",
        "update_title(index);",
        "update_checked_state();",
        "if (s_callbacks.on_focused != NULL)",
        "s_callbacks.on_focused(index, s_callbacks.user_ctx);",
    };
    const char *const home_scroll_required[] = {
        "static void main_scroll_cb",
        "int32_t radius = 245;",
        "cont_y_center = lv_area_get_height(&cont_area) / 2;",
        "diff_y = LV_ABS(diff_y);",
        "x = -radius;",
        "uint32_t x_sqr = radius * radius - diff_y * diff_y;",
        "lv_sqrt(x_sqr, &res, 0x8000);",
        "x = -(radius - res.i);",
        "lv_obj_set_style_translate_x(child, x, 0);",
        "lv_obj_set_style_opa(child, 250 + x + x, 0);",
        "lv_obj_set_scroll_snap_y(s_mainlist, LV_SCROLL_SNAP_CENTER);",
        "lv_obj_set_scroll_dir(s_mainlist, LV_DIR_VER);",
        "lv_obj_add_event_cb(s_mainlist, main_scroll_cb, LV_EVENT_SCROLL, NULL);",
        "lv_obj_scroll_to_view(lv_obj_get_child(s_mainlist, 0), LV_ANIM_OFF);",
        "lv_event_send(s_mainlist, LV_EVENT_SCROLL, NULL);",
        "LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_ONE",
        "lv_obj_add_flag(s_mcontrolp, LV_OBJ_FLAG_EVENT_BUBBLE);",
        "lv_obj_clear_flag(s_mcontrolp, LV_OBJ_FLAG_SCROLLABLE);",
        "lv_obj_scroll_to_view(lv_obj_get_parent(s_buttons[index]), animate ? LV_ANIM_ON : LV_ANIM_OFF);",
    };
    const char *const home_object_tree_required[] = {
        "lv_obj_set_width(s_mainlist, 130);",
        "lv_obj_set_height(s_mainlist, 399);",
        "lv_obj_set_x(s_mainlist, 140);",
        "lv_obj_set_y(s_mainlist, 0);",
        "lv_obj_set_align(s_mainlist, LV_ALIGN_CENTER);",
        "lv_obj_set_flex_flow(s_mainlist, LV_FLEX_FLOW_COLUMN);",
        "lv_obj_set_flex_align(s_mainlist, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);",
        "lv_obj_set_width(slot, 90);",
        "lv_obj_set_height(slot, 90);",
        "lv_obj_set_width(button, 80);",
        "lv_obj_set_height(button, 80);",
        "lv_obj_set_align(button, LV_ALIGN_CENTER);",
        "lv_obj_set_width(s_mcontrolp, 412);",
        "lv_obj_set_height(s_mcontrolp, 412);",
        "lv_obj_set_x(s_mcontrolp, 0);",
        "lv_obj_set_y(s_mcontrolp, 1);",
        "lv_obj_set_align(s_mcontrolp, LV_ALIGN_CENTER);",
    };
    const char *const home_event_binding_required[] = {
        "lv_obj_add_event_cb(button, main_button_event_cb, LV_EVENT_ALL, (void *)(intptr_t)index);",
        "lv_obj_add_event_cb(s_page_home, page_home_event_cb, LV_EVENT_ALL, NULL);",
        "lv_obj_add_flag(s_mcontrolp, LV_OBJ_FLAG_EVENT_BUBBLE);",
        "lv_obj_clear_flag(s_mcontrolp, LV_OBJ_FLAG_SCROLLABLE);",
        "transparent_panel(s_mcontrolp, 0x000000);",
    };
    const char *const home_wifi_status_icon_required[] = {
        "lv_img_set_src(s_mainwifi, wifi_connected ? &ui_img_wifi_3_png : &ui_img_no_wifi_png);",
        "lv_obj_set_style_img_recolor(s_mainwifi, lv_color_hex(wifi_connected ? 0xA9DE2C : 0x000000)",
    };
    const char *const settings_gesture_required[] = {
        "void ui_event_Page_Set",
        "lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP",
        "lv_indev_wait_release(lv_indev_get_act())",
        "sgesup_cb(event)",
        "sgesdown_cb(event)",
        "if (event_code == LV_EVENT_CLICKED)",
        "sclick_cb(event);",
    };
    const char *const settings_click_required[] = {
        "void sclick_cb",
        "factory_settings_ui_click_focused();",
    };
    const char *const settings_click_focused_required[] = {
        "void factory_settings_ui_click_focused(void)",
        "lv_obj_t *focused = s_group != NULL ? lv_group_get_focused(s_group) : NULL;",
        "if (focused == NULL)",
        "lv_event_send(focused, LV_EVENT_CLICKED, NULL);",
    };
    const char *const settings_visible_focus_events_required[] = {
        "void ui_event_setback", "setbackf_cb(event);",  "setbackdf_cb(event);",  "void ui_event_setdown",
        "setdownf_cb(event);",   "setdowndf_cb(event);", "void ui_event_setwifi", "setwifif_cb(event);",
        "setwifidf_cb(event);",  "void ui_event_setvol", "setvolf_cb(event);",    "setvoldf_cb(event);",
        "void ui_event_setbri",  "setbrif_cb(event);",   "setbridf_cb(event);",   "void ui_event_setrgb",
        "setrgbf_cb(event);",    "setrgbdf_cb(event);",  "void ui_event_setdev",  "setdevf_cb(event);",
        "setdevdf_cb(event);",   "void ui_event_setfac", "setfacf_cb(event);",    "setfacdf_cb(event);",
    };
    const char *const settings_visible_focus_style_required[] = {
        "set_obj_style_focused(ui_setback, ui_setbackt);", "set_obj_style_defocused(ui_setback, ui_setbackt);",
        "set_obj_style_focused(ui_setdown, ui_setdownt);", "set_obj_style_defocused(ui_setdown, ui_setdownt);",
        "set_obj_style_focused(ui_setwifi, ui_setwifit);", "set_obj_style_defocused(ui_setwifi, ui_setwifit);",
        "set_obj_style_focused(ui_setvol, ui_setvolt);",   "set_obj_style_defocused(ui_setvol, ui_setvolt);",
        "set_obj_style_focused(ui_setbri, ui_setbrit);",   "set_obj_style_defocused(ui_setbri, ui_setbrit);",
        "set_obj_style_focused(ui_setrgb, ui_setrgbt);",   "set_obj_style_defocused(ui_setrgb, ui_setrgbt);",
        "set_obj_style_focused(ui_setdev, ui_setdevt);",   "set_obj_style_defocused(ui_setdev, ui_setdevt);",
        "set_obj_style_focused(ui_setfac, ui_setfact);",   "set_obj_style_defocused(ui_setfac, ui_setfact);",
    };
    const char *const settings_rgb_toggle_debounce_required[] = {
        "#define SETTINGS_RGB_TOGGLE_DEBOUNCE_MS 500",
    };
    const char *const settings_rgb_toggle_required[] = {
        "static uint32_t s_last_rgb_toggle_ms = 0;",
        "uint32_t now_ms = lv_tick_get();",
        "lv_tick_elaps(s_last_rgb_toggle_ms) < SETTINGS_RGB_TOGGLE_DEBOUNCE_MS",
        "s_last_rgb_toggle_ms = now_ms;",
        "target_enabled = !s_state.rgb_enabled;",
        "set_rgb_switch(UI_CALLER, target_enabled ? 1 : 0)",
    };
    const char *const settings_slider_live_apply_required[] = {
        "void volvc_cb(lv_event_t *event)",
        "const int value = clamp_percent((int)volbri.vs_value, 0);",
        "set_sound(UI_CALLER, value);",
        "void brivc_cb(lv_event_t *event)",
        "const int value = clamp_percent((int)volbri.bs_value, 1);",
        "set_brightness(UI_CALLER, value);",
    };
    const char *const settings_slider_selection_colors_required[] = {
        "#define SETTINGS_SOUND_SELECTION_COLOR 0x8FC31F",
        "#define SETTINGS_BRIGHTNESS_SELECTION_COLOR 0x1F6DC3",
    };
    const char *const settings_slider_persistent_outer_selection_required[] = {
        "static void apply_slider_selection_style(factory_settings_slider_t kind)",
        "lv_obj_set_style_border_color(panel, lv_color_hex(selection_color), LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_obj_set_style_border_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_obj_set_style_outline_color(slider, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_EDITED);",
        "apply_slider_selection_style(FACTORY_SETTINGS_SLIDER_SOUND);",
        "apply_slider_selection_style(FACTORY_SETTINGS_SLIDER_BRIGHTNESS);",
        "apply_slider_selection_style(s_slider_kind);",
    };
    const char *const settings_slider_panel_focused_border_forbidden[] = {
        "lv_obj_set_style_border_color(ui_vp, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);",
        "lv_obj_set_style_border_opa(ui_vp, 255, LV_PART_MAIN | LV_STATE_FOCUSED);",
        "lv_obj_set_style_border_color(ui_bp, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);",
        "lv_obj_set_style_border_opa(ui_bp, 255, LV_PART_MAIN | LV_STATE_FOCUSED);",
    };
    const char *const settings_slider_default_second_layer_required[] = {
        "note_original_page_opened(SETTINGS_PAGE_SLIDER, &group_page_volume, 0, true, true);",
        "note_original_page_opened(SETTINGS_PAGE_SLIDER, &group_page_brightness, 0, true, true);",
    };
    const char *const settings_slider_touch_editing_required[] = {
        "void preserve_slider_editing_on_touch(lv_event_t *event)",
        "lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER",
        "if (event_code == LV_EVENT_PRESSED)",
        "s_restore_slider_editing_after_touch_focus = s_group != NULL && lv_group_get_editing(s_group);",
        "if (event_code == LV_EVENT_FOCUSED && s_restore_slider_editing_after_touch_focus && s_group != NULL)",
        "lv_group_set_editing(s_group, true);",
        "event_code == LV_EVENT_RELEASED || event_code == LV_EVENT_PRESS_LOST",
    };
    const char *const settings_slider_touch_event_binding_required[] = {
        "void ui_event_vslider(lv_event_t *event)",
        "preserve_slider_editing_on_touch(event);",
        "void ui_event_bslider(lv_event_t *event)",
    };
    const char *const settings_slider_defocus_must_not_hide_outer[] = {
        "lv_color_hex(0x000000)",
    };
    const char *const settings_group_order_required[] = {
        "lv_obj_t *set_objects[] = {ui_setback, ui_setdown, ui_setwifi, ui_setvol, ui_setbri,",
        "ui_setrgb,  ui_setdev,  ui_setfac};",
        "addObjToGroup(&group_page_set, set_objects, (uint8_t)(sizeof(set_objects) / sizeof(set_objects[0])));",
    };
    const char *const settings_lazy_page_init_required[] = {
        "void ensure_page_initialized",
        "init_fn();",
        "init_settings_groups();",
    };
    const char *const settings_entry_preload_forbidden[] = {
        "ensure_page_initialized(&ui_Page_Slider, ui_Page_Slider_screen_init);",
        "ensure_page_initialized(&ui_Page_About, ui_Page_About_screen_init);",
        "ensure_page_initialized(&ui_Page_Swipe, ui_Page_Swipe_screen_init);",
    };
    const char *const settings_entry_screen_handoff_required[] = {
        "static void lv_pm_open_page_internal",
        "bool auto_delete_previous_screen",
        "lv_scr_load_anim(*target, fademode, speed, delay, auto_delete_previous_screen);",
        "static void open_set_page_with_previous_screen_delete(bool auto_delete_previous_screen)",
        "void factory_settings_ui_build_with_previous_screen_delete",
        "open_set_page_with_previous_screen_delete(auto_delete_previous_screen);",
        "void open_set_page(void)",
        "open_set_page_with_previous_screen_delete(false);",
        "void factory_settings_ui_build",
        "factory_settings_ui_build_with_previous_screen_delete(screen, state, callbacks, true);",
    };
    const char *const settings_public_page_open_auto_delete_forbidden[] = {
        "lv_scr_load_anim(*target, fademode, speed, delay, true);",
        "open_set_page_with_previous_screen_delete(true);",
    };
    const char *const app_ui_screen_count_diag_required[] = {
        "static void app_ui_diag_log_lvgl_locked",
        "lv_disp_t *disp = lv_disp_get_default();",
        "evt=app_ui_diag_core stage=%s screen_count=%lu",
        "disp != NULL ? (unsigned long)disp->screen_cnt : 0UL",
    };
    const char *const launcher_screen_cache_required[] = {
        "static bool launcher_should_cache_screen_for_app",
        "app_id_is_client_voice_host(id) || strcmp(id, \"agent.app\") == 0",
        "strcmp(id, \"settings.app\") == 0",
        "static bool launcher_app_uses_behavior_display_ui",
        "app_id_is_client_voice_host(id) || strcmp(id, \"agent.app\") == 0",
        "static bool launcher_prepare_screen_cache_for_app",
        "cached && launcher_app_uses_behavior_display_ui(id)",
        "hal_display_retain_previous_screen_once(s_launcher_screen);",
        "s_launcher_screen_cached_for_foreground_app = cached;",
        "bool preserve_screen = s_launcher_screen_cached_for_foreground_app && s_launcher_screen != NULL;",
        "Launcher screen retained for foreground app",
        "lv_disp_load_scr(s_launcher_screen);",
        "lv_obj_del(old_active);",
        "bool retain_previous_screen = s_launcher_screen_cached_for_foreground_app && s_launcher_screen != NULL &&",
        "lv_disp_get_scr_act(NULL) == s_launcher_screen",
        "factory_settings_ui_build_with_previous_screen_delete(NULL, &state, &callbacks,",
        "!retain_previous_screen",
    };
    const char *const client_launcher_fusion_required[] = {
        "#define CLIENT_APP_ID \"client.app\"",
        "#define VOICE_APP_ID \"voice.app\"",
        "#define LAUNCHER_ENTRY_COUNT 5",
        "#define LAUNCHER_PHONE_CONTROL_FIRMWARE_ENTRY_COUNT 2",
        "{.id = CLIENT_APP_ID, .name = \"Desktop\\nLink\"",
        "{.title = \"Desktop\\nLink\"",
        ".name = \"Desktop Link\",",
    };
    const char *const old_launcher_dual_entry_forbidden[] = {
        "{.id = \"client.app\", .name = \"Current\\nTasks\"",
        "{.id = \"voice.app\", .name = \"Voice\"",
        "{.title = \"Current\\nTasks\"",
        "{.title = \"Voice\"",
        "{.id = CLIENT_APP_ID, .name = \"Client\"",
        "{.title = \"Client\"",
    };
    const char *const hal_display_retain_previous_screen_required[] = {
        "void hal_display_retain_previous_screen_once(void *screen)",
        "s_retained_previous_screen_once = (lv_obj_t *)screen;",
        "lv_obj_t *retained_screen = s_retained_previous_screen_once;",
        "s_retained_previous_screen_once = NULL;",
        "old_active_scr != retained_screen",
    };
    const char *const ui_ram_lifecycle_verifier_required[] = {
        "Verify launcher/voice/settings RAM lifecycle",
        "launcher_stable_3s",
        "voice_stable_3s",
        "settings_stable_3s",
        "enter drop=",
        "exit rise=",
        "launcher drift=",
        "launcher screen_count growth=",
        "--allow-missing-apps",
        "--self-test",
        "SELFTEST",
        "required_stages = {\"voice_stable_3s\", \"settings_stable_3s\"}",
        "missing required app lifecycle sample(s)",
        "Ignoring stale discovery result",
        "Discovery task still exiting",
        "transport lifecycle issue:",
        "APP_STABLE_STAGES",
        "missing next launcher_stable_3s before",
        "missing previous launcher_stable_3s after",
        "after_voice_transport_stop",
        "voice transport stop context",
        "voice_stopped",
        "voice transport stop delta=",
        "missing after_voice_transport_stop before launcher return",
    };
    const char *const settings_wifi_api_required[] = {
        "const char *wifi_ssid;",
        "const char *wifi_ip;",
        "bool wifi_connected;",
        "bool wifi_configured;",
        "const char *ble_mac;",
        "bool ble_connected;",
        "bool ble_advertising;",
        "const char *network_wait_title;",
        "const char *network_wait_hint;",
        "const char *network_connected_title;",
        "const char *network_connected_hint;",
        "bool network_use_ble_icon;",
        "uint32_t network_ble_mac_color;",
        "void (*on_wifi_detail_closed)(void *user_ctx);",
        "bool (*on_wifi_disconnect)(void *user_ctx);",
        "void factory_settings_ui_build_wifi_detail_with_previous_screen_delete",
        "void factory_settings_ui_build_network_page",
        "bool factory_settings_ui_open_wifi_detail(void);",
    };
    const char *const settings_network_page_required[] = {
        "static void update_network_page(void)",
        "lv_snprintf(mac_text, sizeof(mac_text), \"BLE MAC: %s\", ble_mac);",
        "lv_img_set_src(ui_wifiicon, &ui_img_ble_png);",
        "lv_obj_set_style_img_recolor(ui_wifiicon, lv_color_hex(ble_visual_color), LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_obj_set_style_img_recolor_opa(ui_wifiicon, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_img_set_src(ui_wifiicon, &ui_img_wifi_3_png);",
        "lv_obj_set_style_img_recolor_opa(ui_wifiicon, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "s_state.network_ble_mac_color != 0U",
        "Open the app and select this BLE MAC to configure Wi-Fi.",
        "\"Set up in App over Bluetooth\"",
        "lv_label_set_text(ui_wifissid, ble_connected ? network_connected_title : network_wait_title);",
        "Send Wi-Fi credentials from the app.",
        "lv_label_set_text(ui_wifibtnt, ble_connected ? network_connected_hint : network_wait_hint);",
        "Wi-Fi saved",
        "App connected. Wi-Fi will connect after Bluetooth disconnects.",
        "Disconnect clears saved Wi-Fi.",
        "lv_label_set_text(ui_wifimqtt, connecting ? \"Connecting\" : \"Offline\");",
        "static void network_disconnect_event_cb",
        "s_callbacks.on_wifi_disconnect(s_callbacks.user_ctx);",
        "void open_network_page(void)",
        "static void open_network_page_with_previous_screen_delete(bool auto_delete_previous_screen)",
        "bool factory_settings_ui_open_wifi_detail(void)",
        "return factory_settings_ui_open_wifi_detail();",
        "configure_network_group(disconnect_focusable);",
    };
    const char *const settings_network_disconnect_spacing_required[] = {
        "#define NETWORK_DISCONNECT_BUTTON_Y 54",
        "#define NETWORK_DISCONNECT_BUTTON_HEIGHT 42",
        "#define NETWORK_DISCONNECT_CANCEL_MIN_GAP 36",
        "#if NETWORK_DISCONNECT_CANCEL_GAP_PX < NETWORK_DISCONNECT_CANCEL_MIN_GAP",
        "lv_obj_set_height(ui_wifi_disconnect, NETWORK_DISCONNECT_BUTTON_HEIGHT);",
        "lv_obj_set_y(ui_wifi_disconnect, NETWORK_DISCONNECT_BUTTON_Y);",
        "lv_obj_set_y(ui_wifibtnt, configured ? 34 : 80);",
    };
    const char *const settings_wifi_event_required[] = {
        "void setwific_cb",
        "remember_set_focus(SETTINGS_SET_FOCUS_WIFI);",
        "s_callbacks.on_wifi(s_callbacks.user_ctx);",
        "open_network_page();",
        "void ui_event_wificancel",
        "s_callbacks.on_wifi_detail_closed(s_callbacks.user_ctx);",
    };
    const char *const settings_return_focus_required[] = {
        "void remember_set_focus(settings_set_focus_t focus)",
        "s_last_set_focus_index = (uint8_t)focus;",
        "load_page(&ui_Page_Set, ui_Page_Set_screen_init, SETTINGS_PAGE_SET, &group_page_set, PM_ADD_OBJS_TO_GROUP,",
        "s_last_set_focus_index, false, true);",
        "focus_set_row(s_last_set_focus_index, LV_ANIM_OFF);",
    };
    const char *const settings_secondary_entry_focus_required[] = {
        "remember_set_focus(SETTINGS_SET_FOCUS_WIFI);",       "remember_set_focus(SETTINGS_SET_FOCUS_SOUND);",
        "remember_set_focus(SETTINGS_SET_FOCUS_BRIGHTNESS);", "remember_set_focus(SETTINGS_SET_FOCUS_ABOUT);",
        "remember_set_focus(SETTINGS_SET_FOCUS_REBOOT);",     "remember_set_focus(SETTINGS_SET_FOCUS_FACTORY_RESET);",
    };
    const char *const settings_pm_group_required[] = {
        "static void lv_pm_obj_group",
        "lv_group_remove_all_objs(group);",
        "for (uint8_t index = 0; index < group_info->obj_count; ++index)",
        "lv_group_add_obj(group, group_info->group[index]);",
    };
    const char *const settings_pm_open_order_required[] = {
        "if (g_page_record.g_curfocused_obj != NULL)",
        "g_page_record.g_prefocused_obj = g_page_record.g_curfocused_obj;",
        "g_page_record.g_curfocused_obj = group != NULL ? lv_group_get_focused(group) : NULL;",
        "if (g_page_record.g_curpage != NULL)",
        "g_page_record.g_prepage = g_page_record.g_curpage;",
        "if (*target == NULL)",
        "target_init();",
        "g_page_record.g_curpage = *target;",
        "switch (operation)",
        "case PM_ADD_OBJS_TO_GROUP:",
        "lv_pm_obj_group(group, group_info);",
        "case PM_CLEAR_GROUP:",
        "lv_group_remove_all_objs(group);",
        "lv_group_focus_obj(g_page_record.g_prefocused_obj);",
        "lv_scr_load_anim(*target, fademode, speed, delay, auto_delete_previous_screen);",
    };
    const char *const launcher_group_order_required[] = {
        "for (int i = 0; i < (int)s_entry_count; ++i)",
        "lv_group_add_obj(*group, s_buttons[i]);",
        "launcher_factory_home_focus_index(focused_index, false);",
    };
    const char *const launcher_phone_control_required[] = {
        "#define PHONE_CONTROL_APP_ID \"phone.control.app\"",
        "#define LAUNCHER_ENTRY_COUNT 5",
        "{.id = PHONE_CONTROL_APP_ID, .name = \"Phone\\nControl\"",
        "{.title = \"Phone\\nControl\"",
        "static const watcher_app_t s_phone_control_app",
        ".id = PHONE_CONTROL_APP_ID,",
        ".name = \"Phone Control\",",
        ".resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,",
        ".on_open = phone_control_app_on_open,",
        "ESP_ERROR_CHECK(watcher_app_register(&s_phone_control_app));",
    };
    const char *const launcher_app_center_entry_forbidden[] = {
        "{.id = \"app.center\"",
        "{.title = \"App\\nCenter\"",
    };
    const char *const phone_control_runtime_required[] = {
        ".resources = WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_MCU_RUNTIME,",
        "requires_wifi = (resources & WATCHER_APP_RESOURCE_SET_WIFI_STA) != 0;",
        "app_resource_release_wifi_if_unused(app_id);",
        "requires_mcu_runtime = (resources & WATCHER_APP_RESOURCE_SET_MCU_RUNTIME) != 0;",
    };
    const char *const phone_control_firmware_exit_required[] = {
        "static volatile bool s_phone_control_firmware_reboot_to_launcher_pending = false;",
        "static bool phone_control_firmware_should_reboot_on_local_exit(void)",
        "return active_app_is(PHONE_CONTROL_APP_ID);",
        "static void phone_control_firmware_request_reboot_to_launcher_partition(void)",
        "s_phone_control_firmware_reboot_to_launcher_pending = true;",
        "static void phone_control_firmware_reboot_to_launcher_partition(void)",
        "s_phone_control_firmware_reboot_to_launcher_pending = false;",
        "Phone Control exit requested; rebooting to Launcher partition",
        "arm_firmware_app_return_to_launcher_on_next_boot(\"Phone Control exit\");",
        "esp_restart();",
        "static void phone_control_firmware_process_reboot_to_launcher_partition(void)",
        "if (phone_control_firmware_should_reboot_on_local_exit())",
        "phone_control_firmware_request_reboot_to_launcher_partition();",
        "phone_control_firmware_process_reboot_to_launcher_partition();",
    };
    const char *const phone_control_launcher_app_center_hidden_required[] = {
        "#define LAUNCHER_PHONE_CONTROL_FIRMWARE_ENTRY_COUNT 2",
        "static int launcher_visible_entry_count(void)",
        "return LAUNCHER_PHONE_CONTROL_FIRMWARE_ENTRY_COUNT;",
        "static bool launcher_entry_index_is_visible(int index)",
        "launcher_factory_home_build(s_launcher_screen, s_launcher_home_entries, "
        "(size_t)launcher_visible_entry_count(),",
        "static void app_center_release_after_launch_if_available(void)",
        "configure_app_package_transport();",
        "#if !defined(WATCHER_PHONE_CONTROL_FIRMWARE_APP)",
        "ESP_ERROR_CHECK(watcher_app_register(app_center_get_app()));",
        "ESP_ERROR_CHECK(watcher_app_register(&s_downloaded_app));",
        "#endif",
    };
    const char *const phone_control_waiting_ui_required[] = {
        "static void phone_control_fill_network_state",
        "state->wifi_configured = false;",
        "state->ble_connected = ble_service_is_connected();",
        "state->network_wait_title = \"Phone Control over Bluetooth\";",
        "state->network_wait_hint = \"Open the app and select this BLE MAC to control.\";",
        "state->network_connected_title = \"Phone connected\";",
        "state->network_connected_hint = \"Phone Control ready.\";",
        "state->network_use_ble_icon = true;",
        "state->network_ble_mac_color = 0x5AA2FF;",
        "static void phone_control_show_waiting_network_ui",
        "factory_settings_ui_build_network_page(NULL, &state, &callbacks, &s_phone_control_group, true);",
        "static void phone_control_app_on_open",
        "display_ui_set_text_suppressed(true);",
        "phone_control_show_connected_sleep_ui(\"phone control open connected\")",
        "phone_control_show_waiting_network_ui();",
    };
    const char *const launcher_dedicated_icon_required[] = {
        "&ui_img_phone_control_png",      "&ui_img_phone_control_focused_png", "&ui_img_python_sdk_png",
        "&ui_img_python_sdk_focused_png", "&ui_img_agent_robot_png",           "&ui_img_agent_robot_focused_png",
    };
    const char *const launcher_icon_declaration_required[] = {
        "LV_IMG_DECLARE(ui_img_phone_control_png);", "LV_IMG_DECLARE(ui_img_phone_control_focused_png);",
        "LV_IMG_DECLARE(ui_img_python_sdk_png);",    "LV_IMG_DECLARE(ui_img_python_sdk_focused_png);",
        "LV_IMG_DECLARE(ui_img_agent_robot_png);",   "LV_IMG_DECLARE(ui_img_agent_robot_focused_png);",
    };
    const char *const launcher_icon_build_required[] = {
        "factory_home_ui/images/ui_img_phone_control_png.c",
        "factory_home_ui/images/ui_img_phone_control_focused_png.c",
        "factory_home_ui/images/ui_img_python_sdk_png.c",
        "factory_home_ui/images/ui_img_python_sdk_focused_png.c",
        "factory_home_ui/images/ui_img_agent_robot_png.c",
        "factory_home_ui/images/ui_img_agent_robot_focused_png.c",
    };
    const char *const launcher_legacy_extension_build_forbidden[] = {
        "factory_home_ui/images/ui_img_extension_png.c",
        "LV_IMG_DECLARE(ui_img_extension_png);",
    };
    const char *const launcher_legacy_extension_binding_forbidden[] = {
        "&ui_img_extension_png",
    };
    const char *const phone_control_connected_sleep_required[] = {
        "static void phone_control_show_connected_sleep_ui",
        "factory_settings_ui_reset();",
        "display_ui_set_text_suppressed(true);",
        "local_behavior_ui_open_ex(\"standby_entry\", \"\", NULL, \"\", true);",
        "behavior_state_set_with_resources(\"standby_entry\", \"\", 0, NULL, \"\")",
        "static void phone_control_app_on_tick",
        "phone_control_show_connected_sleep_ui(\"phone control ble connected\")",
        "phone_control_show_waiting_network_ui();",
        ".on_tick = phone_control_app_on_tick,",
        ".on_close = phone_control_app_on_close,",
    };
    const char *const phone_control_text_suppression_header_required[] = {
        "void display_ui_set_text_suppressed(bool suppressed);",
    };
    const char *const phone_control_text_suppression_display_required[] = {
        "static bool g_text_suppressed = false;",
        "g_text_suppressed = false;",
        "void display_ui_set_text_suppressed(bool suppressed)",
        "text_changed = (!g_text_suppressed && text != NULL",
    };
    const char *const phone_control_text_suppression_app_required[] = {
        "display_ui_set_text_suppressed(false);",
    };
    const char *const phone_control_connected_sleep_forbidden[] = {
        "hal_display_set_text_with_style(\"\", 0, false);",
    };
    const char *const phone_control_no_ble_feedback_required[] = {
        "s_ble_connected_feedback_pending = connected && (active_app_is(\"ble.app\") || "
        "active_app_is(\"provision.app\"));",
    };
    const char *const factory_settings_network_page_api_required[] = {
        "void factory_settings_ui_build_network_page(lv_obj_t *screen,",
        "factory_settings_ui_build_network_page",
        "lv_pm_open_page_internal(s_group, &group_page_network, PM_ADD_OBJS_TO_GROUP, &ui_Page_Network",
    };
    const char *const app_center_phone_control_local_required[] = {
        "static bool app_center_catalog_item_is_local_app",
        "\"phone.control.app\"",
        "\"phone-control\"",
        "\"Phone Control\"",
        "\"agent.app\"",
    };
    const char *const espnow_remote_runtime_forbidden[] = {
        "pot_remote_service",
        "servo_relay_service",
        "WATCHER_POT_REMOTE_BUILD",
        "WATCHER_SERVO_RELAY_BUILD",
        "WATCHER_APP_RESOURCE_ESPNOW",
        "run_espnow_remote_rx_firmware",
        "servo_relay_",
        "s_remote_app",
        "\"remote.app\"",
        "\"ESP-NOW Remote\"",
    };
    const char *const espnow_remote_cmake_forbidden[] = {
        "pot_remote_service",     "servo_relay_service",      "POT_REMOTE_BUILD_ROLE",
        "SERVO_RELAY_BUILD_ROLE", "WATCHER_POT_REMOTE_BUILD", "WATCHER_SERVO_RELAY_BUILD",
    };
    const char *const espnow_low_level_api_forbidden[] = {
        "#include \"esp_now.h\"",
        "esp_now_",
        "ESP_NOW_ETH_ALEN",
        "espnow=%d",
    };
    const char *const espnow_relay_target_forbidden[] = {
        "MCU_MOTION_MSG_SERVO_RELAY_TARGET",
        "mcu_motion_relay_target_t",
        "mcu_motion_submit_relay_target",
        "SERVO_RELAY_TARGET",
    };
    const char *const launcher_factory_dynamic_entry_required[] = {
        "#define LAUNCHER_FACTORY_HOME_ENTRY_COUNT 5",
        "size_t entry_count",
        "static size_t s_entry_count = 0;",
        "static bool entry_index_is_visible(int index)",
        "static lv_obj_t *s_slots[LAUNCHER_FACTORY_HOME_ENTRY_COUNT] = {NULL};",
        "s_entry_count = entry_count > LAUNCHER_FACTORY_HOME_ENTRY_COUNT ? LAUNCHER_FACTORY_HOME_ENTRY_COUNT : "
        "entry_count;",
        "for (int i = 0; i < (int)s_entry_count; ++i)",
        "s_slots[i] = create_slot(s_mainlist, i);",
        "s_buttons[i] = create_button(s_slots[i], i);",
        "s_entry_count = 0;",
        "memset(s_slots, 0, sizeof(s_slots));",
    };
    const char *const launcher_factory_four_button_forbidden[] = {
        "s_mainbtn1", "s_mainbtn2", "s_mainbtn3", "s_mainbtn4", "s_mainlp1", "s_mainlp2", "s_mainlp3", "s_mainlp4",
    };
    const char *const settings_scroll_required[] = {
        "static void settings_set_scroll_cb",
        "SETTINGS_SET_SCROLL_RADIUS",
        "s_set_scroll_center_y",
        "for (uint8_t i = 0; i < group_page_set.obj_count; ++i)",
        "lv_obj_t *child = group_page_set.group[i];",
        "lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)",
        "diff_y = LV_ABS(child_y_center - s_set_scroll_center_y);",
        "factory_settings_scroll_curve_project(&s_set_scroll_curve, diff_y, &x)",
        "lv_obj_get_style_translate_x(child, LV_PART_MAIN) == x",
        "lv_obj_set_style_translate_x(child, x, 0);",
        "factory_settings_scroll_curve_init(&s_set_scroll_curve, settings_scroll_sqrt_integer)",
        "lv_obj_set_scroll_snap_y(ui_Set_panel, LV_SCROLL_SNAP_CENTER);",
        "lv_obj_set_scroll_dir(ui_Set_panel, LV_DIR_VER);",
        "lv_obj_add_event_cb(ui_Set_panel, settings_set_scroll_cb, LV_EVENT_SCROLL, NULL);",
        "lv_obj_scroll_to_view(lv_obj_get_child(ui_Set_panel, 0), LV_ANIM_OFF);",
        "lv_event_send(ui_Set_panel, LV_EVENT_SCROLL, NULL);",
        "lv_obj_scroll_to_view(obj, anim);",
        "settings_enable_set_scroll_anim();",
    };
    const char *const settings_scroll_callback_forbidden[] = {
        "lv_obj_get_child_cnt",
        "lv_obj_get_child(cont",
        "lv_obj_get_coords(cont",
        "lv_sqrt(",
    };
    const char *const settings_redundant_black_fill_required[] = {
        "static bool settings_has_opaque_black_canvas",
        "lv_color_to32(lv_obj_get_style_bg_color(ui_Page_Set, LV_PART_MAIN)) == 0xFF000000U",
        "lv_obj_get_style_bg_opa(ui_Page_Set, LV_PART_MAIN) == LV_OPA_COVER",
        "lv_obj_get_style_bg_opa(ui_Set_panel, LV_PART_MAIN) == LV_OPA_TRANSP",
        "s_set_defocused_fill_is_redundant = settings_has_opaque_black_canvas();",
        "s_set_defocused_fill_is_redundant ? LV_OPA_TRANSP : 120",
        "s_set_defocused_fill_is_redundant ? LV_OPA_TRANSP : LV_OPA_COVER",
    };
    const char *const settings_focused_fill_required[] = {
        "lv_obj_set_style_bg_opa(obj, 120, LV_PART_MAIN | LV_STATE_DEFAULT);",
    };
    const char *const settings_focused_background_cache_required[] = {
        "static bool settings_focus_background_cache_build(void)",
        "lv_snapshot_take(ui_setback, LV_IMG_CF_TRUE_COLOR)",
        "lv_snapshot_take(ui_setdown, LV_IMG_CF_TRUE_COLOR)",
        "lv_obj_set_style_bg_img_src(obj, background, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_obj_set_style_bg_img_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);",
        "static void settings_focus_background_cache_release(void)",
        "if (ui_Page_Set != NULL &&",
        "lv_snapshot_free(s_set_focus_background_cache.back);",
        "lv_snapshot_free(s_set_focus_background_cache.row);",
        "settings_focus_background_cache_release();",
    };
    const char *const settings_object_tree_required[] = {
        "lv_obj_set_width(ui_Set_panel, 412);",
        "lv_obj_set_height(ui_Set_panel, 412);",
        "lv_obj_set_align(ui_Set_panel, LV_ALIGN_CENTER);",
        "lv_obj_set_flex_flow(ui_Set_panel, LV_FLEX_FLOW_COLUMN);",
        "lv_obj_set_flex_align(ui_Set_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);",
        "lv_obj_add_flag(ui_Set_panel, LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_ONE);",
        "lv_obj_set_scrollbar_mode(ui_Set_panel, LV_SCROLLBAR_MODE_OFF);",
        "lv_obj_set_scroll_dir(ui_Set_panel, LV_DIR_VER);",
        "lv_obj_set_height(ui_scontrolp, 412);",
        "lv_obj_set_align(ui_scontrolp, LV_ALIGN_CENTER);",
        "lv_obj_add_flag(ui_scontrolp, LV_OBJ_FLAG_EVENT_BUBBLE);",
        "lv_obj_clear_flag(ui_scontrolp, LV_OBJ_FLAG_SCROLLABLE);",
    };
    const char *const settings_hidden_entries_required[] = {
        "lv_obj_add_flag(ui_setapp, LV_OBJ_FLAG_HIDDEN);",   "lv_obj_add_flag(ui_setble, LV_OBJ_FLAG_HIDDEN);",
        "lv_obj_add_flag(ui_setblesw, LV_OBJ_FLAG_HIDDEN);", "lv_obj_add_flag(ui_setww, LV_OBJ_FLAG_HIDDEN);",
        "lv_obj_add_flag(ui_settime, LV_OBJ_FLAG_HIDDEN);",
    };
    const char *const settings_removed_focus_objects[] = {
        "return ui_setapp;", "return ui_setappt;", "return ui_setble;",  "return ui_setblet;",  "return ui_setblesw;",
        "return ui_setww;",  "return ui_setwwt;",  "return ui_settime;", "return ui_settimet;",
    };
    const char *const shell_open_required[] = {
        "app_animation_ui_deinit();",
    };
    const char *const input_router_app_required[] = {
        "static watcher_input_scope_t current_input_scope(void *user_ctx)",
        ".context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY",
        "!s_shutdown_in_progress && active_app != NULL",
        "active_app->get_input_context != NULL ? active_app->get_input_context()",
        "app_context == WATCHER_INPUT_CONTEXT_UNSPECIFIED ? WATCHER_INPUT_CONTEXT_SYSTEM_ONLY",
        "scope.owner_token = (uintptr_t)active_app;",
        "watcher_input_router_global_set_scope_provider(current_input_scope, NULL);",
        "watcher_input_router_global_consume_app_click()",
        "const esp_err_t callback_ret =",
        "bsp_set_btn_long_press_ms_cb(BUTTON_SHUTDOWN_HOLD_MS, on_button_long_hold_source);",
        "if (callback_ret == ESP_OK)",
        "Failed to register shutdown long-press callback",
        "static void power_button_shutdown_fallback_task(void *arg)",
        "button_shutdown_hold_detector_init(&detector",
        "button_shutdown_hold_detector_update(&detector, raw_pressed, POWER_BUTTON_FALLBACK_POLL_MS)",
        "Power-button shutdown fallback monitor started; short-click routing remains disabled",
        "static void start_power_button_shutdown_fallback(const char *reason)",
        "start_power_button_shutdown_fallback(\"BSP long-press callback registration failed\");",
        "start_power_button_shutdown_fallback(\"LVGL knob input unavailable\");",
    };
    const char *const input_context_declarations_required[] = {
        ".input_context = WATCHER_INPUT_CONTEXT_LVGL_NAV,",
        ".input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,",
        ".input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,",
        ".get_input_context = phone_control_input_context,",
    };
    const char *const input_router_port_required[] = {
        "watcher_input_router_global_on_rotate(diff)",
        "result.owner == WATCHER_INPUT_OWNER_LVGL ? diff : 0",
        "watcher_input_router_global_lvgl_button_pressed(&cancelled)",
        "watcher_input_router_global_on_button_down",
        "watcher_input_router_global_on_button_up",
        "lv_indev_reset(ctx->indev, NULL);",
    };
    const char *const legacy_duplicate_button_paths_forbidden[] = {
        "request_app_button_single_click",   "s_settings_pending_click",         "settings_on_button",
        ".on_button = launcher_on_button",   "s_launcher_pending_focused_click", "SETTINGS_BUTTON_ENTRY_GUARD_MS",
        "SETTINGS_BUTTON_CLICK_DEBOUNCE_MS",
    };
    const char *const settings_wifi_state_storage_required[] = {
        "static char s_settings_wifi_ssid_text[64] = \"-\";",
        "static char s_settings_wifi_ip_text[24] = \"-\";",
        "static volatile bool s_settings_pending_wifi_detail_start = false;",
        "static volatile bool s_settings_pending_ble_switch = false;",
        "static bool s_settings_pending_ble_switch_enabled = false;",
    };
    const char *const settings_wifi_state_required[] = {
        "bool wifi_configured = false;",
        "bool wifi_connected = false;",
        "wifi_status_t wifi_status = WIFI_STATUS_DISCONNECTED;",
        "wifi_configured = wifi_has_credentials() == 1;",
        "wifi_status = wifi_get_status();",
        "wifi_connected = wifi_status == WIFI_STATUS_CONNECTED || wifi_is_connected() == 1;",
        "ble_connected = ble_service_is_connected();",
        "ble_advertising = ble_service_is_advertising_enabled();",
        "wifi_get_saved_ssid(s_settings_wifi_ssid_text, sizeof(s_settings_wifi_ssid_text))",
        "wifi_get_ip_addr(s_settings_wifi_ip_text, sizeof(s_settings_wifi_ip_text))",
        "state->wifi_ssid = s_settings_wifi_ssid_text;",
        "state->wifi_ip = s_settings_wifi_ip_text;",
        "state->wifi_connected = wifi_connected;",
        "state->wifi_configured = wifi_configured;",
        "state->ble_mac = s_settings_ble_mac_text;",
        "state->ble_connected = ble_connected;",
        "state->ble_advertising = ble_advertising;",
    };
    const char *const settings_wifi_disconnect_required[] = {
        "static bool settings_on_wifi_disconnect(void *user_ctx)",
        "s_settings_pending_wifi_disconnect = true;",
        "Settings Wi-Fi disconnect requested",
        "return true;",
    };
    const char *const settings_wifi_disconnect_apply_required[] = {
        "static void settings_perform_wifi_disconnect(void)",
        "wifi_disconnect();",
        "wifi_clear_credentials()",
        "settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS, \"wifi credentials "
        "cleared\")",
        "settings_fill_state(&state);",
        "lvgl_port_lock(0);",
        "factory_settings_ui_update_state(&state);",
        "lvgl_port_unlock();",
        "Settings Wi-Fi disconnected and credentials cleared",
    };
    const char *const settings_wifi_disconnect_tick_required[] = {
        "if (s_settings_pending_wifi_disconnect)",
        "s_settings_pending_wifi_disconnect = false;",
        "settings_perform_wifi_disconnect();",
    };
    const char *const settings_wifi_resume_required[] = {
        "static void settings_resume_wifi_if_needed(void)",
        "ble_service_is_connected()",
        "wifi_has_credentials() != 1",
        "status == WIFI_STATUS_CONNECTED",
        "status == WIFI_STATUS_CONNECTING",
        "wifi_is_connect_requested() == 1",
        "wifi_resume_background()",
        "Settings Wi-Fi detail is resuming saved Wi-Fi connection",
    };
    const char *const settings_wifi_ble_scheduling_required[] = {
        "#define SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS 350U",
        "#define SETTINGS_WIFI_DETAIL_BLE_RETRY_MS 750U",
        "#define SETTINGS_LCD_SPI_TRANSFER_BYTES",
        "#define SETTINGS_WIFI_BLE_MIN_DMA_LARGEST_BYTES",
        "static bool settings_wifi_ble_start_has_headroom(void)",
        "Deferring Settings Wi-Fi BLE provisioning due to low heap",
    };
    const char *const settings_wifi_ble_provisioning_required[] = {
        "static bool settings_start_ble_provisioning_if_needed(void)",
        "wifi_has_credentials() == 1",
        "ble_service_is_connected()",
        "ble_service_is_advertising_enabled()",
        "ble_service_is_advertising_active()",
        "app_resource_start_ble()",
        "s_settings_wifi_ble_provisioning_active = true;",
        "Settings Wi-Fi detail started BLE provisioning advertising",
        "Settings Wi-Fi detail re-armed pending BLE provisioning advertising",
        "settings_lcd_dma_transfer_has_floor()",
        "settings_stop_wifi_detail_provisioning_if_owned(\"lcd dma floor\")",
    };
    const char *const settings_wifi_open_credentials_required[] = {
        "static void settings_on_wifi(void *user_ctx)",
        "if (wifi_has_credentials() == 1)",
        "Settings Wi-Fi detail keeps BLE provisioning off because Wi-Fi credentials already exist",
    };
    const char *const settings_wifi_ble_provisioning_forbidden[] = {
        "Settings Wi-Fi detail keeps BLE provisioning off because Wi-Fi credentials already exist",
    };
    const char *const ble_wifi_json_connect_required[] = {
        "strcmp(message_type, \"cfg.wifi.set\") == 0",
        "wifi_provision(ssid, password)",
        "BLE WiFi provisioning JSON received for SSID",
        "ble_notify_wifi_configured(BLE_SERVICE_WIFI_CONFIG_EVENT_SAVED);",
    };
    const char *const ble_wifi_json_store_forbidden[] = {
        "s_connected ? wifi_store_credentials(ssid, password) : wifi_provision(ssid, password)",
    };
    const char *const ble_wifi_legacy_connect_required[] = {
        "static esp_err_t ble_parse_wifi_config",
        "wifi_provision(ssid->valuestring, password->valuestring)",
        "WIFI_CONNECTING",
    };
    const char *const ble_wifi_legacy_store_forbidden[] = {
        "s_connected\n                  ? wifi_store_credentials(ssid->valuestring, password->valuestring)\n           "
        "       : wifi_provision(ssid->valuestring, password->valuestring)",
    };
    const char *const ble_wifi_config_callback_api_required[] = {
        "BLE_SERVICE_WIFI_CONFIG_EVENT_SAVED",
        "BLE_SERVICE_WIFI_CONFIG_EVENT_CLEARED",
        "typedef void (*ble_service_wifi_config_callback_t)(ble_service_wifi_config_event_t event);",
        "void ble_service_register_wifi_config_callback(ble_service_wifi_config_callback_t cb);",
        "bool ble_service_is_advertising_active(void);",
    };
    const char *const ble_wifi_config_callback_required[] = {
        "static ble_service_wifi_config_callback_t s_wifi_config_cb = NULL;",
        "static void ble_notify_wifi_configured(ble_service_wifi_config_event_t event)",
        "s_wifi_config_cb(event);",
        "ble_notify_wifi_configured(BLE_SERVICE_WIFI_CONFIG_EVENT_SAVED);",
        "ble_notify_wifi_configured(BLE_SERVICE_WIFI_CONFIG_EVENT_CLEARED);",
        "void ble_service_register_wifi_config_callback(ble_service_wifi_config_callback_t cb)",
        "s_wifi_config_cb = cb;",
    };
    const char *const ble_light_control_required[] = {
        "#include \"mcu_led_service.h\"",
        "strcmp(message_type, \"ctrl.light.set\") == 0",
        "static bool ble_light_zone_from_json(cJSON *data, mcu_led_zone_t *out_zone)",
        "{\"zone\", \"area\", \"region\", \"target\"}",
        "ble_light_zone_from_number(item->valueint, out_zone)",
        "ble_light_zone_from_json(data, &req.zone)",
        "req.effect_id = MCU_LED_EFFECT_BREATHING;",
        "ret = mcu_led_submit(&req);",
        "\"mcu_link_not_ready\"",
        "\"invalid_light_zone\"",
        "\"invalid_light_mode\"",
        "\"invalid_light_effect\"",
    };
    const char *const ble_light_component_required[] = {
        "\"mcu_led_service\"",
    };
    const char *const settings_callback_refresh_required[] = {
        "static volatile bool s_settings_pending_state_refresh = false;",
        "static void settings_request_state_refresh(void)",
        "active_app_is(\"settings.app\")",
        "s_settings_pending_state_refresh = true;",
        "static void settings_on_ble_wifi_configured(ble_service_wifi_config_event_t event)",
        "BLE_SERVICE_WIFI_CONFIG_EVENT_SAVED",
        "PROVISIONING_FEEDBACK_SOUND_WIFI_SAVED",
        "settings_request_state_refresh();",
        "ble_service_register_wifi_config_callback(settings_on_ble_wifi_configured);",
    };
    const char *const ble_provisioning_release_after_saved_required[] = {
        "static volatile bool s_ble_provisioning_release_pending = false;",
        "static void ble_provisioning_schedule_release(const char *reason)",
        "BLE_PROVISIONING_RELEASE_DELAY_MS",
        "s_ble_provisioning_release_pending = true;",
        "static void ble_provisioning_release_tick(void)",
        "app_resource_stop_ble();",
        "ble_provisioning_schedule_release(\"wifi saved\")",
        "ble_provisioning_release_tick();",
    };
    const char *const settings_ble_connection_refresh_required[] = {
        "static void on_ble_connection_event(bool connected)",
        "settings_request_state_refresh();",
        "PROVISIONING_FEEDBACK_SOUND_BLE_CONNECTED",
        "PROVISIONING_FEEDBACK_SOUND_BLE_DISCONNECTED",
    };
    const char *const settings_wifi_status_refresh_required[] = {
        "static void on_wifi_status_changed",
        "provisioning_feedback_note_wifi_status(status);",
        "settings_request_state_refresh();",
    };
    const char *const wifi_time_sync_required[] = {
        "#include \"time_sync_service.h\"",
        "time_sync_service_init();",
        "time_sync_service_register_callback(on_time_synchronized);",
        "time_sync_service_start_on_network();",
        "launcher_request_status_refresh();",
    };
    const char *const launcher_resource_required[] = {
        "static const watcher_app_t s_launcher_app",
        ".resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,",
    };
    const char *const settings_resource_required[] = {
        "static const watcher_app_t s_settings_app",
        ".resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,",
    };
    const char *const boot_wifi_resume_required[] = {
        "static void boot_resume_saved_wifi_if_needed(void)",
        "#define BOOT_WIFI_CONNECT_TIMEOUT_MS 10000U",
        "#define BOOT_TIME_SYNC_TIMEOUT_MS 5000U",
        "static void boot_prepare_saved_wifi_before_launcher(void)",
        "wifi_has_credentials() != 1",
        "ble_service_is_connected()",
        "wifi_resume_background()",
        "Boot resuming saved Wi-Fi connection",
        "wifi_wait_for_connection((int)BOOT_WIFI_CONNECT_TIMEOUT_MS)",
        "s_boot_saved_wifi_unavailable = true;",
        "wifi_disconnect();",
        "boot_wait_for_time_sync(BOOT_TIME_SYNC_TIMEOUT_MS)",
        "boot_prepare_saved_wifi_before_launcher();",
    };
    const char *const boot_wifi_resume_order_required[] = {
        "wifi_register_status_callback(on_wifi_status_changed);",
        "settings_load_config();",
        "boot_prepare_saved_wifi_before_launcher();",
        "LOG_HEAP_STATE(\"after_wifi_ready\");",
        "ESP_ERROR_CHECK(watcher_app_open(\"launcher\"));",
    };
    const char *const boot_power_monitor_prime_required[] = {
        "static void boot_prime_power_monitor_for_launcher(void)",
        "power_monitor_service_tick(true)",
        "launcher_request_status_refresh();",
        "boot_prime_power_monitor_for_launcher();",
    };
    const char *const boot_power_monitor_prime_order_required[] = {
        "power_monitor_service_init()",
        "power_monitor_service_set_behavior_gate(power_monitor_behavior_gate, NULL);",
        "boot_prime_power_monitor_for_launcher();",
        "ESP_ERROR_CHECK(watcher_app_open(\"launcher\"));",
    };
    const char *const time_sync_component_required[] = {
        "\"components/utils/time_sync_service\"",
    };
    const char *const time_sync_service_required[] = {
        "#include \"esp_sntp.h\"",
        "time_sync_service_sync_callback_t s_sync_cb = NULL;",
        "void time_sync_service_register_callback(time_sync_service_sync_callback_t cb)",
        "setenv(\"TZ\", CONFIG_WATCHER_TIME_SYNC_DEFAULT_TZ, 1)",
        "tzset();",
        "esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);",
        "esp_sntp_setservername(0, CONFIG_WATCHER_TIME_SYNC_NTP_SERVER_1);",
        "esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);",
        "esp_sntp_init();",
    };
    const char *const wifi_resume_idempotent_required[] = {
        "int wifi_resume_background(void)",
        "if (is_connected || s_connect_requested || s_connection_in_progress)",
        "WiFi background resume skipped because connection is already active",
        "return 0;",
    };
    const char *const app_resource_wifi_resume_guard_required[] = {
        "static esp_err_t app_resource_resume_wifi_if_needed(const char *app_id)",
        "const bool settings_app = app_id != NULL && strcmp(app_id, \"settings.app\") == 0;",
        "wifi_has_credentials() != 1",
        "if (s_boot_saved_wifi_unavailable && !settings_app)",
        "wifi_status = wifi_get_status();",
        "wifi_is_connected() == 1",
        "wifi_is_connect_requested() == 1",
        "wifi_status == WIFI_STATUS_CONNECTED",
        "wifi_status == WIFI_STATUS_CONNECTING",
        "wifi_resume_background()",
        "ret = app_resource_resume_wifi_if_needed(app_id);",
    };
    const char *const provisioning_feedback_required[] = {
        "PROVISIONING_FEEDBACK_SOUND_ID_BLE_CONNECTED",
        "PROVISIONING_FEEDBACK_SOUND_ID_WIFI_SAVED",
        "PROVISIONING_FEEDBACK_SOUND_ID_WIFI_CONNECTED",
        "PROVISIONING_FEEDBACK_SOUND_ID_WIFI_FAILED",
        "static void provisioning_feedback_queue_sound(provisioning_feedback_sound_t sound)",
        "static void provisioning_feedback_tick(void)",
        "sfx_service_play(sound_id)",
        "provisioning_feedback_tick();",
    };
    const char *const transport_cloud_state_labels_required[] = {
        "return \"WIFI_NO_CREDENTIALS\";", "return \"WIFI_STARTING\";", "return \"WIFI_CONNECTING\";",
        "return \"CLOUD_DISCOVERING\";",   "return \"WS_CONNECTING\";", "return \"CLOUD_READY\";",
        "return \"CLOUD_SUSPENDED\";",
    };
    const char *const transport_ble_idle_state_labels_forbidden[] = {
        "return \"BLE_IDLE_NO_CREDENTIALS\";",  "return \"BLE_IDLE_WIFI_STARTING\";",
        "return \"BLE_IDLE_WIFI_CONNECTING\";", "return \"BLE_IDLE_DISCOVERING\";",
        "return \"BLE_IDLE_WS_CONNECTING\";",   "return \"BLE_IDLE_CLOUD_READY\";",
        "return \"BLE_IDLE_CLOUD_SUSPENDED\";",
    };
    const char *const cloud_discovery_retry_backoff_required[] = {
        "#define CLOUD_DISCOVERY_RETRY_INITIAL_DELAY_MS 5000",
        "#define CLOUD_DISCOVERY_RETRY_STEP_MS 5000",
        "#define CLOUD_DISCOVERY_RETRY_MAX_DELAY_MS 30000",
        "static uint32_t transport_note_discovery_failure_and_get_retry_delay(void)",
        "s_consecutive_discovery_failures++",
        "transport_reset_discovery_retry_backoff(\"wifi connected\")",
        "transport_reset_discovery_retry_backoff(\"discovery ready\")",
        "transport_note_discovery_failure_and_get_retry_delay()",
        "transport_schedule_retry(retry_delay_ms)",
        "transport_set_state(TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED, \"wifi connected\")",
    };
    const char *const wifi_open_network_required[] = {
        "sta_cfg->threshold.authmode = pass_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;",
    };
    const char *const wifi_manager_provisioning_init_required[] = {
        "static int wifi_ensure_initialized(const char *source)",
        "if (s_initialized)",
        "wifi_init()",
        "WiFi init failed before %s",
    };
    const char *const wifi_provision_init_required[] = {
        "wifi_ensure_initialized(\"wifi_provision\")",
    };
    const char *const wifi_store_init_required[] = {
        "wifi_ensure_initialized(\"wifi_store_credentials\")",
    };
    const char *const wifi_clear_init_required[] = {
        "wifi_ensure_initialized(\"wifi_clear_credentials\")",
    };
    const char *const ble_write_boundary_required[] = {
        "BLE write ignored: unexpected handle=",
        "BLE prepare write is not supported",
        "ESP_GATT_WRITE_NOT_PERMIT",
    };
    const char *const ble_disconnect_restart_advertising_required[] = {
        "static void ble_mark_connectable_advertising_stopped(void)",
        "s_advertising_active = false;",
        "s_advertising_start_pending = false;",
        "bool ble_service_is_advertising_active(void)",
        "return s_advertising_active || s_advertising_start_pending;",
        "case ESP_GATTS_CONNECT_EVT:",
        "ble_mark_connectable_advertising_stopped();",
        "case ESP_GATTS_DISCONNECT_EVT:",
        "if (s_advertising_requested)",
        "ble_start_advertising_now();",
        "BLE advertising restart after disconnect failed",
    };
    const char *const settings_wifi_open_required[] = {
        "static void settings_on_wifi(void *user_ctx)",
        "s_settings_wifi_detail_active = true;",
        "settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_START_DELAY_MS, \"wifi detail open\")",
        "s_settings_pending_state_refresh = true;",
        "Settings Wi-Fi detail requested",
    };
    const char *const settings_wifi_open_deferred_forbidden[] = {
        "settings_start_ble_provisioning_if_needed();",
        "settings_resume_wifi_if_needed();",
        "settings_fill_state(&state);",
        "factory_settings_ui_update_state(&state);",
    };
    const char *const settings_wifi_detail_tick_required[] = {
        "if (s_settings_pending_wifi_detail_start)",
        "now_us >= s_settings_wifi_detail_ble_start_due_us",
        "settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_RETRY_MS, \"ble start retry\")",
        "settings_stop_wifi_detail_provisioning_if_owned(\"wifi detail closed\")",
        "settings_resume_wifi_if_needed();",
        "if (s_settings_wifi_detail_active)",
        "settings_schedule_wifi_detail_ble_start(SETTINGS_WIFI_DETAIL_BLE_RETRY_MS, \"ble inactive\")",
        "settings_resume_wifi_if_needed();",
    };
    const char *const settings_ble_switch_deferred_required[] = {
        "static bool settings_on_bluetooth_changed(bool enabled, void *user_ctx)",
        "s_settings_pending_ble_switch_enabled = enabled;",
        "s_settings_pending_ble_switch = true;",
        "BLE Settings switch %s queued",
        "static bool settings_perform_bluetooth_change(bool enabled)",
        "if (s_settings_pending_ble_switch)",
        "s_settings_pending_ble_switch = false;",
        "settings_perform_bluetooth_change(s_settings_pending_ble_switch_enabled)",
    };
    const char *const settings_ble_switch_full_release_required[] = {
        "static bool settings_perform_bluetooth_change(bool enabled)",
        "app_resource_stop_ble();",
        "s_ble_provisioning_release_pending = false;",
    };
    const char *const settings_ble_switch_partial_release_forbidden[] = {
        "ret = ble_service_disconnect();",
        "ret = ble_service_stop_advertising();",
    };
    const char *const settings_ble_switch_deferred_forbidden[] = {
        "app_resource_start_ble();",
        "ble_service_disconnect();",
        "ble_service_stop_advertising();",
    };
    const char *const settings_close_ble_release_required[] = {
        "static void settings_on_close(void)",
        "if (s_ble_stack_initialized)",
        "app_resource_stop_ble();",
        "s_settings_wifi_ble_provisioning_active = false;",
    };
    const char *const settings_wifi_detail_close_required[] = {
        "static void settings_on_wifi_detail_closed(void *user_ctx)",
        "s_settings_wifi_detail_active = false;",
        "settings_cancel_wifi_detail_ble_start();",
        "s_settings_pending_wifi_detail_close = true;",
        "Settings Wi-Fi detail closed",
        "static void settings_stop_wifi_detail_provisioning_if_owned",
        "if (!s_settings_wifi_ble_provisioning_active)",
        "app_resource_stop_ble();",
        "LOG_HEAP_STATE(\"settings_wifi_detail_ble_stop\");",
    };
    const char *const settings_wifi_detail_launch_intent_required[] = {
        "static void settings_open_wifi_detail_from_app(void);",
        "static bool s_settings_open_wifi_detail_on_open = false;",
        "static void settings_open_wifi_detail_from_app(void)",
        "s_settings_open_wifi_detail_on_open = true;",
        "watcher_app_open(\"settings.app\")",
        "App connect UI failed to open Settings Wi-Fi detail",
        "const bool open_wifi_detail = s_settings_open_wifi_detail_on_open;",
        "s_settings_open_wifi_detail_on_open = false;",
        "factory_settings_ui_build_wifi_detail_with_previous_screen_delete(NULL, &state, &callbacks,",
    };
    const char *const debug_app_connect_header_required[] = {
        "typedef esp_err_t (*debug_cli_app_connect_cb_t)(void);",
        "debug_cli_app_connect_cb_t connect_cb",
    };
    const char *const debug_app_connect_cli_required[] = {
        "static debug_cli_app_connect_cb_t s_app_connect_cb = NULL;",
        "s_app_connect_cb = connect_cb;",
        "static void debug_cli_handle_app_connect(void)",
        "s_app_connect_cb()",
        "debug.app.connect",
    };
    const char *const debug_app_connect_runtime_required[] = {
        "DEBUG_APP_COMMAND_CONNECT",         "static esp_err_t debug_app_enqueue_connect(void);",
        "debug_app_enqueue_connect",         "static esp_err_t debug_app_enqueue_connect(void)",
        ".type = DEBUG_APP_COMMAND_CONNECT", "case DEBUG_APP_COMMAND_CONNECT:",
        "app_connect_action_clicked(NULL);", "debug_app_connect",
    };
    const char *const voice_runtime_deferred_required[] = {
        "typedef enum {",
        "VOICE_RUNTIME_STAGE_PENDING",
        "VOICE_RUNTIME_STAGE_AUDIO_STARTING",
        "VOICE_RUNTIME_STAGE_DEGRADED",
        "static void voice_runtime_request_start(const char *reason)",
        "static void voice_runtime_tick(void)",
        "voice_runtime_start_if_due",
        "voice_open_ui_ms",
        "wake_init_ms",
        "voice_ready_ms",
        "LOG_HEAP_STATE(\"after_voice_runtime_start\")",
    };
    const char *const voice_on_open_required[] = {
        "const char *open_reason = active_app_is(CLIENT_APP_ID) ? \"client.app open\" : \"voice.app open\";",
        "s_voice_ready_ui_shown = false;",
        "voice_app_reset_connect_ui_state();",
        "ws_client_set_behavior_feedback_enabled(false);",
        "voice_runtime_request_start(open_reason)",
        "local_behavior_ui_open_ex(\"standby\", \"\", \"\", \"\", false)",
        "voice_app_update_connect_ui_if_needed();",
        "s_boot_completed = true;",
        "transport_schedule_retry(0);",
    };
    const char *const voice_fade_duration_required[] = {
        "animation_service_init()",
        "animation_service_bind_surface(surface)",
        "animation_service_unbind_surface()",
    };
    const char *const voice_on_open_connect_ui_required[] = {
        "local_behavior_ui_open_ex(\"standby\", \"\", \"\", \"\", false)",
        "voice_app_update_connect_ui_if_needed();",
        "voice_app_enter_ready_ui_if_needed(open_reason)",
    };
    const char *const voice_connect_ui_required[] = {
        "#define VOICE_CONNECT_UI_WS_TIMEOUT_MS 15000U",
        "VOICE_CONNECT_VIEW_WIFI_SETUP",
        "VOICE_CONNECT_VIEW_WIFI_CONNECTING",
        "VOICE_CONNECT_VIEW_WIFI_FAILED",
        "APP_CONNECT_ACTION_RETRY",
        "APP_CONNECT_ACTION_WIFI",
        "s_voice_connect_ws_wait_start_us",
        "VOICE_RUNTIME_STAGE_WS_FAILED",
        "app_wifi_gate_fill_status(APP_WIFI_GATE_VIEW_SETUP, \"Desktop Link\"",
        "app_wifi_gate_fill_status(APP_WIFI_GATE_VIEW_CONNECTING, \"Desktop Link\"",
        "static void voice_app_enter_ws_failed",
        "static void voice_app_update_connect_ui_if_needed(void)",
        "static void voice_app_retry_connect(const char *reason)",
        "static bool voice_app_handle_connect_action(const char *reason)",
        "static void app_connect_process_action_click(void)",
        "hal_display_voice_connect_status_set(title, detail, action, show_spinner, alert)",
        "hal_display_voice_connect_status_set_action_callback(app_connect_action_clicked, NULL)",
        "Desktop Link connect action requested:",
        "App connect action dispatch active=",
        "Wi-Fi required",
        "Connecting WebSocket",
        "WebSocket failed",
        "Button: Open Wi-Fi",
        "Button: Retry",
        "voice_app_handle_connect_action(\"button\")",
        "active_app_is_client_voice_host()",
        "settings_open_wifi_detail_from_app();",
    };
    const char *const app_wifi_gate_connecting_action_required[] = {
        "case APP_WIFI_GATE_VIEW_SETUP:",  "case APP_WIFI_GATE_VIEW_CONNECTING:", "case APP_WIFI_GATE_VIEW_FAILED:",
        "return APP_CONNECT_ACTION_WIFI;", "return APP_CONNECT_ACTION_NONE;",
    };
    const char *const app_wifi_gate_connecting_ui_required[] = {
        "case APP_WIFI_GATE_VIEW_CONNECTING:",
        "*action = \"Button: Open Wi-Fi\";",
    };
    const char *const client_wifi_gate_deps_required[] = {
        "void (*open_wifi_gate_base_ui)(void);",
        "bool (*show_wifi_gate)(const char *app_label);",
        "bool (*handle_wifi_gate_action)(const char *reason);",
        "void (*clear_wifi_gate)(void);",
        "void (*set_wifi_gate_action_enabled)(bool enabled);",
        "void client_app_process_connect_action_click(void);",
    };
    const char *const client_wifi_gate_flow_required[] = {
        "static bool client_app_show_wifi_gate_if_needed(void)",
        "s_deps.show_wifi_gate(\"Desktop Link\")",
        "client_app_open_wifi_gate_base();",
        "if (!client_app_show_wifi_gate_if_needed())",
        "client_app_open_ui_waiting();",
        "if (client_app_show_wifi_gate_if_needed())",
        "if (!s_main_ui_shown)",
        "Waiting Desktop Link",
        "Desktop Link connected",
        "client_app_handle_wifi_gate_action(\"button\")",
        "void client_app_process_connect_action_click(void)",
        "client_app_handle_wifi_gate_action(\"touch\")",
    };
    const char *const client_voice_app_main_required[] = {
        "#define CLIENT_APP_ID \"client.app\"",
        "#define VOICE_APP_ID \"voice.app\"",
        "static bool app_id_is_client_voice_host(const char *app_id)",
        "strcmp(app_id, CLIENT_APP_ID) == 0 || strcmp(app_id, VOICE_APP_ID) == 0",
        "static const watcher_app_t s_client_voice_app",
        ".id = CLIENT_APP_ID,",
        ".name = \"Desktop Link\",",
        ".icon = \"voice\",",
        ".on_open = voice_app_on_open,",
        ".on_close = voice_app_on_close,",
        "ESP_ERROR_CHECK(watcher_app_register(&s_client_voice_app));",
        "ESP_ERROR_CHECK(watcher_app_register(&s_voice_app));",
    };
    const char *const legacy_client_app_main_forbidden[] = {
        "ESP_ERROR_CHECK(watcher_app_register(client_app_get_app()));",
    };
    const char *const app_center_wifi_gate_header_required[] = {
        "typedef struct {",
        "void (*open_wifi_gate_base_ui)(void);",
        "bool (*show_wifi_gate)(const char *app_label);",
        "bool (*handle_wifi_gate_action)(const char *reason);",
        "void (*clear_wifi_gate)(void);",
        "void (*set_wifi_gate_action_enabled)(bool enabled);",
        "} app_center_deps_t;",
        "void app_center_configure(const app_center_deps_t *deps);",
        "void app_center_process_connect_action_click(void);",
    };
    const char *const app_center_wifi_gate_flow_required[] = {
        "static bool app_center_wifi_gate_needed(void)",
        "return wifi_is_connected() != 1;",
        "static void app_center_open_wifi_gate_base(void)",
        "app_center_forget_screen_locked();",
        "s_deps.open_wifi_gate_base_ui();",
        "static bool app_center_show_wifi_gate_if_needed(void)",
        "s_deps.show_wifi_gate(\"App.Center\")",
        "app_center_handle_wifi_gate_action(\"button\")",
        "s_page = APP_CENTER_PAGE_LIST;",
        "if (app_center_show_wifi_gate_if_needed())",
        "App.Center waiting for Wi-Fi gate",
        "App.Center Wi-Fi gate cleared; rendering list",
        "void app_center_process_connect_action_click(void)",
        "app_center_handle_wifi_gate_action(\"touch\")",
    };
    const char *const app_center_wifi_gate_app_main_required[] = {
        "static app_wifi_gate_state_t s_app_center_wifi_gate = {0};",
        "static void app_connect_open_wifi_gate_base_ui(void)",
        "if (app_animation_ui_init_text_only(\"\") != 0)",
        "behavior_state_set_with_resources(\"standby\", \"\", 0, \"\", \"\")",
        "static void app_center_open_wifi_gate_base_ui(void)",
        "app_animation_ui_deinit();",
        "app_connect_open_wifi_gate_base_ui();",
        "static bool app_center_show_wifi_gate(const char *app_label)",
        "app_wifi_gate_show(&s_app_center_wifi_gate, app_label, \"App.Center\")",
        "static bool app_center_handle_wifi_gate_action(const char *reason)",
        "app_wifi_gate_handle_action(&s_app_center_wifi_gate, reason)",
        "static void configure_app_center(void)",
        "app_center_configure(&deps);",
        "configure_app_center();",
        "app_center_process_connect_action_click();",
    };
    const char *const app_center_wifi_setup_page_forbidden[] = {
        "APP_CENTER_PAGE_WIFI_SETUP",
        "app_center_should_show_wifi_setup",
        "app_center_render_wifi_setup_locked",
        "app_center_wifi_title",
        "app_center_wifi_hint",
        "App.Center opens after Wi-Fi is ready",
    };
    const char *const voice_connect_root_settings_forbidden[] = {
        "watcher_app_open(\"settings.app\")",
    };
    const char *const voice_connect_timeout_close_required[] = {
        "voice_app_enter_ws_failed(\"voice ws connect timeout\", elapsed_ms);",
        "transport_abort_discovery_request(safe_reason);",
        "transport_stop_ws(safe_reason);",
        "transport_deinit_ws_stack(safe_reason)",
        "transport_clear_cached_ws_url(safe_reason);",
        "voice_recorder_close()",
        "s_cloud_runtime_started = false;",
        "s_voice_runtime_stage = VOICE_RUNTIME_STAGE_WS_FAILED;",
        "voice ws failed waiting retry",
        "active_app_is_client_voice_host() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_WS_FAILED",
    };
    const char *const voice_connect_ws_failed_close_required[] = {
        "transport_clear_cached_ws_url(safe_reason);",
        "voice_recorder_close()",
        "s_voice_runtime_stage = VOICE_RUNTIME_STAGE_WS_FAILED;",
    };
    const char *const voice_connect_timeout_stop_forbidden[] = {
        "voice_recorder_stop()",
    };
    const char *const voice_recorder_close_releases_hal_required[] = {
        "if (release_audio_hal)",
        "hal_audio_deinit()",
        "hal_audio_enter_app_idle()",
    };
    const char *const voice_recorder_close_api_required[] = {
        "return voice_recorder_stop_internal(true);",
    };
    const char *const voice_recorder_stop_api_required[] = {
        "return voice_recorder_stop_internal(false);",
    };
    const char *const voice_recorder_behavior_feedback_api_required[] = {
        "void voice_recorder_set_behavior_feedback_enabled(bool enabled);",
    };
    const char *const voice_recorder_behavior_feedback_required[] = {
        "static bool g_behavior_feedback_enabled = true;",
        "void voice_recorder_set_behavior_feedback_enabled(bool enabled)",
        "g_behavior_feedback_enabled = enabled;",
    };
    const char *const voice_recorder_behavior_feedback_guard_required[] = {
        "if (!g_behavior_feedback_enabled)",
        "return;",
    };
    const char *const voice_waiting_ui_without_animation_required[] = {
        "behavior_state_set_with_resources(\"thinking\", \"\", 0, \"thinking\", \"\")",
        "Thinking expression shown without body action, sound, or text",
    };
    const char *const voice_connect_retry_restart_required[] = {
        "transport_abort_discovery_request(\"voice connect retry\");",
        "transport_deinit_ws_stack(\"voice connect retry\")",
        "transport_clear_cached_ws_url(\"voice connect retry\");",
        "transport_reset_discovery_retry_backoff(\"voice connect retry\");",
        "transport_schedule_retry(0);",
        "ensure_cloud_runtime_started();",
        "voice_app_update_connect_ui_if_needed();",
    };
    const char *const discovery_cancel_wait_required[] = {
        "static void transport_reset_discovery_result_queue(void)",
        "xQueueReset(s_discovery_result_queue);",
        "static bool transport_wait_for_discovery_idle",
        "transport_reset_discovery_result_queue();",
    };
    const char *const voice_ready_ui_transition_required[] = {
        "static bool voice_app_enter_ready_ui_if_needed(const char *reason)",
        "active_app_is_client_voice_host()",
        "s_voice_runtime_stage != VOICE_RUNTIME_STAGE_READY",
        "if (app_animation_ui_init_with_text(\"\") != 0)",
        "Voice ready UI cannot start without an animation surface",
        "hal_display_voice_connect_status_clear();",
        "behavior_state_set_with_resources(\"standby_entry\", \"\", 0, NULL, \"\")",
        "hal_display_set_text_with_style(\"\", 0, false)",
        "s_voice_ready_ui_shown = true;",
        "voice_app_reset_connect_ui_state();",
        "voice_app_resume_wake_word_for_sleep_standby(\"voice ready standby entry\")",
        "Voice ready UI entered",
    };
    const char *const voice_runtime_ready_ui_required[] = {
        "(void)voice_app_enter_ready_ui_if_needed(\"voice runtime ready\");",
    };
    const char *const agent_connect_ui_required[] = {
        "static void agent_app_reset_connect_ui_state(void)",
        "s_agent_ready_ui_shown = false;",
        "s_agent_connect_action_click_pending = false;",
        "hal_display_voice_connect_status_set(title, detail, action, show_spinner, alert)",
        "hal_display_voice_connect_status_set_action_callback(agent_app_connect_action_clicked, NULL)",
        "static bool agent_app_handle_connect_action(const char *reason)",
        "static void agent_app_process_connect_action_click(void)",
        "agent_runtime_retry(agent_app_now_ms(), \"agent connect retry\")",
        "watcher_app_open(\"settings.app\")",
        "Preparing cloud session",
        "Cloud session did not become ready",
        "Session unavailable",
        "Button: Retry",
    };
    const char *const agent_ready_ui_required[] = {
        "static bool agent_app_enter_ready_ui_if_needed(const char *reason)",
        "active_app_is(\"agent.app\")",
        "hal_display_voice_connect_status_clear();",
        "if (!s_agent_activity_seen) {",
        "behavior_state_set_with_resources(\"standby_entry\", \"\", 0, NULL, \"\")",
        "behavior_state_set_with_resources(\"standby\", \"\", 0, anim_id, \"\")",
        "hal_display_set_text_with_style(\"\", 0, false)",
        "s_agent_ready_ui_shown = true;",
        "agent_app_reset_connect_ui_state();",
        "Agent ready UI entered",
    };
    const char *const agent_ready_idle_random_required[] = {
        "static const char *agent_app_choose_ready_idle_anim(void)",
        "s_agent_ready_idle_last_variant_index",
        "available_count > 1 && available[i] == s_agent_ready_idle_last_variant_index",
        "filtered[esp_random() % (uint32_t)filtered_count]",
        "ready_idle_variant_name(selected)",
        "static void agent_app_update_ready_idle_variant(void)",
        "agent_runtime_get_stage() != AGENT_RUNTIME_STAGE_READY",
        "!s_agent_activity_seen",
        "s_agent_ready_idle_sleeping",
        "s_agent_ready_idle_next_switch_us",
        "behavior_state_set_with_resources(\"standby\", \"\", 0, anim_id, \"\")",
        "static bool agent_app_apply_ready_idle_sleep(void)",
        "behavior_state_set_with_resources(\"standby_start\", \"\", 0, NULL, \"\")",
        "s_agent_ready_idle_sleeping = true;",
        "Agent ready sleep transition applied",
        "agent_app_update_ready_idle_variant();",
    };
    const char *const agent_ready_ui_sleep_forbidden[] = {
        "s_ready_idle_sleeping = true;",
        "mark_ready_idle_standby_transition_pending();",
    };
    const char *const agent_activity_ui_required[] = {
        "behavior_state_poll_animation_event(&observed)",
        "strcmp(observed.state_id, \"listening_wake\") == 0",
        "case ANIMATION_EVENT_COMPLETED:",
        "agent_runtime_complete_wake(agent_app_now_ms())",
        "agent_runtime_fail_wake(\"Agent animation transition failed\")",
        "agent_app_show_activity(\"listening\", \"listening\")",
        "agent_app_show_activity(\"thinking\", \"thinking\")",
        "agent_app_show_activity(\"speaking\", \"speaking\")",
        "behavior_state_set_with_resources(state_id, \"\", 0, anim_id, \"\")",
        "s_agent_activity_seen = true;",
        "s_agent_ready_ui_shown = false;",
    };
    const char *const agent_ready_prefetch_required[] = {
        "static void agent_app_prefetch_listening_anim(const char *reason)",
        "animation_prefetch_hint(EMOJI_ANIM_LISTENING)",
        "Agent listening animation prefetch requested: reason=%s",
        "static void agent_app_prefetch_wake_anim(const char *reason)",
        "animation_prefetch_hint(EMOJI_ANIM_STANDBY_END)",
        "Agent wake animation prefetch requested: reason=%s",
        "agent_app_prefetch_wake_anim(\"agent sleep loop committed\")",
        "agent_app_prefetch_listening_anim(\"agent ready idle\")",
        "agent_app_prefetch_listening_anim(\"agent ready idle variant\")",
    };
    const char *const agent_sfx_suppression_required[] = {
        "static void agent_app_set_local_sfx_suppressed(bool suppressed, const char *reason)",
        "sfx_service_set_cloud_audio_busy(suppressed);",
        "Agent local sfx %s: reason=%s",
    };
    const char *const agent_failure_clears_activity_required[] = {
        "static void agent_app_clear_activity_on_failure(void)",
        "agent_audio_player_abort();",
        "behavior_state_set_with_resources(\"standby\", \"\", 0, \"standby\", \"\")",
        "agent_app_clear_activity_on_failure();",
        "agent_app_show_connect_view(stage, error);",
    };
    const char *const agent_stage_sfx_suppression_order_required[] = {
        "case AGENT_RUNTIME_STAGE_AUDIO_STARTING:",
        "agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_READY:",
        "agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_LISTENING:",
        "agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_THINKING:",
        "agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_SPEAKING:",
        "agent_app_set_local_sfx_suppressed(true, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_FAILED:",
        "agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));",
        "case AGENT_RUNTIME_STAGE_STOPPED:",
        "agent_app_set_local_sfx_suppressed(false, agent_runtime_stage_name(stage));",
    };
    const char *const agent_on_close_connect_ui_required[] = {
        "hal_display_voice_connect_status_set_action_callback(NULL, NULL);",
        "hal_display_voice_connect_status_clear();",
        "agent_app_set_local_sfx_suppressed(false, \"agent.app close\");",
        "agent_app_reset_connect_ui_state();",
    };
    const char *const agent_stage_old_ui_forbidden[] = {
        "Connecting Agent",
        "Agent ready",
        "Agent disconnected",
        "\"sad\"",
    };
    const char *const agent_behavior_feedback_required[] = {
        "static void agent_runtime_set_behavior_feedback(bool enabled, void *user_ctx)",
        "ws_client_set_behavior_feedback_enabled(enabled);",
        "voice_recorder_set_behavior_feedback_enabled(enabled);",
    };
    const char *const voice_stable_ram_diag_required[] = {
        "static void voice_diag_schedule_stable_samples(void)",
        "static void voice_diag_cancel_stable_samples(void)",
        "static void voice_diag_log_stable_if_due(void)",
        "app_ui_diag_log_snapshot(\"voice_stable_1s\");",
        "app_ui_diag_log_snapshot(\"voice_stable_3s\");",
        "voice_diag_log_stable_if_due();",
        "voice_diag_schedule_stable_samples();",
        "voice_diag_cancel_stable_samples();",
    };
    const char *const voice_ready_idle_lightweight_required[] = {
        "static bool ready_idle_should_use_lightweight_ui(void)",
        "return is_basic_app_active() && s_voice_runtime_stage == VOICE_RUNTIME_STAGE_READY;",
    };
    const char *const voice_ready_idle_flow_required[] = {
        "#define READY_IDLE_STANDBY_TIMEOUT_MS 60000",
        "#define READY_IDLE_AMBIENT_ACCENT_COUNT 5",
        "#define READY_IDLE_AMBIENT_ACCENT_PERCENT 25",
        "client_voice_ready",
        "voice_ready_idle_can_replace_busy_state",
        "behavior_state_set_with_resources(\"standby_start\", view->text, view->font_size, NULL, \"\")",
        "esp_random() % (uint32_t)available_count",
        "s_ready_idle_last_base_variant_index",
        "s_ready_idle_last_accent_index",
        "available_count > 1 && available[i] == s_ready_idle_last_base_variant_index",
        "available_count > 1 && available[i] == s_ready_idle_last_accent_index",
        "filtered[esp_random() % (uint32_t)filtered_count]",
        "animation_prefetch_hint(ready_idle_variant_type(selected))",
        "behavior_state_set_with_resources_and_action_once(\"standby\", view->text, view->font_size, anim_id, \"\", "
        "action_id)",
        "static bool apply_ready_idle_passive_variant(const idle_hint_view_t *view, int64_t now_us)",
        "behavior_state_set_with_resources(\"standby\", view->text, view->font_size, passive_anim_id, \"\")",
        "s_ready_idle_phase = READY_IDLE_PHASE_PERFORMING;",
        "s_ready_idle_phase = READY_IDLE_PHASE_RESTING;",
        "s_ready_idle_behavior_complete_us",
        "behavior_state_poll_animation_event(&observed)",
        "ANIMATION_EVENT_COMPLETED",
        "behavior_state_is_action_active()",
        "READY_IDLE_MIN_PASSIVE_REST_MS",
        "s_ready_idle_sleep_retry_us",
        "behavior_state_has_action(anim_id) ? anim_id : NULL",
        "emoji_get_loop_duration_ms(anim_type)",
        "READY_IDLE_COMPLETION_GUARD_MS",
        "s_ready_idle_action_owned = action_id != NULL;",
        "s_ready_idle_last_base_variant_index = selected;",
        "s_ready_idle_last_accent_index = selected - READY_IDLE_VARIANT_COUNT;",
        "desired_hint == IDLE_HINT_READY && s_ready_idle_action_owned",
        "view.text = \"\";",
        "s_ready_idle_sleep_deadline_us = now_us + (int64_t)READY_IDLE_STANDBY_TIMEOUT_MS * 1000LL;",
        "s_ready_idle_next_switch_us",
        "\"blink\"",
        "\"sunglasses\"",
        "\"speechless\"",
        "\"concentration\"",
        "\"sad\"",
        "Ready idle foreground task completed; entering ambient wait",
        "ready_idle_state_is_sleep_state",
        "control_ingress_has_foreground_ai_lease()",
        "standby_entry",
        "standby_loop",
    };
    const char *const random_touch_fondle_required[] = {
        "static const touch_fondle_variant_t s_touch_fondle_variants[]",
        "\"fondle_love\"",
        "\"fondle_anger\"",
        "esp_random() % TOUCH_FONDLE_VARIANT_COUNT",
        "Touch press triggered %s",
    };
    const char *const voice_tts_completion_ready_idle_required[] = {
        "#define VOICE_TTS_COMPLETION_READY_IDLE_GRACE_MS 5000U",
        "static void voice_app_track_tts_lifecycle(bool voice_app_active)",
        "static bool voice_app_tts_completion_recent(void)",
        "s_voice_tts_completion_until_us",
        "ws_client_is_tts_playing()",
        "Voice TTS completion observed; ready idle handoff armed",
        "strcmp(current_state, \"thinking\") == 0 && voice_app_tts_completion_recent()",
        "Voice TTS completion using text-only standby under memory pressure",
        "voice_app_track_tts_lifecycle(voice_app_active);",
    };
    const char *const basic_ready_idle_lightweight_required[] = {
        "static bool apply_ready_idle_lightweight_standby(const idle_hint_view_t *view)",
        "const char *text = view->text;",
        "behavior_state_set_with_resources(\"standby\", text, view->font_size, \"standby\", \"\")",
        "voice_app_resume_wake_word_for_sleep_standby(\"ready idle lightweight standby\")",
        "Ready idle lightweight standby applied for app runtime",
    };
    const char *const voice_on_open_forbidden[] = {
        "ws_client_set_behavior_feedback_enabled(true);",
        "voice_recorder_start();",
        "local_behavior_ui_open_ex(\"processing\", \"Connecting cloud...\", \"processing\", \"\", true)",
        "emoji_anim_request_fade_in(VOICE_OPEN_ANIM_FADE_IN_MS);",
        "local_behavior_ui_open(\"processing\", \"Preparing Voice\", \"processing\", \"\");",
        "local_behavior_ui_open_text_only(\"Preparing Voice\")",
        "local_behavior_ui_open_ex(\"standby\", \"Connecting cloud...\", \"\", \"\", false)",
        "local_behavior_ui_open_ex(\"standby\", \"Preparing Voice\", \"standby\", \"\", true)",
    };
    const char *const hal_display_animation_surface_required[] = {
        "static int hal_display_ui_init_internal(const char *initial_text, bool enable_animation)",
        "if (enable_animation)",
        "img_emoji = lv_img_create(scr);",
        "void *hal_display_get_animation_surface(void)",
        "return (void *)img_emoji;",
    };
    const char *const hal_display_animation_control_forbidden[] = {
        "#include \"animation_service.h\"", "#include \"anim_player.h\"", "emoji_anim_",         "animation_submit(",
        "animation_service_bind_surface(",  "ANIM_INTERRUPT_POLICY_PATH", "fallback_timeout_ms",
    };
    const char *const hal_display_voice_connect_status_required[] = {
        "int hal_display_voice_connect_status_set",
        "hal_display_voice_connect_status_set_action_callback",
        "hal_display_voice_connect_action_event_cb",
        "lv_spinner_create(voice_connect_panel, 900, 70)",
        "voice_connect_title_label",
        "voice_connect_detail_label",
        "voice_connect_action_label",
        "voice_connect_action_button",
        "lv_obj_set_size(voice_connect_panel, LV_PCT(100), LV_PCT(100))",
        "lv_obj_add_flag(voice_connect_panel, LV_OBJ_FLAG_GESTURE_BUBBLE)",
        "code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED",
        "voice_connect_action_button = lv_btn_create(voice_connect_panel)",
        "lv_obj_clear_flag(voice_connect_panel, LV_OBJ_FLAG_CLICKABLE)",
        "lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_CLICKABLE)",
        "lv_obj_add_flag(voice_connect_action_label, LV_OBJ_FLAG_EVENT_BUBBLE)",
        "lv_obj_add_event_cb(voice_connect_action_button, hal_display_voice_connect_action_event_cb, LV_EVENT_PRESSED",
        "lv_obj_add_event_cb(voice_connect_action_button, hal_display_voice_connect_action_event_cb, LV_EVENT_CLICKED",
        "hal_display_update_text_overlay_visibility_locked(\"\")",
        "void hal_display_voice_connect_status_clear(void)",
    };
    const char *const anim_player_prefetch_required[] = {
        "ANIM_LOAD_TARGET_PREFETCH",
        "static anim_playback_t g_prefetch_playback",
        "Animation switch uses prefetched frames: %s buffered=%d",
        "Animation prefetch queued: %s",
        "playback_open(&g_prefetch_playback, type, g_latest_generation, ANIMATION_TICKET_INVALID,",
        "ANIM_PLAYBACK_RESOURCE_DEFAULT, 0U, 0U)",
    };
    const char *const display_ui_text_only_required[] = {
        "int display_update_with_style(const char *text, int font_size, display_text_style_t text_style,",
        "hal_display_set_text_with_style(text, normalized_font_size, text_style == DISPLAY_TEXT_STYLE_ALERT)",
        "int display_get_text(char *out_buf, int buf_size)",
    };
    const char *const display_ui_animation_forbidden[] = {
        "#include \"animation_service.h\"",
        "#include \"anim_player.h\"",
        "emoji_anim_",
        "display_get_emoji",
        "sync_current_emoji",
        "animation_submit(",
        "animation_get_snapshot(",
    };
    const char *const behavior_display_failure_fallback_required[] = {
        "Display text update failed for state '%s' during %s",
        "if (anim == NULL)",
        "display_update_with_style(text, command->font_size, text_style, NULL)",
        "animation_result = animation_submit(&request, &animation_ticket);",
        "Animation submit failed state='%s' anim='%s' owner=%lu result=%d",
    };
    const char *const anim_direct_lcd_single_dma_strip_required[] = {
        "#define ANIM_DIRECT_LCD_STRIP_SRC_ROWS 2U",
        "#define ANIM_DIRECT_LCD_STRIP_BUFFERS 1U",
        "static uint16_t *g_direct_strip = NULL;",
        "heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)",
        "buffers=%u bytes_each=%u",
        "uint16_t *dst = g_direct_strip;",
    };
    const char *const anim_direct_lcd_fade_color_shift_forbidden[] = {
        "blend_rgb565_with_black",
        "color = blend_rgb565_with_black(color, fade_alpha);",
    };
    const char *const anim_direct_lcd_double_dma_strip_forbidden[] = {
        "static uint16_t *g_direct_strip[2]",
        "g_direct_next_strip",
        "(g_direct_next_strip + 1) % 2",
    };
    const char *const voice_on_button_exit_only_required[] = {
        "static void voice_app_on_button(void)",
        "local_exit_is_visible()",
        "local_return_to_launcher();",
        "local_show_exit();",
    };
    const char *const local_exit_touch_target_required[] = {
        "static void local_exit_event_cb",
        "code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED",
        "if (!local_exit_is_visible())",
        "local_return_to_launcher_from_lvgl_event(true);",
        "lv_indev_wait_release(indev);",
        "launcher_arm_open_input_settle(LAUNCHER_RETURN_TOUCH_SETTLE_MS, \"local exit touch\");",
        "lv_obj_set_size(s_local_exit, 160, 64)",
        "lv_obj_set_ext_click_area(s_local_exit, 16)",
        "lv_obj_add_event_cb(s_local_exit, local_exit_event_cb, LV_EVENT_PRESSED, NULL)",
    };
    const char *const local_exit_pointer_swipe_required[] = {
        "#define LOCAL_EXIT_SWIPE_MIN_DISTANCE_PX 64",
        "static void local_pointer_swipe_event_cb",
        "code == LV_EVENT_PRESSED",
        "code != LV_EVENT_PRESSING",
        "point.y - s_local_pointer_swipe_start.y <= -LOCAL_EXIT_SWIPE_MIN_DISTANCE_PX",
        "lv_indev_wait_release(indev);",
        "static void local_register_pointer_swipe_handlers",
        "lv_obj_remove_event_cb(obj, local_pointer_swipe_event_cb);",
        "lv_obj_add_event_cb(obj, local_pointer_swipe_event_cb, LV_EVENT_ALL, NULL);",
        "local_register_pointer_swipe_handlers(screen);",
    };
    const char *const local_exit_animation_live_required[] = {
        "static void local_update_exit_anim_protection_locked(void)",
        "static void local_apply_exit_anim_protection(void)",
        "animation_set_overlay_region(&s_local_overlay_region)",
        "animation_clear_overlay_region()",
        "s_local_overlay_update_pending = true;",
    };
    const char *const local_show_exit_animation_stop_forbidden[] = {
        "emoji_anim_stop();",
        "s_local_exit_paused_anim = emoji_anim_is_running();",
        "s_local_exit_resume_anim = emoji_anim_get_type();",
    };
    const char *const animation_player_direct_api_forbidden[] = {
        "#include \"anim_player.h\"",
        "emoji_anim_start(",
        "emoji_anim_stop(",
        "emoji_anim_prefetch_type(",
        "emoji_anim_get_type(",
        "emoji_anim_get_requested_type(",
        "emoji_anim_set_direct_lcd_protected_region(",
        "emoji_anim_clear_direct_lcd_protected_region(",
        "emoji_anim_request_fade_in(",
    };
    const char *const happy_ticket_handoff_required[] = {
        "\"on_animation_complete\": { \"anim\": \"happy\", \"state\": \"standby_loop\" }",
        "\"on_animation_failed\": { \"state\": \"standby_loop\" }",
        "\"anim\": \"happy\", \"playback_mode\": \"once\"",
    };
    const char *const happy_timed_handoff_forbidden[] = {
        "emoji_get_loop_duration_ms(EMOJI_ANIM_HAPPY)",
        "READY_IDLE_POST_HAPPY_MIN_DELAY_MS",
        "ready_idle_post_happy_delay_ms",
        "ready_idle_can_replace_happy",
        "s_ready_idle_after_happy_us",
        "s_ready_idle_happy_observed_us",
    };
    const char *const legacy_display_animation_api_forbidden[] = {
        "display_get_emoji(",     "display_emoji_from_string(",   "hal_display_get_current_emoji_id(",
        "hal_display_set_emoji(", "sync_current_emoji_from_hal(",
    };
    const char *const main_animation_snapshot_required[] = {
        "behavior_state_poll_animation_event(&observed)",
        "case ANIMATION_EVENT_COMMITTED:",
        "case ANIMATION_EVENT_COMPLETED:",
        "case ANIMATION_EVENT_PREEMPTED:",
        "case ANIMATION_EVENT_CANCELLED:",
        "case ANIMATION_EVENT_FAILED:",
        "case ANIMATION_EVENT_ACCEPTED:",
        "case ANIMATION_EVENT_PREPARING:",
        "case ANIMATION_EVENT_CYCLE_COMPLETED:",
        "agent_animation_flow_on_event(&s_agent_animation_flow, clip, event)",
    };
    const char *const sdk_control_app_required[] = {
        "#include \"sdk_control_app.h\"",
        "#define LAUNCHER_ENTRY_COUNT 5",
        "{.id = SDK_CONTROL_APP_ID,",
        ".name = \"Python\\nSDK\"",
        "static void sdk_control_show_pairing_code",
        "static void sdk_control_show_connected",
        "static void configure_sdk_control_app",
        "ESP_ERROR_CHECK(watcher_app_register(sdk_control_app_get()));",
    };
    const char *const sdk_control_ui_component_required[] = {
        "sdk_control_ui.c",
        "#include \"sdk_control_ui.h\"",
        "sdk_control_ui_build",
        "sdk_control_ui_show_pairing",
        "sdk_control_ui_show_connected",
        "sdk_control_ui_show_error",
        "sdk_control_ui_reset",
    };
    const char *const sdk_control_semantic_ui_required[] = {
        "show_pairing_code",
        "show_connected",
        "show_error",
        "restore_control_ui",
    };
    const char *const sdk_control_legacy_text_ui_forbidden[] = {
        "show_status",
        "Python SDK\\nPair code:",
    };
    const char *const sdk_control_display_lifecycle_required[] = {
        "s_display_job_id",          "sdk_control_restore_display_ui", "event.job_id == s_display_job_id",
        "WATCHER_SDK_JOB_COMPLETED", "WATCHER_SDK_JOB_FAILED",         "WATCHER_SDK_JOB_CANCELLED",
    };
    const char *const sdk_control_ui_visual_contract_required[] = {
        "SDK_CONTROL_UI_GREEN",   "SDK_CONTROL_UI_AMBER", "PYTHON SDK",      "PAIR CODE",     "Waiting for desktop",
        "Desktop control active", "Ready for commands",   "Connection lost", "NEW PAIR CODE", "Waiting to reconnect",
    };
    const char *const sdk_control_ui_input_debug_required[] = {
        "static void enable_event_bubble",
        "lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);",
        "sdk_control_ui_show_input_debug",
        "lv_label_set_text(ui->detail, safe_line);",
    };
    const char *const sdk_control_app_input_debug_required[] = {
        "show_input_debug",
        "sdk_format_input_debug",
        "s_ui.show_input_debug(line);",
    };
    const char *const sdk_control_main_input_debug_required[] = {
        "static void sdk_control_show_input_debug",
        "sdk_control_ui_show_input_debug(&s_sdk_control_ui, line);",
        ".show_input_debug = sdk_control_show_input_debug",
    };
    const char *const sdk_control_ui_vector_emblem_required[] = {
        "SDK_CONTROL_EMBLEM_CHECK",
        "SDK_CONTROL_EMBLEM_CLOSE",
        "ui->icon_stroke_a = lv_line_create(ui->emblem);",
        "ui->icon_stroke_b = lv_line_create(ui->emblem);",
        "lv_line_set_points(line, points, point_count);",
        "set_vector_line(ui->icon_stroke_a",
        "set_vector_line(ui->icon_stroke_b",
        "lv_obj_set_style_line_rounded(line, true, 0);",
        "set_emblem(ui, SDK_CONTROL_UI_GREEN, SDK_CONTROL_EMBLEM_CHECK, 148, 64);",
        "set_emblem(ui, SDK_CONTROL_UI_AMBER, SDK_CONTROL_EMBLEM_CLOSE, 90, 69);",
    };
    const char *const sdk_control_ui_font_symbol_forbidden[] = {
        "LV_SYMBOL_OK",
        "LV_SYMBOL_CLOSE",
    };
    const char *const sdk_control_ui_device_calibration_required[] = {
        "#define SDK_CONTROL_UI_DIM 0x838A85",
        "#define SDK_CONTROL_UI_GREEN_BACKGROUND 0x0B1D08",
        "#define SDK_CONTROL_UI_GREEN_EMBLEM_TOP 0x16240E",
        "static void set_screen_tone",
        "lv_obj_set_style_bg_color(ui->screen, lv_color_hex(background_top), 0);",
        "lv_obj_set_style_bg_grad_color(ui->screen, lv_color_hex(SDK_CONTROL_UI_BACKGROUND), 0);",
        "lv_obj_set_style_bg_grad_color(ui->emblem, lv_color_hex(emblem_bottom), 0);",
        "lv_obj_set_style_shadow_width(ui->emblem, 28, 0);",
        "lv_obj_set_style_shadow_spread(ui->emblem, 3, 0);",
        "lv_obj_set_style_shadow_opa(ui->emblem, LV_OPA_50, 0);",
        "ui->icon_prompt_bar = lv_obj_create(ui->emblem);",
        "lv_obj_set_size(ui->icon_prompt_bar, 18, 5);",
        "lv_obj_set_style_radius(ui->icon_prompt_bar, LV_RADIUS_CIRCLE, 0);",
        "lv_obj_align(ui->icon_prompt_bar, LV_ALIGN_CENTER, 16, 3);",
    };
    const char *const sdk_control_ui_original_layout_required[] = {
        "static void show_inline_status",
        "lv_obj_set_style_bg_opa(ui->status_pill, LV_OPA_TRANSP, 0);",
        "lv_obj_set_style_border_width(ui->status_pill, 0, 0);",
        "set_emblem(ui, SDK_CONTROL_UI_GREEN, SDK_CONTROL_EMBLEM_PROMPT, 96, 79);",
        "show_inline_status(ui, \"Waiting for desktop\", 270, 286, SDK_CONTROL_UI_GREEN);",
        "set_emblem(ui, SDK_CONTROL_UI_GREEN, SDK_CONTROL_EMBLEM_CHECK, 148, 64);",
        "show_status_pill(ui, \"Ready for commands\", 252, 315, SDK_CONTROL_UI_GREEN);",
        "set_emblem(ui, SDK_CONTROL_UI_AMBER, SDK_CONTROL_EMBLEM_CLOSE, 90, 69);",
        "lv_obj_set_style_text_font(ui->headline, &lv_font_montserrat_20, 0);",
        "show_inline_status(ui, \"Waiting to reconnect\", 286, 300, SDK_CONTROL_UI_AMBER);",
        "The previous session was closed safely",
    };
    const char *const sdk_control_launcher_focused_icon_layers_required[] = {
        "Focused canvas stays 102x102; only the foreground glyph is reduced to 75 percent.",
        ".header.w = 102,",
        ".header.h = 102,",
    };
    const char *const sdk_control_version_required[] = {
        "#include \"ota_service.h\"",
        "ota_service_get_fw_version()",
    };
    const char *const sdk_control_launcher_metadata_required[] = {
        ".name = \"Python SDK\"",
        ".icon = \"python-sdk\"",
        ".theme_color = 0xA9DE2C",
    };
    const char *const sdk_control_network_lifecycle_required[] = {
        "#include \"freertos/event_groups.h\"",
        "SDK_NET_STOP_REQUESTED",
        "SDK_NET_RESET_REQUESTED",
        "SDK_NET_EXITED",
        "xEventGroupWaitBits",
    };
    const char *const sdk_control_hello_recovery_required[] = {
        "#define SDK_HELLO_RESPONSE_TIMEOUT_MS 5000U",
        "static bool sdk_hello_response_timed_out(void)",
        "static bool sdk_request_gateway_reset(const char *reason)",
        "SDK_NET_RESET_REQUESTED",
        "s_session_reset_pending = true;",
        "s_was_connected = false;",
    };
    const char *const sdk_control_hello_recovery_tick_required[] = {
        "ws_client_has_hello_rejected()",
        "sdk_request_gateway_reset(\"hello rejected\")",
        "sdk_request_gateway_reset(\"hello response timeout\")",
    };
    const char *const sdk_discovery_pairing_required[] = {
        "#include \"esp_system.h\"",
        "#define SDK_DISCOVERY_REQUEST_ID_LENGTH 16U",
        "static bool pairing_code_is_valid",
        "cJSON_AddStringToObject(root, \"pairing_code\", pairing_code);",
        "cJSON_AddStringToObject(root, \"request_id\", request_id);",
        "strcmp(cmd->valuestring, \"ANNOUNCE\") != 0",
        "strcmp(response_pairing_code->valuestring, pairing_code) != 0",
        "strcmp(response_request_id->valuestring, request_id) != 0",
        "destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);",
    };
    const char *const sdk_discovery_pairing_forbidden[] = {
        "#include \"esp_log.h\"",
        "#include <errno.h>",
        "SDK_ADVERTISE",
        "bind(socket_handle",
        "#include \"esp_netif.h\"",
        "configure_discovery_destination",
        "esp_netif_get_handle_from_ifkey",
        "esp_netif_get_ip_info",
        "Discovery using directed broadcast",
    };
    const char *const sdk_control_network_exit_order_required[] = {
        "s_network_task = NULL;",
        "xEventGroupSetBits(events, SDK_NET_EXITED)",
        "vTaskDelete(NULL)",
    };
    const char *const sdk_control_ws_setup_order_required[] = {
        "ws_client_set_server_url(url)",
        "ws_client_init()",
        "ws_client_start()",
    };
    const char *const ws_client_router_bootstrap_required[] = {
        "static void ws_client_ensure_default_router(void)",
        "ws_handlers_init()",
        "router = ws_handlers_get_router()",
        "ws_router_init(&router)",
        "ws_client_ensure_default_router();",
    };
    const char *const app_main_router_bootstrap_forbidden[] = {
        "static bool s_ws_router_ready",
        "if (!s_ws_router_ready)",
    };
    const char *const sdk_control_network_lifecycle_forbidden[] = {
        "static volatile bool s_stop_requested",
        "static volatile bool s_network_reset_requested",
        "for (attempts = 0; attempts < 20 && s_network_task != NULL; ++attempts)",
        "if (s_network_events != NULL && s_network_task != NULL)",
    };
    const char *const sdk_control_protocol_diagnostics_required[] = {
        "#include \"cJSON.h\"",
        "SDK_NACK_QUEUE_DEPTH",
        "static void queue_protocol_nack",
        "\"command_queue_full\"",
        "\"unsupported_command\"",
        "\"invalid_argument\"",
        "cJSON_GetObjectItem(root, \"type\")",
    };
    const char *const sdk_control_handler_lifecycle_required[] = {
        "static bool s_handler_accepting",   "static unsigned s_handler_active",  "static bool sdk_handler_begin",
        "static void sdk_handler_end",       "static bool sdk_handler_wait_idle", "sdk_handler_stop_accepting()",
        "static bool reset_control_session", "s_session_reset_pending",
    };
    const char *const sdk_control_surface_contract_required[] = {
        "bool (*prepare_animation)(void);",
    };
    const char *const sdk_control_surface_preparation_required[] = {
        "static watcher_sdk_result_t prepare_animation_surface(void)",
        "s_ui.prepare_animation == NULL",
        "prepare_animation_surface()",
        "#include \"sfx_service.h\"",
        "sfx_service_set_voice_audio_busy(false);",
        "#if CONFIG_WATCHER_DEBUG_CLI_ENABLE",
        "SDK_SMOKE pairing_code=%s",
        "bool sdk_control_app_debug_log_pairing_code(void)",
    };
    const char *const sdk_control_audio_stream_preemption_required[] = {
        "ws_client_register_tts_frame_guard(sdk_audio_stream_guard, NULL);",
        "case WATCHER_SDK_PROTOCOL_AUDIO_STREAM_BEGIN:",
        "s_authorized_audio_stream_id",
        "case WATCHER_SDK_PROTOCOL_AUDIO_PLAY:",
        "ws_client_abort_tts_playback();",
        "case WATCHER_SDK_PROTOCOL_AUDIO_STOP:",
        "ws_client_abort_tts_playback();",
        "ws_client_register_tts_frame_guard(NULL, NULL);",
        "ws_client_set_behavior_feedback_enabled(false);",
        "ws_client_set_behavior_feedback_enabled(true);",
    };
    const char *const sdk_control_debug_pairing_cli_required[] = {
        "#include \"sdk_control_app.h\"",
        "debug.sdk.pairing",
        "sdk_control_app_debug_log_pairing_code()",
    };
    const char *const sdk_control_surface_binding_required[] = {
        "static bool sdk_control_prepare_animation(void)",
        "app_animation_ui_init_with_text(\"\")",
        ".prepare_animation = sdk_control_prepare_animation,",
        ".restore_control_ui = sdk_control_restore_control_ui,",
    };
    const char *const sdk_microphone_audio_release_required[] = {
        "microphone_audio_release_pending",
        "voice_recorder_get_state() != VOICE_STATE_RECORDING",
        "sfx_service_set_voice_audio_busy(false);",
        "event_type == ANIMATION_EVENT_COMPLETED",
        "strcmp(animation_event.state_id, context->behavior_id) == 0",
    };
    const char *const sdk_camera_async_required[] = {
        "static void sdk_camera_capture_task(void *user_context)",
        "camera_capture_done",
        "xTaskCreate(sdk_camera_capture_task",
        "xSemaphoreTake(context->camera_capture_done, portMAX_DELAY)",
    };
    const char *const sdk_camera_capture_sync_forbidden[] = {
        "camera_service_init()",
        "camera_service_configure(",
        "camera_service_capture_once()",
    };
    const char *const sdk_camera_cold_start_required[] = {
        "#define WATCHER_SDK_CAMERA_DEFAULT_WIDTH 640",
        "#define WATCHER_SDK_CAMERA_DEFAULT_HEIGHT 480",
        "#define WATCHER_SDK_CAMERA_WARM_UP_FRAMES 2",
        "#define WATCHER_SDK_CAMERA_WARM_UP_SETTLE_MS 500",
        "static esp_err_t sdk_camera_prepare(watcher_sdk_context_t *context)",
        "camera cold-start warm-up frames discarded",
        "error = sdk_camera_prepare(context);",
    };
    const char *const sdk_camera_warm_up_order_required[] = {
        "frame_index < WATCHER_SDK_CAMERA_WARM_UP_FRAMES",
        "error = camera_service_capture_once();",
        "vTaskDelay(pdMS_TO_TICKS(WATCHER_SDK_CAMERA_WARM_UP_SETTLE_MS));",
        "error = camera_service_register_frame_callback(sdk_camera_frame, context);",
    };
    const char *const hal_camera_official_capture_semantics_required[] = {
        "static esp_err_t hal_camera_request_frame(bool from_stream)",
        "return sscma_client_invoke(s_ctx.client, 1, false, true);",
        "return sscma_client_sample(s_ctx.client, 1);",
    };
    const char *const hal_camera_capture_request_required[] = {
        "ret = hal_camera_request_frame(from_stream);",
    };
    const char *const hal_camera_direct_invoke_forbidden[] = {
        "ret = sscma_client_invoke(s_ctx.client, 1, false, true);",
    };
    const char *const voice_animation_snapshot_required[] = {
        "static bool current_display_is_awake_idle_variant(emoji_anim_type_t *current_out)",
        "animation_get_snapshot(&snapshot)",
        "current = snapshot.visible_type;",
    };
    const char *const shutdown_system_animation_required[] = {
        "static void shutdown_present_user_feedback(void)",
        ".type = EMOJI_ANIM_ERROR",
        ".priority = ANIM_PRIORITY_SYSTEM",
        ".preempt_policy = ANIM_PROTECTED_AFTER_COMMIT",
        ".source = ANIM_SOURCE_POWER",
        "behavior_state_set_with_resources(\"error\", \"Shutting down...\", 0, \"\", \"\")",
        "animation_submit(&animation_request, &animation_ticket)",
        "Shutdown SYSTEM animation accepted ticket=%lu",
    };
    const char *const shutdown_behavior_animation_forbidden[] = {
        "behavior_state_set_with_resources(\"error\", \"Shutting down...\", 0, \"error\", \"\")",
    };
    const char *const animation_debug_cli_required[] = {
        "debug.anim status|play <name> [repeat]|cancel <ticket>|stress [count]",
        "animation_get_snapshot(&snapshot)",
        "animation_submit(&request, &ticket)",
        "animation_cancel((animation_ticket_t)ticket_value)",
        "snapshot.visible_ticket == ticket",
        "debug.anim stress requested=%d accepted=%d committed=%d timed_out=%d",
    };
    const char *const local_exit_direct_open_forbidden[] = {
        "watcher_app_open(\"launcher\")",
        "local_return_to_launcher();",
    };
    const char *const launcher_return_settle_required[] = {
        "#define LAUNCHER_RETURN_TOUCH_SETTLE_MS 700U",
        "static int64_t s_launcher_open_input_block_until_us = 0;",
        "static volatile bool s_local_return_to_launcher_pending = false;",
        "static void launcher_arm_open_input_settle",
        "static bool launcher_should_ignore_open_input",
        "Launcher %s ignored during return settle",
        "launcher_should_ignore_open_input(\"card click\", index)",
        "launcher_should_ignore_open_input(\"pending open\", index)",
        "static void local_queue_return_to_launcher(void)",
        "static void process_pending_app_navigation(void)",
        "s_local_return_to_launcher_pending = true;",
        "process_pending_app_navigation();",
        "bool voice_app_active = !s_local_return_to_launcher_pending",
    };
    const char *const voice_on_button_record_forbidden[] = {
        "voice_recorder_process_event(VOICE_EVENT_BUTTON_SHORT_CLICK);",
    };
    const char *const local_touch_dispatch_required[] = {
        "static void local_touch_event_cb",
        "code != LV_EVENT_SHORT_CLICKED",
        "local_exit_is_visible()",
        "voice_app_handle_connect_action(\"screen-touch\")",
        "Desktop Link connect action handled by screen touch",
        "active_app->on_touch(point.x, point.y);",
        "lv_obj_remove_event_cb(screen, local_touch_event_cb);",
        "lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);",
        "lv_obj_add_event_cb(screen, local_touch_event_cb, LV_EVENT_SHORT_CLICKED, NULL);",
    };
    const char *const voice_touch_double_tap_required[] = {
        "#define VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS 450",
        "static int64_t s_voice_last_touch_tap_us = 0;",
        "static void voice_app_on_touch(int16_t x, int16_t y)",
        "elapsed_ms > VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS",
        "voice_recorder_get_state() == VOICE_STATE_RECORDING",
        "voice_recorder_request_close();",
        "s_voice_runtime_stage != VOICE_RUNTIME_STAGE_READY",
        "voice_recorder_request_open();",
        ".on_touch = voice_app_on_touch",
    };
    const char *const agent_touch_double_tap_required[] = {
        "static int64_t s_agent_last_touch_tap_us = 0;",    "static void agent_app_on_touch(int16_t x, int16_t y)",
        "elapsed_ms > VOICE_TOUCH_DOUBLE_TAP_WINDOW_MS",    "Agent touch tap armed",
        "agent_app_handle_connect_action(\"touch\")",       "Agent touch double-tap handled",
        "agent_runtime_handle_button(agent_app_now_ms());", ".on_touch = agent_app_on_touch",
    };
    const char *const voice_close_transport_required[] = {
        "static void voice_app_stop_transport(const char *reason)",
        "transport_abort_discovery_request(safe_reason);",
        "transport_wait_for_discovery_idle(CLOUD_DISCOVERY_CANCEL_WAIT_MS)",
        "transport_stop_ws_for_resource_release(safe_reason);",
        "transport_deinit_ws_stack(safe_reason)",
        "voice_recorder_suspend_cloud_audio();",
        "voice_runtime_reset(safe_reason);",
        "transport_reset_cached_ws_resume_state();",
        "transport_clear_cached_ws_url(safe_reason);",
        "transport_schedule_retry(CLOUD_RETRY_DELAY_MS);",
        "s_transport_state = TRANSPORT_BLE_IDLE_CLOUD_SUSPENDED;",
        "LOG_HEAP_STATE(\"after_voice_transport_stop\");",
    };
    const char *const voice_on_close_required[] = {
        "static void voice_app_on_close(void)",
        "const char *close_reason = active_app_is(CLIENT_APP_ID) ? \"client.app close\" : \"voice.app close\";",
        "hal_display_voice_connect_status_clear();",
        "voice_app_stop_transport(close_reason);",
        "voice_recorder_close();",
        "local_behavior_app_cleanup();",
    };
    const char *const basic_on_open_required[] = {
        "voice_runtime_request_start(\"basic open\")",
    };
    const char *const basic_on_close_required[] = {
        "static void basic_app_on_close(void)",
        "voice_app_stop_transport(\"basic close\");",
        "voice_recorder_close();",
    };
    const char *const voice_runtime_tick_required[] = {
        "active_app_is_voice_runtime_context()",
        "if (voice_app_active)",
        "voice_runtime_tick();",
    };
    const char *const main_loop_active_after_dispatch_order_required[] = {
        "watcher_app_tick();",
        "on_button_single_click_dispatch();",
        "process_pending_app_navigation();",
        "bool voice_app_active = !s_local_return_to_launcher_pending",
        "if (wifi_transport_app_active)",
        "transport_coordinator_tick();",
    };
    const char *const voice_runtime_wifi_gate_required[] = {
        "static void voice_runtime_start_if_due(void)",
        "s_local_return_to_launcher_pending",
        "!active_app_is_voice_runtime_context()",
        "if (wifi_has_credentials() != 1 || wifi_is_connected() != 1)",
        "return;",
    };
    const char *const ble_stop_nonblocking_required[] = {
        "int64_t stop_started_us = esp_timer_get_time();",
        "ble_stop_ms",
        "ble_service_disconnect();",
        "ble_service_stop_advertising();",
        "ble_service_deinit();",
        "return ESP_OK;",
    };
    const char *const ble_stop_forbidden[] = {
        "ble_service_reset_session();",
    };
    const char *const voice_task_internal_stack_required[] = {
        "ret = xTaskCreate(voice_recorder_task",
        "CONFIG_VOICE_TASK_STACK_SIZE",
    };
    const char *const voice_task_psram_stack_forbidden[] = {
        "xTaskCreateWithCaps(voice_recorder_task",
        "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",
    };
    const char *const voice_resume_wake_closing_guard_required[] = {
        "static volatile bool g_closing",
        "g_closing = true;",
        "g_closing = false;",
        "if (g_closing || !g_task_running || g_state != VOICE_STATE_IDLE)",
        "Wake word resume skipped",
    };
    const char *const voice_wake_runtime_disabled_required[] = {
        "#define VOICE_WAKE_WORD_RUNTIME_ENABLED CONFIG_VOICE_WAKE_WORD_RUNTIME_ENABLED",
        "bool wake_runtime_active = VOICE_WAKE_WORD_RUNTIME_ENABLED && g_wake_lifecycle_configured",
        "voice_wake_lifecycle_is_active(&g_wake_lifecycle)",
        "if (!wake_runtime_active)",
        "if (g_state != VOICE_STATE_RECORDING)",
        "VOICE_WAKE_WORD_RUNTIME_ENABLED && hal_wake_word_is_supported()",
        "Wake word detection disabled for voice app runtime",
    };
    const char *const voice_wake_configure_only_required[] = {
        "static void wake_word_configure(void)",
        "if (g_wake_lifecycle_configured)",
        "wake_lifecycle_ensure_lock()",
        "voice_wake_lifecycle_init(&g_wake_lifecycle",
        "Wake word standby runtime configured; waiting for sleep standby resume",
        "static esp_err_t wake_word_resume_standby(const char *reason)",
        "ESP_ERR_NO_MEM",
        "button recording remains available",
        "Wake word standby runtime active",
    };
    const char *const voice_start_no_wake_resume_forbidden[] = {
        "voice_wake_lifecycle_resume(",
        "wake_word_resume_standby(",
        "Failed to initialize wake word detector",
    };
    const char *const voice_wake_lock_precreate_required[] = {
        "static bool wake_lifecycle_ensure_lock(void)",
        "xSemaphoreCreateRecursiveMutexStatic(&g_wake_lifecycle_lock_buffer)",
        "wake_lifecycle_ensure_lock()",
    };
    const char *const voice_init_precreates_wake_lock_required[] = {
        "VOICE_WAKE_WORD_RUNTIME_ENABLED",
        "wake_lifecycle_ensure_lock()",
    };
    const char *const voice_wake_configure_lock_required[] = {
        "wake_lifecycle_ensure_lock()",
        "voice_wake_lifecycle_init(&g_wake_lifecycle",
        "g_wake_lifecycle_configured = true;",
    };
    const char *const voice_wake_lock_callback_forbidden[] = {
        "xSemaphoreCreateRecursiveMutexStatic(&g_wake_lifecycle_lock_buffer)",
    };
    const char *const voice_listening_memory_thresholds_required[] = {
        "#define LISTENING_UI_MIN_INTERNAL_FREE_BYTES (48U * 1024U)",
        "#define LISTENING_UI_MIN_INTERNAL_LARGEST_BYTES (24U * 1024U)",
        "#define LISTENING_UI_MIN_DMA_LARGEST_BYTES (24U * 1024U)",
        "heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)",
    };
    const char *const voice_listening_low_memory_required[] = {
        "full_anim_headroom",
        "if (full_anim_headroom && use_wake_intro && !wake_transition_in_progress)",
        "if (full_anim_headroom)",
        "behavior_state_set_with_resources(listening_state, \"\", 0, NULL, \"\")",
        "behavior_state_set_with_resources(\"listening\", \"\", 24, \"\", \"\")",
        "Very low internal heap, skipping listening UI refresh",
    };
    const char *const voice_listening_state_gating_required[] = {
        "#include \"animation_service.h\"",
        "static bool current_display_is_awake_idle_variant(emoji_anim_type_t *current_out)",
        "static bool current_display_is_sleep_standby(emoji_anim_type_t current)",
        "animation_get_snapshot(&snapshot)",
        "*current_out = current;",
        "current == EMOJI_ANIM_STANDBY_1",
        "current == EMOJI_ANIM_STANDBY_4",
        "current == EMOJI_ANIM_STANDBY_LOOP",
        "emoji_anim_type_t current_emoji = EMOJI_ANIM_NONE;",
        "strcmp(current_behavior_state, \"listening_wake\") == 0",
        "const bool skip_wake_intro = current_display_is_awake_idle_variant(&current_emoji);",
        "voice_listening_ui_should_use_wake_intro(",
        "const char *listening_state = use_wake_intro ? \"listening_wake\" : \"listening\";",
        "Listening UI will skip wake intro from non-sleep state",
    };
    const char *const voice_listening_old_prefetch_forbidden[] = {
        "const char *listening_anim_override = skip_wake_intro ? \"listening\" : NULL;",
        "if (!skip_wake_intro)",
    };
    const char *const voice_remote_single_writer_required[] = {
        "#include \"voice_remote_control_core.h\"",
        "portMUX_TYPE g_remote_mailbox_lock",
        "voice_remote_mailbox_state_t g_remote_mailbox",
        "voice_remote_control_state_t g_remote_control",
        "voice_remote_mailbox_prepare_session",
        "voice_remote_mailbox_open_session",
        "voice_remote_mailbox_close_session",
        "voice_remote_mailbox_request_suspend",
        "voice_remote_control_apply(&g_remote_control",
        "static void handle_remote_control_snapshot(void)",
    };
    const char *const voice_remote_single_writer_forbidden[] = {
        "static volatile bool g_remote_recording_desired",
        "static volatile bool g_remote_apply_pending",
        "static volatile bool g_recording_permitted",
        "VOICE_EVENT_REMOTE_APPLY",
    };
    const char *const voice_suspend_caller_side_effects_forbidden[] = {
        "hal_audio_stop()",
        "vad_disable()",
        "g_state = VOICE_STATE_IDLE",
        "xQueueReset(g_event_queue)",
    };
    const char *const anim_storage_frame_validation_required[] = {
        "static bool anim_frame_desc_is_valid",
        "frame->size > payload_size - frame->offset",
        "uint64_t payload_size64 = (uint64_t)file_size - (uint64_t)header.payload_offset",
        "out_stream->payload_size = payload_size;",
        "stream->payload_size",
        "Invalid indexed frame size",
        "Invalid animpack bounds",
        "anim_frame_table_is_valid",
        "anim_frame_desc_is_valid(stream->info.name",
    };
    const char *const anim_pending_bad_frame_cancel_required[] = {
        "job->target == ANIM_LOAD_TARGET_PENDING",
        "Cancelling pending animation switch",
        "playback_cleanup(playback);",
        "restore_after_prepare_failure_locked();",
    };
    const char *const voice_wake_word_listening_order_required[] = {
        "voice_event_t event = VOICE_EVENT_WAKE_WORD;",
        "xQueueSend(g_event_queue, &event, 0)",
    };
    const char *const voice_wake_word_callback_forbidden[] = {
        "g_recording_triggered_by_wake_word",
        "voice_recorder_process_event(",
        "start_recording(",
    };
    const char *const voice_wake_word_event_handler_required[] = {
        "static void handle_wake_word_event(void)",
        "g_recording_triggered_by_wake_word = true;",
        "voice_recorder_process_event(VOICE_EVENT_WAKE_WORD);",
        "if (g_state == VOICE_STATE_RECORDING)",
        "show_listening_ui();",
        "g_recording_triggered_by_wake_word = false;",
        "show_cloud_not_ready_state();",
    };
    const char *const voice_sleep_wake_resume_required[] = {
        "static void voice_app_resume_wake_word_for_sleep_standby(const char *reason)",
        "active_app_is(\"voice.app\")",
        "voice_recorder_resume_wake_word_for_sleep();",
        "voice_app_resume_wake_word_for_sleep_standby(\"voice ready standby entry\")",
        "voice_app_resume_wake_word_for_sleep_standby(\"ready idle sleep\")",
        "voice_app_resume_wake_word_for_sleep_standby(\"ready idle lightweight standby\")",
    };
    const char *const voice_wake_lifecycle_feed_required[] = {
        "voice_wake_lifecycle_feed(&g_wake_lifecycle, samples, num_samples, &feed_size)",
    };
    const char *const voice_wake_lifecycle_feed_forbidden[] = {
        "hal_wake_word_feed(",
        "hal_wake_word_get_feed_size(",
        "wake_word_context(",
        "voice_wake_lifecycle_get_context(",
    };
    const char *const behavior_expression_event_dispatch_required[] = {
        "anim = event->anim[0] != '\\0' ? event->anim : NULL;",
        "text = event->text;",
        "font_size = event->font_size;",
        "behavior_copy_string(request->anim, sizeof(request->anim), anim);",
    };
    const char *const behavior_expression_event_dispatch_forbidden[] = {
        "behavior_capture_display_request_locked(request);",
    };
    const char *const voice_stop_recording_wait_required[] = {
        "show_thinking_expression_without_body_action();",
    };
    const char *const voice_stop_recording_animation_forbidden[] = {
        "show_thinking_ui_without_local_sfx();",
        "behavior_state_set_with_text(\"thinking\", \"\", 0);",
        "behavior_state_set_with_text(\"processing\", \"\", 0);",
    };
    const char *const ws_asr_result_log_only_required[] = {
        "!ws_event_ui_should_apply_asr_result()",
        "ASR result ignored for UI; recorder owns the thinking presentation",
    };
    const char *const ai_status_processing_mapping_required[] = {
        "return \"processing\";",
    };
    const char *const ws_tts_worker_internal_stack_required[] = {
        "task_ret = xTaskCreate(ws_tts_worker_task",
        "\"ws_tts_play\"",
        "WS_TTS_WORKER_STACK",
    };
    const char *const ws_tts_worker_psram_stack_forbidden[] = {
        "xTaskCreateWithCaps(ws_tts_worker_task",
        "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",
    };
    const char *const sfx_cloud_busy_no_lazy_init_required[] = {
        "void sfx_service_set_cloud_audio_busy(bool busy)",
        "if (!s_ctx.initialized)",
        "s_ctx.cloud_audio_busy = busy;",
        "return;",
    };
    const char *const sfx_voice_busy_no_lazy_init_required[] = {
        "void sfx_service_set_voice_audio_busy(bool busy)",
        "if (!s_ctx.initialized)",
        "s_ctx.voice_audio_busy = busy;",
        "return;",
    };
    const char *const sfx_cloud_busy_lazy_init_forbidden[] = {
        "if (busy && sfx_service_init() != ESP_OK)",
    };
    const char *const sfx_audio_blocker_required[] = {
        "static bool sfx_audio_blocked_locked(void)",
        "return s_ctx.cloud_audio_busy || s_ctx.voice_audio_busy;",
        "sfx_audio_blocked_locked()",
    };
    const char *const sfx_release_idle_after_local_playback_required[] = {
        "handoff_to_foreground_audio = sfx_is_audio_blocked();",
        "recording_audio_active = hal_audio_is_running() && !hal_audio_is_playback_mode();",
        "if (recording_audio_active)",
        "Keeping audio path for active recording after local sfx",
        "else if (handoff_to_foreground_audio)",
        "hal_audio_stop()",
        "hal_audio_release_idle()",
        "Failed to release audio path after local sfx",
    };
    const char *const voice_service_sfx_guard_required[] = {
        "#include \"sfx_service.h\"",
        "sfx_service_set_voice_audio_busy(true);",
        "sfx_service_stop();",
        "sfx_service_set_voice_audio_busy(false);",
    };
    const char *const voice_app_sfx_guard_required[] = {
        "sfx_service_set_voice_audio_busy(true);",
        "sfx_service_stop();",
    };
    const char *const voice_runtime_idle_sfx_release_order_required[] = {
        "if (voice_recorder_start() != 0)",
        "voice_recorder_request_startup_audio_release()",
        "s_cloud_runtime_started = true",
    };
    const char *const voice_runtime_direct_sfx_release_forbidden[] = {
        "sfx_service_set_voice_audio_busy(false);",
    };
    const char *const voice_task_startup_audio_release_order_required[] = {
        "handle_remote_control_snapshot();",
        "voice_release_startup_audio_guard_if_idle();",
        "xQueueReceive(g_event_queue, &event, 0)",
        "handle_remote_control_snapshot();",
    };
    const char *const voice_startup_audio_release_guard_order_required[] = {
        "if (g_state != VOICE_STATE_IDLE)",
        "if (g_startup_audio_release_pending)",
        "g_startup_audio_release_pending = false;",
        "sfx_service_set_voice_audio_busy(false);",
    };
    const char *const voice_startup_audio_release_request_required[] = {
        "esp_err_t voice_recorder_request_startup_audio_release(void)",
        "if (!g_task_running || g_voice_task_handle == NULL)",
        "g_startup_audio_release_pending = true;",
    };
    const char *const voice_app_sfx_release_required[] = {
        "voice_recorder_close();",
        "sfx_service_set_voice_audio_busy(false);",
    };
    const char *const sync_anim_current_project_required[] = {
        "def read_project_version()",
        "PROJECT_VER",
        "def default_source_dir()",
        "\"release\" / read_project_version() / \"sdcard\" / \"anim\"",
    };
    const char *const agent_audio_enqueue_gain_required[] = {
        "agent_audio_apply_gain_q8(s_slots[index].data, s_slots[index].len, AGENT_AUDIO_GAIN_Q8);",
    };
    const char *const agent_audio_tail_guard_order_required[] = {
        "write_tail_guard();",
        "hal_audio_enter_app_idle();",
    };
    const char *const behavior_load_sfx_preload_forbidden[] = {
        "sfx_service_reload();",
    };
    const char *const behavior_spiffs_mount_required[] = {
        "#include \"sensecap-watcher.h\"",
        "bsp_spiffs_init_default()",
        "Behavior SPIFFS init failed",
    };
    const char *const spiffs_mount_failure_visibility_required[] = {
        "esp_err_t register_ret;",
        "register_ret = esp_vfs_spiffs_register(&conf);",
        "SPIFFS mount failed: label=%s base_path=%s err=%s",
        "inited = true;",
    };
    const char *const behavior_missing_state_reload_required[] = {
        "static bool behavior_state_is_missing_from_catalog(const char *state_id)",
        "behavior_find_state_locked(state_id) == NULL",
        "Retrying behavior catalog load for missing state=%s",
        "behavior_state_load();",
    };
    const char *const behavior_action_active_snapshot_required[] = {
        "static volatile bool s_action_active_snapshot",
        "static volatile uint32_t s_action_active_until_ms",
        "static void behavior_update_action_active_snapshot_locked(uint32_t now_ms)",
        "return behavior_action_active_snapshot_is_active(behavior_now_ms());",
    };
    const char *const behavior_busy_snapshot_required[] = {
        "static volatile bool s_state_busy_snapshot",
        "static void behavior_update_state_busy_snapshot_locked(void)",
        "s_state_busy_snapshot =",
        "return sfx_service_is_busy() || s_state_busy_snapshot;",
    };
    const char *const behavior_action_active_lock_query_forbidden[] = {
        "behavior_lock_with_timeout(BEHAVIOR_QUERY_LOCK_TIMEOUT_MS)",
        "behavior_log_query_timeout_once(\"behavior_state_is_action_active\"",
    };
    const char *const behavior_busy_lock_query_forbidden[] = {
        "behavior_lock_with_timeout(BEHAVIOR_QUERY_LOCK_TIMEOUT_MS)",
        "behavior_log_query_timeout_once(\"behavior_state_is_busy\"",
    };
    const char *const ws_rx_fragment_psram_required[] = {
        "static void *ws_alloc_rx_buffer(size_t size)",
        "heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        "heap_caps_malloc(size, MALLOC_CAP_8BIT)",
        "s_text_fragment_state.buffer = (char *)ws_alloc_rx_buffer(total_len + 1U)",
        "s_binary_fragment_state.payload_buffer =",
        "(uint8_t *)ws_alloc_rx_buffer(s_binary_fragment_state.payload_len)",
    };
    const char *const ws_rx_fragment_internal_first_forbidden[] = {
        "s_text_fragment_state.buffer = (char *)calloc(total_len + 1U, 1U)",
        "s_binary_fragment_state.payload_buffer = (uint8_t *)malloc(s_binary_fragment_state.payload_len)",
    };
    const char *const wake_word_model_cache_required[] = {
        "static void wake_word_release_models(wake_word_ctx_t *ctx)",
        "ESP-SR mmap models intentionally stay cached",
        "ctx->models = NULL;",
        "wake_word_release_models(ctx);",
    };
    const char *const wake_word_deinit_model_forbidden[] = {
        "esp_srmodel_deinit(ctx->models);",
    };
    const char *const bsp_audio_codec_open_state_required[] = {
        "static bool play_dev_open",
        "static bool record_dev_open",
        "static esp_err_t bsp_codec_close_play_locked(void)",
        "if (!play_dev_open || play_dev_handle == NULL)",
        "play_dev_open = false;",
        "static esp_err_t bsp_codec_close_record_locked(void)",
        "if (!record_dev_open || record_dev_handle == NULL)",
        "record_dev_open = false;",
        "bsp_codec_close_play_locked();",
        "bsp_codec_close_record_locked();",
    };
    const char *const bsp_audio_i2s_disable_guard_required[] = {
        "if (play_dev_open && i2s_tx_chan != NULL)",
        "if (record_dev_open && i2s_rx_chan != NULL)",
    };
    const char *const sensecap_codec_override_required[] = {
        "espressif/esp_codec_dev:",
        "override_path: \"../esp_codec_dev\"",
    };
    const char *const codec_i2s_set_fmt_disable_guard_required[] = {
        "if ((dev_type & ESP_CODEC_DEV_TYPE_OUT) && i2s_data->out_enable)",
        "if ((dev_type & ESP_CODEC_DEV_TYPE_IN) && i2s_data->in_enable)",
    };
    const char *const sdkconfig_voice_resource_required[] = {
        "# CONFIG_ENABLE_WAKE_WORD is not set",
        "CONFIG_WATCHER_WS_TTS_QUEUE_DEPTH=64",
    };
    const char *const hal_display_text_lock_required[] = {
        "static bool hal_display_text_target_ready_locked(void)", "lv_obj_is_valid(label_text)", "lvgl_port_lock(0);",
        "if (!hal_display_text_target_ready_locked())",           "lvgl_port_unlock();",
    };
    const char *const hal_display_deinit_lock_required[] = {
        "lvgl_port_lock(0);",
        "hal_display_voice_connect_status_clear_locked();",
        "hal_display_release_behavior_screen_locked();",
        "img_emoji = NULL;",
        "label_text = NULL;",
        "text_overlay = NULL;",
        "lvgl_port_unlock();",
    };
    const char *const hal_display_voice_connect_public_clear_required[] = {
        "void hal_display_voice_connect_status_clear(void)",
        "if (voice_connect_panel == NULL)",
        "return;",
        "hal_display_voice_connect_status_clear_locked();",
    };
    const char *const settings_callback_refresh_tick_required[] = {
        "if (s_settings_pending_state_refresh)",
        "s_settings_pending_state_refresh = false;",
        "settings_fill_state(&state);",
        "factory_settings_ui_update_state(&state);",
    };
    const char *const settings_wifi_open_forbidden[] = {
        "settings_request_open(\"provision.app\");",
    };
    const char *const settings_git_about_state_required[] = {
        "const char *esp32_git_ref;",
        "const char *stm32_git_ref;",
        "const char *resource_bundle_version;",
    };
    const char *const settings_git_about_labels_required[] = {
        "lv_label_set_text(ui_snt1, \"ESP32 Branch :\");",
        "lv_label_set_text(ui_euit1, \"STM32 Branch :\");",
        "update_about_scrolling_label(ui_snt2, safe_text(s_state.about.esp32_git_ref, \"-\"));",
        "update_about_scrolling_label(ui_euit2, safe_text(s_state.about.stm32_git_ref, \"-\"));",
    };
    const char *const settings_git_about_scrolling_label_required[] = {
        "lv_label_get_long_mode(label) != LV_LABEL_LONG_SCROLL_CIRCULAR",
        "lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);",
        "const char *current_text = lv_label_get_text(label);",
        "current_text == NULL || strcmp(current_text, text) != 0",
        "lv_label_set_text(label, text);",
    };
    const char *const settings_git_about_labels_forbidden[] = {
        "lv_label_set_long_mode(ui_snt2, LV_LABEL_LONG_SCROLL_CIRCULAR);",
        "lv_label_set_long_mode(ui_euit2, LV_LABEL_LONG_SCROLL_CIRCULAR);",
        "lv_label_set_text(ui_snt2, safe_text(s_state.about.esp32_git_ref, \"-\"));",
        "lv_label_set_text(ui_euit2, safe_text(s_state.about.stm32_git_ref, \"-\"));",
    };
    const char *const settings_about_system_overview_required[] = {
        "#include \"app_center.h\"",
        "static void ensure_about_system_cards(void)",
        "create_about_system_card(\"Apps\", &ui_about_apps_value)",
        "create_about_system_card(\"Storage\", &ui_about_storage_value)",
        "create_about_system_card(\"Memory\", &ui_about_memory_value)",
        "create_about_system_card(\"OTA Slot\", &ui_about_ota_slot_value)",
        "create_about_system_card(\"Resource Bundle\", &ui_about_resource_bundle_value)",
        "s_state.about.resource_bundle_version",
        "lv_label_set_text(ui_about_apps_hint, \"Manage in App.Center\");",
        "app_center_get_cached_manager_snapshot(&snapshot)",
        "app_center_manager_format_app_count(snapshot.installed_count",
        "app_center_manager_format_bytes_pair(snapshot.spiffs_used_bytes",
        "heap_caps_get_info(&internal_info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);",
        "app_center_manager_format_ota_slot(snapshot.firmware_app_name",
    };
    const char *const settings_about_phone_control_variant_required[] = {
        "static void format_about_memory",
        "lv_label_set_text(ui_about_apps_hint, \"Managed by phone control\");",
        "snprintf(apps_text, sizeof(apps_text), \"Phone Control\");",
        "snprintf(ota_slot_text, sizeof(ota_slot_text), \"Firmware app\");",
        "format_about_memory(internal_info.total_free_bytes, internal_info.largest_free_block",
    };
    const char *const settings_resource_manifest_required[] = {
        "#define SETTINGS_RESOURCE_MANIFEST_PATH \"/sdcard/resource_manifest.json\"",
        "#define SETTINGS_RESOURCE_BUNDLE_FALLBACK \"Legacy / Unversioned\"",
        "static void settings_load_resource_bundle_version_once(void)",
        "cJSON_GetObjectItem(root, \"bundle_version\")",
        "state->about.resource_bundle_version = s_settings_resource_bundle_text;",
    };
    const char *const settings_about_sync_snapshot_forbidden[] = {
        "app_center_get_manager_snapshot(&snapshot)",
    };
    const char *const app_center_manager_api_required[] = {
        "esp_err_t app_center_get_manager_snapshot(app_center_manager_snapshot_t *out_snapshot);",
        "esp_err_t app_center_get_cached_manager_snapshot(app_center_manager_snapshot_t *out_snapshot);",
        "void app_center_request_manager_snapshot_refresh(void);",
        "esp_err_t app_center_uninstall_app(const char *app_id, const char *name, const char *command_id);",
    };
    const char *const app_center_manager_snapshot_cache_required[] = {
        "static app_center_manager_snapshot_t s_manager_snapshot_cache",
        "static bool s_manager_snapshot_cache_valid",
        "static bool s_manager_snapshot_refresh_pending",
        "static void app_center_manager_snapshot_refresh_task(void *arg)",
        "app_center_invalidate_manager_snapshot();",
    };
    const char *const app_center_firmware_install_match_required[] = {
        "static cJSON *app_center_load_package_metadata_json",
        "static bool app_center_read_package_firmware_metadata_string",
        "static bool app_center_firmware_install_matches_catalog",
        "Firmware app catalog version changed",
        "Firmware app catalog SHA changed",
        "Firmware app image version changed",
        "app_center_app_state_clear(entry->id);",
        "entry.installed = firmware_app ? app_center_firmware_install_matches_catalog(&entry)",
    };
    const char *const app_center_espnow_legacy_cleanup_required[] = {
        "#define APP_CENTER_LEGACY_ESPNOW_NAME_KEY \"espnowremote\"",
        "static bool app_center_legacy_espnow_name_matches",
        "static bool app_center_legacy_espnow_requested",
        "return app_center_legacy_espnow_id_matches(id) || app_center_legacy_espnow_name_matches(name);",
        "static void app_center_cleanup_legacy_espnow_remote_state",
        "app_center_remove_package_artifacts(legacy_ids[index]);",
        "Catalog app rejected: legacy remote app",
        "Package manifest legacy remote permission is not supported",
        "Desktop install rejected: legacy remote app",
    };
    const char *const app_center_legacy_ui_name_forbidden[] = {
        "ESP-NOW Remote",
    };
    const char *const app_center_espnow_builtin_forbidden[] = {
        "CONFIG_APP_CENTER_BUILTIN_REMOTE_PACKAGE_URL",
        "APP_CENTER_BUILTIN_REMOTE_PACKAGE_URL",
        "APP_CENTER_REMOTE_APP_ID",
        "APP_CENTER_REMOTE_RUNTIME_APP_ID",
        "app_center_add_builtin_remote_entry",
    };
    const char *const app_center_sample_espnow_forbidden[] = {
        "ESP-NOW Remote", "espnow-remote", "espnow_remote.pkg", "\"remote.app\"", "\"esp-now\"",
    };
    const char *const app_center_uninstall_confirm_required[] = {
        "app_center_manager_uninstall_policy(kind)",
        "policy.erase_ota_slot",
        "Confirm remove",
        "Tap Confirm remove to delete",
        "app_center_uninstall_entry_by_id",
    };
    const char *const app_center_manager_policy_required[] = {
        "policy.remove_package_files = false;",
        ".erase_ota_slot = false,",
        "removed. Slot will be overwritten",
    };
    const char *const app_center_exit_required[] = {
        "static void app_center_request_return_to_launcher",
        "static void app_center_request_download_cancel",
        "s_download_cancel_requested = true;",
        "app_center_cancel_http_client(&s_download_client, safe_reason);",
        "ota_service_cancel_active_request(safe_reason);",
        "s_return_launcher_requested = true;",
        "static void app_center_screen_event_cb",
        "dir == LV_DIR_TOP && app_center_exit_policy_gesture_reveals_exit",
        "app_center_show_exit_locked();",
        "lv_obj_add_event_cb(s_screen, app_center_screen_event_cb, LV_EVENT_GESTURE, NULL);",
        "if (s_group == NULL || lv_obj_get_group(s_exit_button) != s_group)",
        "lv_group_remove_obj(s_exit_button);",
        "app_center_exit_policy_button_action(app_center_exit_page_from_current(), exit_visible)",
        "app_center_request_return_to_launcher(\"button exit\")",
        "app_center_firmware_install_cancel_requested",
        "command_id == NULL ? app_center_firmware_install_cancel_requested : NULL",
        "ota_service_install_with_sha256_cancelable",
        "app_center_exit_policy_tick_action(s_return_launcher_requested, s_download_running)",
        "watcher_app_open(\"launcher\")",
    };
    const char *const app_center_exit_policy_required[] = {
        "APP_CENTER_EXIT_BUTTON_RETURN_TO_LAUNCHER", "APP_CENTER_EXIT_BUTTON_SHOW_EXIT",
        "APP_CENTER_EXIT_BUTTON_CANCEL_DOWNLOAD",    "APP_CENTER_EXIT_TICK_CANCEL_DOWNLOAD",
        "APP_CENTER_EXIT_TICK_RETURN_TO_LAUNCHER",   "bool app_center_exit_policy_gesture_reveals_exit",
    };
    const char *const app_center_exit_policy_test_required[] = {
        "test_visible_exit_button_always_returns_to_launcher",
        "test_wifi_setup_button_reveals_exit_before_returning",
        "test_list_and_detail_buttons_keep_page_actions_until_exit_is_visible",
        "test_install_button_cancels_download_until_exit_is_visible",
        "test_return_request_waits_for_download_cancellation",
        "test_top_gesture_reveals_exit_on_every_app_center_page",
    };
    const char *const app_center_wifi_style_status_ui_required[] = {
        "#include \"factory_home_ui/ui.h\"",
        "#define APP_CENTER_STYLE_GREEN 0x95F500",
        "#define APP_CENTER_STATUS_PANEL_W 400",
        "#define APP_CENTER_STATUS_PANEL_H 300",
        "#define APP_CENTER_STATUS_PANEL_Y -50",
        "#define APP_CENTER_STATUS_TEXT_W 350",
        "#define APP_CENTER_STATUS_BUTTON_W 184",
        "#define APP_CENTER_STATUS_BUTTON_H 42",
        "#define APP_CENTER_STATUS_ICON_Y -100",
        "#define APP_CENTER_STATUS_ICON_ZOOM 140",
        "#define APP_CENTER_STATUS_TITLE_Y -17",
        "#define APP_CENTER_STATUS_ACCENT_Y 16",
        "#define APP_CENTER_STATUS_BODY_Y 44",
        "static lv_obj_t *app_center_create_status_panel_locked",
        "static lv_obj_t *app_center_render_status_page_locked",
        "return \"Retrying apps\";",
        "return \"Waiting for Wi-Fi\";",
        "return \"Catalog not configured\";",
        "return \"Keep Wi-Fi connected while retrying.\";",
        "lv_obj_set_size(panel, APP_CENTER_STATUS_PANEL_W, APP_CENTER_STATUS_PANEL_H);",
        "lv_obj_align(panel, LV_ALIGN_CENTER, 0, APP_CENTER_STATUS_PANEL_Y);",
        "lv_img_set_src(icon, &ui_img_task_template_png);",
        "lv_img_set_zoom(icon, APP_CENTER_STATUS_ICON_ZOOM);",
        "lv_obj_set_style_img_recolor(icon, lv_color_hex(APP_CENTER_STYLE_GREEN), 0);",
        "lv_obj_align(icon, LV_ALIGN_CENTER, 0, APP_CENTER_STATUS_ICON_Y);",
        "s_status_title_label = app_center_create_status_label_locked(panel, title, APP_CENTER_STATUS_TITLE_Y,",
        "&lv_font_montserrat_24, APP_CENTER_STYLE_WHITE,",
        "app_center_create_status_label_locked(panel, accent, APP_CENTER_STATUS_ACCENT_Y,",
        "s_status_body_label = app_center_create_status_label_locked(panel, body, APP_CENTER_STATUS_BODY_Y,",
        "&lv_font_montserrat_20, APP_CENTER_STYLE_WHITE,",
        "lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);",
        "lv_obj_set_size(s_install_progress_bar, APP_CENTER_STATUS_PROGRESS_W, APP_CENTER_STATUS_PROGRESS_H);",
        "app_center_render_remote_status_locked();",
        "app_center_render_status_page_locked(entry->name, s_download_status, app_center_download_status_color()",
        "lv_obj_set_style_bg_img_src(s_exit_button, &ui_img_button_cancel_png, 0);",
    };
    const char *const app_center_list_detail_text_style_required[] = {
        "#define APP_CENTER_CARD_Y 118",
        "#define APP_CENTER_LIST_TITLE_Y 36",
        "#define APP_CENTER_LIST_STATUS_Y 68",
        "#define APP_CENTER_CARD_TEXT_X 16",
        "#define APP_CENTER_CARD_TEXT_W 174",
        "#define APP_CENTER_DETAIL_TEXT_X 24",
        "#define APP_CENTER_DETAIL_TEXT_W 188",
        "#define APP_CENTER_CARD_NAME_Y 17",
        "#define APP_CENTER_CARD_STATE_Y 55",
        "#define APP_CENTER_DETAIL_NAME_Y 16",
        "#define APP_CENTER_DETAIL_STATE_Y 48",
        "#define APP_CENTER_DETAIL_BODY_Y 76",
        "#define APP_CENTER_DETAIL_META_Y 120",
        "static const char *app_center_entry_state_text",
        "static uint32_t app_center_entry_state_color",
        "return \"Ready to download\";",
        "return \"Catalog loaded\";",
        "lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);",
        "lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);",
        "lv_obj_set_style_pad_all(button, 0, 0);",
        "lv_label_set_text(name, entry->name);",
        "lv_obj_set_size(name, APP_CENTER_CARD_TEXT_W, 30);",
        "lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);",
        "lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);",
        "lv_obj_align(name, LV_ALIGN_TOP_LEFT, APP_CENTER_CARD_TEXT_X, APP_CENTER_CARD_NAME_Y);",
        "lv_label_set_text(state, app_center_entry_state_text(entry));",
        "lv_obj_set_size(state, APP_CENTER_CARD_TEXT_W, 24);",
        "lv_obj_set_style_text_font(state, &lv_font_montserrat_20, 0);",
        "lv_obj_align(state, LV_ALIGN_TOP_LEFT, APP_CENTER_CARD_TEXT_X, APP_CENTER_CARD_STATE_Y);",
        "lv_label_set_text(remote, app_center_list_status_text());",
        "app_center_render_header_locked(\"App details\");",
        "lv_obj_set_size(detail, APP_CENTER_DETAIL_TEXT_W, APP_CENTER_DETAIL_BODY_H);",
        "lv_obj_set_style_text_font(detail, &lv_font_montserrat_20, 0);",
        "lv_obj_align(detail, LV_ALIGN_TOP_LEFT, APP_CENTER_DETAIL_TEXT_X, APP_CENTER_DETAIL_BODY_Y);",
    };
    const char *const app_center_ui_snapshot_required[] = {
        "void app_center_debug_log_ui_snapshot(const char *stage)",
        "static bool app_center_area_fits_round_screen_locked",
        "evt=app_center_ui_snapshot",
        "evt=app_center_ui_obj",
        "name=%s present=1 hidden=%d x1=%ld y1=%ld x2=%ld y2=%ld w=%ld h=%ld",
        "round_check=%d round_fit=%d",
        "app_center_debug_log_obj_locked(stage, \"status_icon\", s_status_icon, true);",
        "app_center_debug_log_obj_locked(stage, \"status_title\", s_status_title_label, true);",
        "app_center_debug_log_obj_locked(stage, \"status_accent\", s_install_status_label, true);",
        "app_center_debug_log_obj_locked(stage, \"status_body\", s_status_body_label, true);",
        "app_center_debug_log_obj_locked(stage, \"progress_bar\", s_install_progress_bar, true);",
        "app_center_debug_log_obj_locked(stage, \"download_button\", s_download_cancel_button, true);",
        "app_center_debug_log_obj_locked(stage, \"list_card_name\", s_list_card_name_label, true);",
        "app_center_debug_log_obj_locked(stage, \"detail_body\", s_detail_body_label, true);",
    };
    const char *const app_center_debug_status_hook_required[] = {
        "app_center_debug_log_ui_snapshot(stage);",
        "strcmp(active_app->id, \"app.center\") == 0",
    };
    const char *const ota_service_cancel_header_required[] = {
        "typedef bool (*ota_service_cancel_cb_t)(void *user_ctx);",
        "ota_service_install_with_sha256_cancelable",
        "ota_service_cancel_active_request",
    };
    const char *const ota_service_cancel_required[] = {
        "#include \"freertos/semphr.h\"",
        "static esp_http_client_handle_t s_active_client",
        "static bool ota_service_cancel_requested",
        "esp_err_t ota_service_cancel_active_request",
        "esp_http_client_cancel_request(client)",
        "ota_service_install_with_sha256_cancelable",
        "ota_service_return_canceled(status_cb, user_ctx)",
        "ota_service_clear_active_client(client)",
        "return ota_service_install_with_sha256_cancelable",
    };
    const char *const settings_git_build_info_required[] = {
        "#include \"watcher_build_info.h\"",
        "settings_format_git_ref(s_settings_esp32_git_text",
        "WATCHER_BUILD_GIT_BRANCH",
        "WATCHER_BUILD_GIT_COMMIT",
        "settings_update_stm32_git_ref_from_link(link);",
        "mcu_link_copy_peer_info(link, &info)",
    };
    const char *const mcu_link_git_tlv_constants_required[] = {
        "MCU_HELLO_TLV_GIT_BRANCH",
        "MCU_HELLO_TLV_GIT_COMMIT",
        "MCU_HELLO_TLV_GIT_DIRTY",
    };
    const char *const mcu_link_git_metadata_required[] = {
        "MCU_HELLO_TLV_GIT_BRANCH",           "MCU_HELLO_TLV_GIT_COMMIT", "MCU_HELLO_TLV_GIT_DIRTY",
        "mcu_link_parse_hello_rsp_peer_info", "mcu_link_copy_peer_info",
    };
    const char *const esp32_build_info_cmake_required[] = {
        "watcher_build_info_gen",
        "generate_build_info.py",
        "WATCHER_BUILD_INFO_HEADER",
        "target_include_directories(${COMPONENT_LIB} PRIVATE",
    };
    const char *const sdkconfig_touch_perf_required[] = {
        "CONFIG_FREERTOS_HZ=1000",
        "CONFIG_LV_MEM_BUF_MAX_NUM=32",
        "CONFIG_BSP_LCD_PANEL_SPI_TRANS_Q_DEPTH=1",
        "CONFIG_BSP_LCD_SPI_DMA_SIZE_DIV=16",
        "CONFIG_LVGL_DRAW_BUFF_HEIGHT=412",
        "CONFIG_LVGL_PORT_TASK_PRIORITY=11",
        "CONFIG_LVGL_PORT_TASK_AFFINITY_CPU1=y",
        "# CONFIG_LVGL_PORT_TASK_STACK_ALLOC_EXTERNAL is not set",
        "CONFIG_LVGL_INPUT_DEVICE_SENSITIVITY=-1",
        "CONFIG_LVGL_PORT_TASK_MAX_SLEEP_MS=5",
        "CONFIG_LVGL_PORT_TIMER_PERIOD_MS=5",
        "CONFIG_LV_DISP_DEF_REFR_PERIOD=30",
        "CONFIG_LV_INDEV_DEF_READ_PERIOD=30",
    };
    const char *const lvgl_indev_factory_thresholds[] = {
        "#define LV_INDEV_DEF_SCROLL_LIMIT         10",  "#define LV_INDEV_DEF_SCROLL_THROW         10",
        "#define LV_INDEV_DEF_LONG_PRESS_TIME      400", "#define LV_INDEV_DEF_LONG_PRESS_REP_TIME  100",
        "#define LV_INDEV_DEF_GESTURE_LIMIT        50",  "#define LV_INDEV_DEF_GESTURE_MIN_VELOCITY 3",
    };
    const char *const watcher_kconfig_factory_defaults[] = {
        "config LVGL_DRAW_BUFF_HEIGHT",
        "default 412",
        "config LVGL_PORT_TIMER_PERIOD_MS",
        "default 5",
    };
    const char *const touch_component_versions_required[] = {
        "espressif/esp_lcd_spd2010:",
        "version: \"1.0.1\"",
        "espressif/esp_lcd_touch_spd2010:",
        "version: \"0.0.1\"",
    };
    const char *const dependency_lock_versions_required[] = {
        "lvgl/lvgl:",
        "version: 8.4.0",
        "espressif/esp_lcd_spd2010:",
        "version: 1.0.1",
        "espressif/esp_lcd_touch_spd2010:",
        "version: 0.0.1",
    };
    const char *const esp_lvgl_port_component_required[] = {
        "description: ESP LVGL port with custom encoder button event callback support",
        "url: https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port",
        "version: 1.4.0-custom.1",
    };
    const char *const hal_display_bsp_input_required[] = {
        "static void hal_display_capture_bsp_inputs(void)",
        "s_touch_handle = bsp_lcd_get_touch_handle();",
        "while ((indev = lv_indev_get_next(indev)) != NULL)",
        "if (type == LV_INDEV_TYPE_ENCODER && s_knob_indev == NULL)",
        "s_knob_indev = indev;",
        "else if (type == LV_INDEV_TYPE_POINTER && s_touch_indev == NULL)",
        "s_touch_indev = indev;",
        "inputs_initialized = (s_knob_indev != NULL) || (s_touch_indev != NULL);",
        "s_display = bsp_lvgl_init();",
        "hal_display_capture_bsp_inputs();",
    };
    const char *const forbidden_hal_touch_registration[] = {
        "lvgl_port_add_touch(",
        "lv_indev_drv_register",
    };

    failures += file_contains_all(root, "../components/esp_lvgl_port/esp_lvgl_port.c", touch_required,
                                  sizeof(touch_required) / sizeof(touch_required[0]));
    failures += file_contains_all(root, "../components/sensecap-watcher/sensecap-watcher.c", touch_bsp_init_required,
                                  sizeof(touch_bsp_init_required) / sizeof(touch_bsp_init_required[0]));
    failures += file_contains_all(root, "../components/esp_lvgl_port/esp_lvgl_port.c", lvgl_task_required,
                                  sizeof(lvgl_task_required) / sizeof(lvgl_task_required[0]));
    failures += file_contains_all(root, "../components/esp_lvgl_port/esp_lvgl_port.c", lvgl_flush_required,
                                  sizeof(lvgl_flush_required) / sizeof(lvgl_flush_required[0]));
    failures += file_contains_all(root, "../components/esp_lvgl_port/esp_lvgl_port.c", lvgl_direct_draw_wait_required,
                                  sizeof(lvgl_direct_draw_wait_required) / sizeof(lvgl_direct_draw_wait_required[0]));
    failures +=
        file_contains_none(root, "../components/esp_lvgl_port/esp_lvgl_port.c", forbidden_non_factory_touch_logic,
                           sizeof(forbidden_non_factory_touch_logic) / sizeof(forbidden_non_factory_touch_logic[0]));
    failures +=
        file_contains_none(root, "factory_settings_factory_events.c", forbidden_non_factory_touch_logic,
                           sizeof(forbidden_non_factory_touch_logic) / sizeof(forbidden_non_factory_touch_logic[0]));
    failures +=
        file_contains_none(root, "launcher_factory_home.c", forbidden_shell_touch_suppression,
                           sizeof(forbidden_shell_touch_suppression) / sizeof(forbidden_shell_touch_suppression[0]));
    failures +=
        file_contains_none(root, "factory_settings_ui.c", forbidden_shell_touch_suppression,
                           sizeof(forbidden_shell_touch_suppression) / sizeof(forbidden_shell_touch_suppression[0]));
    failures +=
        file_contains_none(root, "factory_settings_factory_events.c", forbidden_shell_touch_suppression,
                           sizeof(forbidden_shell_touch_suppression) / sizeof(forbidden_shell_touch_suppression[0]));
    failures += file_contains_all(root, "launcher_factory_home.c", home_gesture_required,
                                  sizeof(home_gesture_required) / sizeof(home_gesture_required[0]));
    failures += file_contains_all(root, "launcher_factory_home.c", home_button_event_required,
                                  sizeof(home_button_event_required) / sizeof(home_button_event_required[0]));
    failures += file_contains_all(root, "launcher_factory_home.c", home_scroll_required,
                                  sizeof(home_scroll_required) / sizeof(home_scroll_required[0]));
    failures += file_contains_all(root, "launcher_factory_home.c", home_object_tree_required,
                                  sizeof(home_object_tree_required) / sizeof(home_object_tree_required[0]));
    failures += file_contains_all(root, "launcher_factory_home.c", home_event_binding_required,
                                  sizeof(home_event_binding_required) / sizeof(home_event_binding_required[0]));
    failures += function_contains_all(
        root, "launcher_factory_home.c", "void launcher_factory_home_update_status", home_wifi_status_icon_required,
        sizeof(home_wifi_status_icon_required) / sizeof(home_wifi_status_icon_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c", settings_gesture_required,
                                  sizeof(settings_gesture_required) / sizeof(settings_gesture_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c", settings_click_required,
                                  sizeof(settings_click_required) / sizeof(settings_click_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_click_focused_required,
                                  sizeof(settings_click_focused_required) / sizeof(settings_click_focused_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c", settings_visible_focus_events_required,
                                  sizeof(settings_visible_focus_events_required) /
                                      sizeof(settings_visible_focus_events_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c", settings_visible_focus_style_required,
                                  sizeof(settings_visible_focus_style_required) /
                                      sizeof(settings_visible_focus_style_required[0]));
    failures += file_contains_all(root, "factory_settings_ui_internal.h", settings_rgb_toggle_debounce_required,
                                  sizeof(settings_rgb_toggle_debounce_required) /
                                      sizeof(settings_rgb_toggle_debounce_required[0]));
    failures += function_contains_all(root, "factory_settings_factory_events.c", "void setrgbc_cb",
                                      settings_rgb_toggle_required,
                                      sizeof(settings_rgb_toggle_required) / sizeof(settings_rgb_toggle_required[0]));
    failures +=
        file_contains_all(root, "factory_settings_factory_events.c", settings_slider_live_apply_required,
                          sizeof(settings_slider_live_apply_required) / sizeof(settings_slider_live_apply_required[0]));
    failures += file_contains_all(root, "factory_settings_ui_internal.h", settings_slider_selection_colors_required,
                                  sizeof(settings_slider_selection_colors_required) /
                                      sizeof(settings_slider_selection_colors_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c",
                                  settings_slider_persistent_outer_selection_required,
                                  sizeof(settings_slider_persistent_outer_selection_required) /
                                      sizeof(settings_slider_persistent_outer_selection_required[0]));
    failures += file_contains_none(root, "factory_settings_original_ui/screens/ui_Page_Slider.c",
                                   settings_slider_panel_focused_border_forbidden,
                                   sizeof(settings_slider_panel_focused_border_forbidden) /
                                       sizeof(settings_slider_panel_focused_border_forbidden[0]));
    failures +=
        file_contains_all(root, "factory_settings_factory_events.c", settings_slider_default_second_layer_required,
                          sizeof(settings_slider_default_second_layer_required) /
                              sizeof(settings_slider_default_second_layer_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_slider_touch_editing_required,
                                  sizeof(settings_slider_touch_editing_required) /
                                      sizeof(settings_slider_touch_editing_required[0]));
    failures += file_contains_all(
        root, "factory_settings_factory_events.c", settings_slider_touch_event_binding_required,
        sizeof(settings_slider_touch_event_binding_required) / sizeof(settings_slider_touch_event_binding_required[0]));
    failures += function_contains_none(
        root, "factory_settings_factory_events.c", "void voldf_cb", settings_slider_defocus_must_not_hide_outer,
        sizeof(settings_slider_defocus_must_not_hide_outer) / sizeof(settings_slider_defocus_must_not_hide_outer[0]));
    failures += function_contains_none(
        root, "factory_settings_factory_events.c", "void bridf_cb", settings_slider_defocus_must_not_hide_outer,
        sizeof(settings_slider_defocus_must_not_hide_outer) / sizeof(settings_slider_defocus_must_not_hide_outer[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_group_order_required,
                                  sizeof(settings_group_order_required) / sizeof(settings_group_order_required[0]));
    failures += function_contains_all(
        root, "factory_settings_ui.c", "void ensure_page_initialized", settings_lazy_page_init_required,
        sizeof(settings_lazy_page_init_required) / sizeof(settings_lazy_page_init_required[0]));
    failures += function_contains_none(
        root, "factory_settings_ui.c", "void factory_settings_ui_build", settings_entry_preload_forbidden,
        sizeof(settings_entry_preload_forbidden) / sizeof(settings_entry_preload_forbidden[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_entry_screen_handoff_required,
                                  sizeof(settings_entry_screen_handoff_required) /
                                      sizeof(settings_entry_screen_handoff_required[0]));
    failures += function_contains_none(root, "factory_settings_ui.c", "void open_set_page(void)",
                                       settings_public_page_open_auto_delete_forbidden,
                                       sizeof(settings_public_page_open_auto_delete_forbidden) /
                                           sizeof(settings_public_page_open_auto_delete_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void app_ui_diag_log_lvgl_locked", app_ui_screen_count_diag_required,
        sizeof(app_ui_screen_count_diag_required) / sizeof(app_ui_screen_count_diag_required[0]));
    failures += file_contains_all(root, "app_main.c", launcher_screen_cache_required,
                                  sizeof(launcher_screen_cache_required) / sizeof(launcher_screen_cache_required[0]));
    failures += file_contains_all(root, "app_main.c", client_launcher_fusion_required,
                                  sizeof(client_launcher_fusion_required) / sizeof(client_launcher_fusion_required[0]));
    failures +=
        file_contains_none(root, "app_main.c", old_launcher_dual_entry_forbidden,
                           sizeof(old_launcher_dual_entry_forbidden) / sizeof(old_launcher_dual_entry_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", discovery_cancel_wait_required,
                                  sizeof(discovery_cancel_wait_required) / sizeof(discovery_cancel_wait_required[0]));
    failures += file_contains_all(
        root, "../components/hal/hal_display/src/hal_display.c", hal_display_retain_previous_screen_required,
        sizeof(hal_display_retain_previous_screen_required) / sizeof(hal_display_retain_previous_screen_required[0]));
    failures +=
        file_contains_all(root, "../tools/verify_ui_ram_lifecycle.py", ui_ram_lifecycle_verifier_required,
                          sizeof(ui_ram_lifecycle_verifier_required) / sizeof(ui_ram_lifecycle_verifier_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.h", settings_wifi_api_required,
                                  sizeof(settings_wifi_api_required) / sizeof(settings_wifi_api_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_network_page_required,
                                  sizeof(settings_network_page_required) / sizeof(settings_network_page_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_network_disconnect_spacing_required,
                                  sizeof(settings_network_disconnect_spacing_required) /
                                      sizeof(settings_network_disconnect_spacing_required[0]));
    failures += file_contains_all(
        root, "../components/protocols/ble_service/include/ble_service.h", ble_wifi_config_callback_api_required,
        sizeof(ble_wifi_config_callback_api_required) / sizeof(ble_wifi_config_callback_api_required[0]));
    failures += file_contains_all(
        root, "../components/protocols/ble_service/src/ble_service.c", ble_wifi_config_callback_required,
        sizeof(ble_wifi_config_callback_required) / sizeof(ble_wifi_config_callback_required[0]));
    failures +=
        file_contains_all(root, "../components/protocols/ble_service/src/ble_service.c", ble_wifi_json_connect_required,
                          sizeof(ble_wifi_json_connect_required) / sizeof(ble_wifi_json_connect_required[0]));
    failures +=
        file_contains_none(root, "../components/protocols/ble_service/src/ble_service.c", ble_wifi_json_store_forbidden,
                           sizeof(ble_wifi_json_store_forbidden) / sizeof(ble_wifi_json_store_forbidden[0]));
    failures += file_contains_all(
        root, "../components/protocols/ble_service/src/ble_service.c", ble_wifi_legacy_connect_required,
        sizeof(ble_wifi_legacy_connect_required) / sizeof(ble_wifi_legacy_connect_required[0]));
    failures += file_contains_none(
        root, "../components/protocols/ble_service/src/ble_service.c", ble_wifi_legacy_store_forbidden,
        sizeof(ble_wifi_legacy_store_forbidden) / sizeof(ble_wifi_legacy_store_forbidden[0]));
    failures +=
        file_contains_all(root, "../components/protocols/ble_service/src/ble_service.c", ble_write_boundary_required,
                          sizeof(ble_write_boundary_required) / sizeof(ble_write_boundary_required[0]));
    failures += file_contains_all(
        root, "../components/protocols/ble_service/src/ble_service.c", ble_disconnect_restart_advertising_required,
        sizeof(ble_disconnect_restart_advertising_required) / sizeof(ble_disconnect_restart_advertising_required[0]));
    failures +=
        file_contains_all(root, "../components/protocols/ble_service/src/ble_service.c", ble_light_control_required,
                          sizeof(ble_light_control_required) / sizeof(ble_light_control_required[0]));
    failures +=
        file_contains_all(root, "../components/protocols/ble_service/CMakeLists.txt", ble_light_component_required,
                          sizeof(ble_light_component_required) / sizeof(ble_light_component_required[0]));
    failures +=
        file_contains_all(root, "../components/utils/wifi_manager/src/wifi_manager.c", wifi_open_network_required,
                          sizeof(wifi_open_network_required) / sizeof(wifi_open_network_required[0]));
    failures += file_contains_all(
        root, "../components/utils/wifi_manager/src/wifi_manager.c", wifi_manager_provisioning_init_required,
        sizeof(wifi_manager_provisioning_init_required) / sizeof(wifi_manager_provisioning_init_required[0]));
    failures += function_contains_all(root, "../components/utils/wifi_manager/src/wifi_manager.c", "int wifi_provision",
                                      wifi_provision_init_required,
                                      sizeof(wifi_provision_init_required) / sizeof(wifi_provision_init_required[0]));
    failures += function_contains_all(root, "../components/utils/wifi_manager/src/wifi_manager.c",
                                      "int wifi_store_credentials", wifi_store_init_required,
                                      sizeof(wifi_store_init_required) / sizeof(wifi_store_init_required[0]));
    failures += function_contains_all(root, "../components/utils/wifi_manager/src/wifi_manager.c",
                                      "int wifi_clear_credentials", wifi_clear_init_required,
                                      sizeof(wifi_clear_init_required) / sizeof(wifi_clear_init_required[0]));
    failures += file_contains_all(root, "app_main.c", provisioning_feedback_required,
                                  sizeof(provisioning_feedback_required) / sizeof(provisioning_feedback_required[0]));
    failures += function_contains_all(root, "factory_settings_factory_events.c", "void setwific_cb",
                                      settings_wifi_event_required,
                                      sizeof(settings_wifi_event_required) / sizeof(settings_wifi_event_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_return_focus_required,
                                  sizeof(settings_return_focus_required) / sizeof(settings_return_focus_required[0]));
    failures += file_contains_all(root, "factory_settings_factory_events.c", settings_secondary_entry_focus_required,
                                  sizeof(settings_secondary_entry_focus_required) /
                                      sizeof(settings_secondary_entry_focus_required[0]));
    failures +=
        function_contains_all(root, "factory_settings_ui.c", "static void lv_pm_obj_group", settings_pm_group_required,
                              sizeof(settings_pm_group_required) / sizeof(settings_pm_group_required[0]));
    failures += patterns_in_order(root, "factory_settings_ui.c", settings_pm_open_order_required,
                                  sizeof(settings_pm_open_order_required) / sizeof(settings_pm_open_order_required[0]));
    failures += patterns_in_order(root, "launcher_factory_home.c", launcher_group_order_required,
                                  sizeof(launcher_group_order_required) / sizeof(launcher_group_order_required[0]));
    failures += file_contains_all(root, "app_main.c", launcher_phone_control_required,
                                  sizeof(launcher_phone_control_required) / sizeof(launcher_phone_control_required[0]));
    failures += file_contains_none(root, "app_main.c", launcher_app_center_entry_forbidden,
                                   sizeof(launcher_app_center_entry_forbidden) /
                                       sizeof(launcher_app_center_entry_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", sdk_control_app_required,
                                  sizeof(sdk_control_app_required) / sizeof(sdk_control_app_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_version_required,
                                  sizeof(sdk_control_version_required) / sizeof(sdk_control_version_required[0]));
    failures += file_contains_all(root, "CMakeLists.txt", sdk_control_ui_component_required, 1U);
    failures += file_contains_all(root, "sdk_control_ui.h", sdk_control_ui_component_required + 2, 5U);
    failures +=
        file_contains_all(root, "sdk_control_app.h", sdk_control_semantic_ui_required,
                          sizeof(sdk_control_semantic_ui_required) / sizeof(sdk_control_semantic_ui_required[0]));
    failures += file_contains_none(root, "sdk_control_app.h", sdk_control_legacy_text_ui_forbidden, 1U);
    failures += file_contains_none(root, "app_main.c", sdk_control_legacy_text_ui_forbidden + 1, 1U);
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_display_lifecycle_required,
                                  sizeof(sdk_control_display_lifecycle_required) /
                                      sizeof(sdk_control_display_lifecycle_required[0]));
    failures += file_contains_all(root, "sdk_control_ui.c", sdk_control_ui_visual_contract_required,
                                  sizeof(sdk_control_ui_visual_contract_required) /
                                      sizeof(sdk_control_ui_visual_contract_required[0]));
    failures +=
        file_contains_all(root, "sdk_control_ui.c", sdk_control_ui_input_debug_required,
                          sizeof(sdk_control_ui_input_debug_required) / sizeof(sdk_control_ui_input_debug_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_app_input_debug_required,
                                  sizeof(sdk_control_app_input_debug_required) /
                                      sizeof(sdk_control_app_input_debug_required[0]));
    failures += file_contains_all(root, "app_main.c", sdk_control_main_input_debug_required,
                                  sizeof(sdk_control_main_input_debug_required) /
                                      sizeof(sdk_control_main_input_debug_required[0]));
    failures += file_contains_all(root, "sdk_control_ui.c", sdk_control_ui_vector_emblem_required,
                                  sizeof(sdk_control_ui_vector_emblem_required) /
                                      sizeof(sdk_control_ui_vector_emblem_required[0]));
    failures += file_contains_none(root, "sdk_control_ui.c", sdk_control_ui_font_symbol_forbidden,
                                   sizeof(sdk_control_ui_font_symbol_forbidden) /
                                       sizeof(sdk_control_ui_font_symbol_forbidden[0]));
    failures += file_contains_all(root, "sdk_control_ui.c", sdk_control_ui_device_calibration_required,
                                  sizeof(sdk_control_ui_device_calibration_required) /
                                      sizeof(sdk_control_ui_device_calibration_required[0]));
    failures += file_contains_all(root, "sdk_control_ui.c", sdk_control_ui_original_layout_required,
                                  sizeof(sdk_control_ui_original_layout_required) /
                                      sizeof(sdk_control_ui_original_layout_required[0]));
    failures += file_contains_all(root, "factory_home_ui/images/ui_img_python_sdk_focused_png.c",
                                  sdk_control_launcher_focused_icon_layers_required,
                                  sizeof(sdk_control_launcher_focused_icon_layers_required) /
                                      sizeof(sdk_control_launcher_focused_icon_layers_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_launcher_metadata_required,
                                  sizeof(sdk_control_launcher_metadata_required) /
                                      sizeof(sdk_control_launcher_metadata_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_network_lifecycle_required,
                                  sizeof(sdk_control_network_lifecycle_required) /
                                      sizeof(sdk_control_network_lifecycle_required[0]));
    failures +=
        file_contains_all(root, "sdk_control_app.c", sdk_control_hello_recovery_required,
                          sizeof(sdk_control_hello_recovery_required) / sizeof(sdk_control_hello_recovery_required[0]));
    failures += function_contains_all(
        root, "sdk_control_app.c", "static void sdk_control_on_tick(void)", sdk_control_hello_recovery_tick_required,
        sizeof(sdk_control_hello_recovery_tick_required) / sizeof(sdk_control_hello_recovery_tick_required[0]));
    failures += file_contains_all(root, "../components/sdk/watcher_sdk/src/watcher_sdk_discovery.c",
                                  sdk_discovery_pairing_required,
                                  sizeof(sdk_discovery_pairing_required) / sizeof(sdk_discovery_pairing_required[0]));
    failures += file_contains_none(
        root, "../components/sdk/watcher_sdk/src/watcher_sdk_discovery.c", sdk_discovery_pairing_forbidden,
        sizeof(sdk_discovery_pairing_forbidden) / sizeof(sdk_discovery_pairing_forbidden[0]));
    failures += patterns_in_order(root, "sdk_control_app.c", sdk_control_network_exit_order_required,
                                  sizeof(sdk_control_network_exit_order_required) /
                                      sizeof(sdk_control_network_exit_order_required[0]));
    failures +=
        patterns_in_order(root, "sdk_control_app.c", sdk_control_ws_setup_order_required,
                          sizeof(sdk_control_ws_setup_order_required) / sizeof(sdk_control_ws_setup_order_required[0]));
    failures += file_contains_all(
        root, "../components/protocols/ws_client/src/ws_client.c", ws_client_router_bootstrap_required,
        sizeof(ws_client_router_bootstrap_required) / sizeof(ws_client_router_bootstrap_required[0]));
    failures += file_contains_none(root, "app_main.c", app_main_router_bootstrap_forbidden,
                                   sizeof(app_main_router_bootstrap_forbidden) /
                                       sizeof(app_main_router_bootstrap_forbidden[0]));
    failures += file_contains_none(root, "sdk_control_app.c", sdk_control_network_lifecycle_forbidden,
                                   sizeof(sdk_control_network_lifecycle_forbidden) /
                                       sizeof(sdk_control_network_lifecycle_forbidden[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_protocol_diagnostics_required,
                                  sizeof(sdk_control_protocol_diagnostics_required) /
                                      sizeof(sdk_control_protocol_diagnostics_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_handler_lifecycle_required,
                                  sizeof(sdk_control_handler_lifecycle_required) /
                                      sizeof(sdk_control_handler_lifecycle_required[0]));
    failures += file_contains_all(root, "sdk_control_app.h", sdk_control_surface_contract_required,
                                  sizeof(sdk_control_surface_contract_required) /
                                      sizeof(sdk_control_surface_contract_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_surface_preparation_required,
                                  sizeof(sdk_control_surface_preparation_required) /
                                      sizeof(sdk_control_surface_preparation_required[0]));
    failures += file_contains_all(root, "sdk_control_app.c", sdk_control_audio_stream_preemption_required,
                                  sizeof(sdk_control_audio_stream_preemption_required) /
                                      sizeof(sdk_control_audio_stream_preemption_required[0]));
    failures += file_contains_all(root, "debug_cli.c", sdk_control_debug_pairing_cli_required,
                                  sizeof(sdk_control_debug_pairing_cli_required) /
                                      sizeof(sdk_control_debug_pairing_cli_required[0]));
    failures += file_contains_all(root, "app_main.c", sdk_control_surface_binding_required,
                                  sizeof(sdk_control_surface_binding_required) /
                                      sizeof(sdk_control_surface_binding_required[0]));
    failures += file_contains_all(
        root, "../components/sdk/watcher_sdk/src/watcher_sdk.c", sdk_microphone_audio_release_required,
        sizeof(sdk_microphone_audio_release_required) / sizeof(sdk_microphone_audio_release_required[0]));
    failures += file_contains_all(root, "../components/sdk/watcher_sdk/src/watcher_sdk.c", sdk_camera_async_required,
                                  sizeof(sdk_camera_async_required) / sizeof(sdk_camera_async_required[0]));
    failures += function_contains_none(root, "../components/sdk/watcher_sdk/src/watcher_sdk.c",
                                       "watcher_sdk_result_t watcher_camera_capture", sdk_camera_capture_sync_forbidden,
                                       sizeof(sdk_camera_capture_sync_forbidden) /
                                           sizeof(sdk_camera_capture_sync_forbidden[0]));
    failures +=
        file_contains_all(root, "../components/sdk/watcher_sdk/src/watcher_sdk.c", sdk_camera_cold_start_required,
                          sizeof(sdk_camera_cold_start_required) / sizeof(sdk_camera_cold_start_required[0]));
    failures +=
        patterns_in_order(root, "../components/sdk/watcher_sdk/src/watcher_sdk.c", sdk_camera_warm_up_order_required,
                          sizeof(sdk_camera_warm_up_order_required) / sizeof(sdk_camera_warm_up_order_required[0]));
    failures += file_contains_all(root, "../components/hal/hal_camera/src/hal_camera.c",
                                  hal_camera_official_capture_semantics_required,
                                  sizeof(hal_camera_official_capture_semantics_required) /
                                      sizeof(hal_camera_official_capture_semantics_required[0]));
    failures += function_contains_all(
        root, "../components/hal/hal_camera/src/hal_camera.c", "static esp_err_t hal_camera_take_image_string",
        hal_camera_capture_request_required,
        sizeof(hal_camera_capture_request_required) / sizeof(hal_camera_capture_request_required[0]));
    failures += function_contains_none(
        root, "../components/hal/hal_camera/src/hal_camera.c", "static esp_err_t hal_camera_take_image_string",
        hal_camera_direct_invoke_forbidden,
        sizeof(hal_camera_direct_invoke_forbidden) / sizeof(hal_camera_direct_invoke_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_runtime_required,
                                  sizeof(phone_control_runtime_required) / sizeof(phone_control_runtime_required[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_firmware_exit_required,
                                  sizeof(phone_control_firmware_exit_required) /
                                      sizeof(phone_control_firmware_exit_required[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_launcher_app_center_hidden_required,
                                  sizeof(phone_control_launcher_app_center_hidden_required) /
                                      sizeof(phone_control_launcher_app_center_hidden_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", phone_control_waiting_ui_required,
                          sizeof(phone_control_waiting_ui_required) / sizeof(phone_control_waiting_ui_required[0]));
    failures +=
        file_contains_none(root, "app_main.c", espnow_remote_runtime_forbidden,
                           sizeof(espnow_remote_runtime_forbidden) / sizeof(espnow_remote_runtime_forbidden[0]));
    failures +=
        file_contains_none(root, "debug_cli.c", espnow_remote_runtime_forbidden,
                           sizeof(espnow_remote_runtime_forbidden) / sizeof(espnow_remote_runtime_forbidden[0]));
    failures += file_contains_none(root, "CMakeLists.txt", espnow_remote_cmake_forbidden,
                                   sizeof(espnow_remote_cmake_forbidden) / sizeof(espnow_remote_cmake_forbidden[0]));
    failures += file_contains_none(root, "../CMakeLists.txt", espnow_remote_cmake_forbidden,
                                   sizeof(espnow_remote_cmake_forbidden) / sizeof(espnow_remote_cmake_forbidden[0]));
    failures += file_contains_none(root, "app_main.c", espnow_low_level_api_forbidden,
                                   sizeof(espnow_low_level_api_forbidden) / sizeof(espnow_low_level_api_forbidden[0]));
    failures += file_contains_none(root, "debug_cli.c", espnow_low_level_api_forbidden,
                                   sizeof(espnow_low_level_api_forbidden) / sizeof(espnow_low_level_api_forbidden[0]));
    failures += file_contains_none(root, "../components/services/mcu_motion_service/include/mcu_motion_service.h",
                                   espnow_relay_target_forbidden,
                                   sizeof(espnow_relay_target_forbidden) / sizeof(espnow_relay_target_forbidden[0]));
    failures += file_contains_none(root, "../components/services/mcu_motion_service/src/mcu_motion_service.c",
                                   espnow_relay_target_forbidden,
                                   sizeof(espnow_relay_target_forbidden) / sizeof(espnow_relay_target_forbidden[0]));
    failures +=
        file_contains_none(root, "../components/protocols/mcu_link/include/mcu_frame.h", espnow_relay_target_forbidden,
                           sizeof(espnow_relay_target_forbidden) / sizeof(espnow_relay_target_forbidden[0]));
    failures +=
        file_contains_all(root, "app_main.c", launcher_dedicated_icon_required,
                          sizeof(launcher_dedicated_icon_required) / sizeof(launcher_dedicated_icon_required[0]));
    failures +=
        file_contains_all(root, "factory_home_ui/ui.h", launcher_icon_declaration_required,
                          sizeof(launcher_icon_declaration_required) / sizeof(launcher_icon_declaration_required[0]));
    failures += file_contains_all(root, "CMakeLists.txt", launcher_icon_build_required,
                                  sizeof(launcher_icon_build_required) / sizeof(launcher_icon_build_required[0]));
    failures += file_contains_none(root, "CMakeLists.txt", launcher_legacy_extension_build_forbidden,
                                   sizeof(launcher_legacy_extension_build_forbidden) /
                                       sizeof(launcher_legacy_extension_build_forbidden[0]));
    failures += file_contains_none(root, "factory_home_ui/ui.h", launcher_legacy_extension_build_forbidden,
                                   sizeof(launcher_legacy_extension_build_forbidden) /
                                       sizeof(launcher_legacy_extension_build_forbidden[0]));
    failures += file_contains_none(root, "app_main.c", launcher_legacy_extension_binding_forbidden,
                                   sizeof(launcher_legacy_extension_binding_forbidden) /
                                       sizeof(launcher_legacy_extension_binding_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_connected_sleep_required,
                                  sizeof(phone_control_connected_sleep_required) /
                                      sizeof(phone_control_connected_sleep_required[0]));
    failures += file_contains_all(root, "../components/services/anim_service/include/display_ui.h",
                                  phone_control_text_suppression_header_required,
                                  sizeof(phone_control_text_suppression_header_required) /
                                      sizeof(phone_control_text_suppression_header_required[0]));
    failures += file_contains_all(root, "../components/services/anim_service/src/display_ui.c",
                                  phone_control_text_suppression_display_required,
                                  sizeof(phone_control_text_suppression_display_required) /
                                      sizeof(phone_control_text_suppression_display_required[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_text_suppression_app_required,
                                  sizeof(phone_control_text_suppression_app_required) /
                                      sizeof(phone_control_text_suppression_app_required[0]));
    failures += function_contains_none(root, "app_main.c", "static void phone_control_show_connected_sleep_ui",
                                       phone_control_connected_sleep_forbidden,
                                       sizeof(phone_control_connected_sleep_forbidden) /
                                           sizeof(phone_control_connected_sleep_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", phone_control_no_ble_feedback_required,
                                  sizeof(phone_control_no_ble_feedback_required) /
                                      sizeof(phone_control_no_ble_feedback_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.h", factory_settings_network_page_api_required, 1);
    failures += file_contains_all(
        root, "factory_settings_ui.c", factory_settings_network_page_api_required + 1,
        (sizeof(factory_settings_network_page_api_required) / sizeof(factory_settings_network_page_api_required[0])) -
            1);
    failures += file_contains_all(root, "app_center.c", app_center_phone_control_local_required,
                                  sizeof(app_center_phone_control_local_required) /
                                      sizeof(app_center_phone_control_local_required[0]));
    failures += file_contains_all(root, "launcher_factory_home.h", launcher_factory_dynamic_entry_required, 1);
    failures += file_contains_all(
        root, "launcher_factory_home.c", launcher_factory_dynamic_entry_required + 1,
        (sizeof(launcher_factory_dynamic_entry_required) / sizeof(launcher_factory_dynamic_entry_required[0])) - 1);
    failures += file_contains_none(root, "launcher_factory_home.c", launcher_factory_four_button_forbidden,
                                   sizeof(launcher_factory_four_button_forbidden) /
                                       sizeof(launcher_factory_four_button_forbidden[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_scroll_required,
                                  sizeof(settings_scroll_required) / sizeof(settings_scroll_required[0]));
    failures += function_contains_none(
        root, "factory_settings_ui.c", "static void settings_set_scroll_cb", settings_scroll_callback_forbidden,
        sizeof(settings_scroll_callback_forbidden) / sizeof(settings_scroll_callback_forbidden[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_redundant_black_fill_required,
                                  sizeof(settings_redundant_black_fill_required) /
                                      sizeof(settings_redundant_black_fill_required[0]));
    failures += function_contains_all(
        root, "factory_settings_ui.c", "void set_obj_style_focused", settings_focused_fill_required,
        sizeof(settings_focused_fill_required) / sizeof(settings_focused_fill_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_focused_background_cache_required,
                                  sizeof(settings_focused_background_cache_required) /
                                      sizeof(settings_focused_background_cache_required[0]));
    failures +=
        file_contains_all(root, "factory_settings_original_ui/screens/ui_Page_Set.c", settings_object_tree_required,
                          sizeof(settings_object_tree_required) / sizeof(settings_object_tree_required[0]));
    failures += function_contains_all(
        root, "factory_settings_ui.c", "static void configure_set_page", settings_hidden_entries_required,
        sizeof(settings_hidden_entries_required) / sizeof(settings_hidden_entries_required[0]));
    failures += function_contains_none(
        root, "factory_settings_ui.c", "static lv_obj_t *set_focus_obj", settings_removed_focus_objects,
        sizeof(settings_removed_focus_objects) / sizeof(settings_removed_focus_objects[0]));
    failures += function_contains_none(
        root, "factory_settings_ui.c", "static lv_obj_t *set_focus_label", settings_removed_focus_objects,
        sizeof(settings_removed_focus_objects) / sizeof(settings_removed_focus_objects[0]));

    failures += check_group_binding_is_encoder_only(root, "launcher_factory_home.c");
    failures += check_group_binding_is_encoder_only(root, "factory_settings_ui.c");
    failures += check_group_binding_is_encoder_only(root, "app_center.c");
    failures += check_group_binding_is_encoder_only(root, "app_main.c");
    failures += function_contains_all(root, "app_main.c", "static void launcher_on_open(void)", shell_open_required,
                                      sizeof(shell_open_required) / sizeof(shell_open_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void settings_on_open(void)", shell_open_required,
                                      sizeof(shell_open_required) / sizeof(shell_open_required[0]));
    failures += file_contains_all(root, "app_main.c", input_router_app_required,
                                  sizeof(input_router_app_required) / sizeof(input_router_app_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", input_context_declarations_required,
                          sizeof(input_context_declarations_required) / sizeof(input_context_declarations_required[0]));
    failures += file_contains_all(root, "../components/esp_lvgl_port/esp_lvgl_port.c", input_router_port_required,
                                  sizeof(input_router_port_required) / sizeof(input_router_port_required[0]));
    failures += file_contains_none(root, "app_main.c", legacy_duplicate_button_paths_forbidden,
                                   sizeof(legacy_duplicate_button_paths_forbidden) /
                                       sizeof(legacy_duplicate_button_paths_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", settings_wifi_state_storage_required,
                                  sizeof(settings_wifi_state_storage_required) /
                                      sizeof(settings_wifi_state_storage_required[0]));
    failures +=
        function_contains_all(root, "app_main.c", "static void settings_fill_state", settings_wifi_state_required,
                              sizeof(settings_wifi_state_required) / sizeof(settings_wifi_state_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static bool settings_on_wifi_disconnect", settings_wifi_disconnect_required,
        sizeof(settings_wifi_disconnect_required) / sizeof(settings_wifi_disconnect_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_perform_wifi_disconnect", settings_wifi_disconnect_apply_required,
        sizeof(settings_wifi_disconnect_apply_required) / sizeof(settings_wifi_disconnect_apply_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_on_tick", settings_wifi_disconnect_tick_required,
        sizeof(settings_wifi_disconnect_tick_required) / sizeof(settings_wifi_disconnect_tick_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void settings_resume_wifi_if_needed",
                                      settings_wifi_resume_required,
                                      sizeof(settings_wifi_resume_required) / sizeof(settings_wifi_resume_required[0]));
    failures += file_contains_all(root, "app_main.c", settings_wifi_ble_scheduling_required,
                                  sizeof(settings_wifi_ble_scheduling_required) /
                                      sizeof(settings_wifi_ble_scheduling_required[0]));
    failures += function_contains_all(root, "app_main.c", "static bool settings_start_ble_provisioning_if_needed",
                                      settings_wifi_ble_provisioning_required,
                                      sizeof(settings_wifi_ble_provisioning_required) /
                                          sizeof(settings_wifi_ble_provisioning_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_on_wifi", settings_wifi_open_credentials_required,
        sizeof(settings_wifi_open_credentials_required) / sizeof(settings_wifi_open_credentials_required[0]));
    failures += function_contains_none(root, "app_main.c", "static bool settings_start_ble_provisioning_if_needed",
                                       settings_wifi_ble_provisioning_forbidden,
                                       sizeof(settings_wifi_ble_provisioning_forbidden) /
                                           sizeof(settings_wifi_ble_provisioning_forbidden[0]));
    failures +=
        file_contains_all(root, "app_main.c", settings_callback_refresh_required,
                          sizeof(settings_callback_refresh_required) / sizeof(settings_callback_refresh_required[0]));
    failures += file_contains_all(root, "app_main.c", ble_provisioning_release_after_saved_required,
                                  sizeof(ble_provisioning_release_after_saved_required) /
                                      sizeof(ble_provisioning_release_after_saved_required[0]));
    failures += file_contains_all(root, "app_main.c", settings_ble_connection_refresh_required,
                                  sizeof(settings_ble_connection_refresh_required) /
                                      sizeof(settings_ble_connection_refresh_required[0]));
    failures += file_contains_all(root, "app_main.c", settings_wifi_status_refresh_required,
                                  sizeof(settings_wifi_status_refresh_required) /
                                      sizeof(settings_wifi_status_refresh_required[0]));
    failures += file_contains_all(root, "app_main.c", wifi_time_sync_required,
                                  sizeof(wifi_time_sync_required) / sizeof(wifi_time_sync_required[0]));
    failures += function_contains_all(root, "app_main.c", "static const watcher_app_t s_launcher_app",
                                      launcher_resource_required,
                                      sizeof(launcher_resource_required) / sizeof(launcher_resource_required[0]));
    failures += function_contains_all(root, "app_main.c", "static const watcher_app_t s_settings_app",
                                      settings_resource_required,
                                      sizeof(settings_resource_required) / sizeof(settings_resource_required[0]));
    failures += file_contains_all(root, "app_main.c", boot_wifi_resume_required,
                                  sizeof(boot_wifi_resume_required) / sizeof(boot_wifi_resume_required[0]));
    failures += patterns_in_order(root, "app_main.c", boot_wifi_resume_order_required,
                                  sizeof(boot_wifi_resume_order_required) / sizeof(boot_wifi_resume_order_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", boot_power_monitor_prime_required,
                          sizeof(boot_power_monitor_prime_required) / sizeof(boot_power_monitor_prime_required[0]));
    failures += patterns_in_order(root, "app_main.c", boot_power_monitor_prime_order_required,
                                  sizeof(boot_power_monitor_prime_order_required) /
                                      sizeof(boot_power_monitor_prime_order_required[0]));
    failures += file_contains_all(root, "../CMakeLists.txt", time_sync_component_required,
                                  sizeof(time_sync_component_required) / sizeof(time_sync_component_required[0]));
    failures += file_contains_all(root, "../components/utils/time_sync_service/src/time_sync_service.c",
                                  time_sync_service_required,
                                  sizeof(time_sync_service_required) / sizeof(time_sync_service_required[0]));
    failures +=
        function_contains_all(root, "../components/utils/wifi_manager/src/wifi_manager.c", "int wifi_resume_background",
                              wifi_resume_idempotent_required,
                              sizeof(wifi_resume_idempotent_required) / sizeof(wifi_resume_idempotent_required[0]));
    failures += file_contains_all(root, "app_main.c", app_resource_wifi_resume_guard_required,
                                  sizeof(app_resource_wifi_resume_guard_required) /
                                      sizeof(app_resource_wifi_resume_guard_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static const char *transport_state_to_string", transport_cloud_state_labels_required,
        sizeof(transport_cloud_state_labels_required) / sizeof(transport_cloud_state_labels_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static const char *transport_state_to_string", transport_ble_idle_state_labels_forbidden,
        sizeof(transport_ble_idle_state_labels_forbidden) / sizeof(transport_ble_idle_state_labels_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", cloud_discovery_retry_backoff_required,
                                  sizeof(cloud_discovery_retry_backoff_required) /
                                      sizeof(cloud_discovery_retry_backoff_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void settings_on_wifi", settings_wifi_open_required,
                                      sizeof(settings_wifi_open_required) / sizeof(settings_wifi_open_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static void settings_on_wifi", settings_wifi_open_deferred_forbidden,
        sizeof(settings_wifi_open_deferred_forbidden) / sizeof(settings_wifi_open_deferred_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_on_tick", settings_wifi_detail_tick_required,
        sizeof(settings_wifi_detail_tick_required) / sizeof(settings_wifi_detail_tick_required[0]));
    failures += file_contains_all(root, "app_main.c", settings_ble_switch_deferred_required,
                                  sizeof(settings_ble_switch_deferred_required) /
                                      sizeof(settings_ble_switch_deferred_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static bool settings_perform_bluetooth_change", settings_ble_switch_full_release_required,
        sizeof(settings_ble_switch_full_release_required) / sizeof(settings_ble_switch_full_release_required[0]));
    failures += function_contains_none(root, "app_main.c", "static bool settings_perform_bluetooth_change",
                                       settings_ble_switch_partial_release_forbidden,
                                       sizeof(settings_ble_switch_partial_release_forbidden) /
                                           sizeof(settings_ble_switch_partial_release_forbidden[0]));
    failures += function_contains_none(
        root, "app_main.c", "static bool settings_on_bluetooth_changed", settings_ble_switch_deferred_forbidden,
        sizeof(settings_ble_switch_deferred_forbidden) / sizeof(settings_ble_switch_deferred_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_on_close", settings_close_ble_release_required,
        sizeof(settings_close_ble_release_required) / sizeof(settings_close_ble_release_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", settings_wifi_detail_close_required,
                          sizeof(settings_wifi_detail_close_required) / sizeof(settings_wifi_detail_close_required[0]));
    failures += file_contains_all(root, "app_main.c", settings_wifi_detail_launch_intent_required,
                                  sizeof(settings_wifi_detail_launch_intent_required) /
                                      sizeof(settings_wifi_detail_launch_intent_required[0]));
    failures +=
        file_contains_all(root, "debug_cli.h", debug_app_connect_header_required,
                          sizeof(debug_app_connect_header_required) / sizeof(debug_app_connect_header_required[0]));
    failures += file_contains_all(root, "debug_cli.c", debug_app_connect_cli_required,
                                  sizeof(debug_app_connect_cli_required) / sizeof(debug_app_connect_cli_required[0]));
    failures += file_contains_all(root, "debug_cli.c", animation_debug_cli_required,
                                  sizeof(animation_debug_cli_required) / sizeof(animation_debug_cli_required[0]));
    failures += file_contains_none(root, "app_main.c", animation_player_direct_api_forbidden,
                                   sizeof(animation_player_direct_api_forbidden) /
                                       sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", animation_player_direct_api_forbidden,
        sizeof(animation_player_direct_api_forbidden) / sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(root, "client_app.c", animation_player_direct_api_forbidden,
                                   sizeof(animation_player_direct_api_forbidden) /
                                       sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(root, "debug_cli.c", animation_player_direct_api_forbidden,
                                   sizeof(animation_player_direct_api_forbidden) /
                                       sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(root, "../components/services/behavior_state_service/src/behavior_executor.c",
                                   animation_player_direct_api_forbidden,
                                   sizeof(animation_player_direct_api_forbidden) /
                                       sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                   animation_player_direct_api_forbidden,
                                   sizeof(animation_player_direct_api_forbidden) /
                                       sizeof(animation_player_direct_api_forbidden[0]));
    failures += file_contains_none(root, "app_main.c", happy_timed_handoff_forbidden,
                                   sizeof(happy_timed_handoff_forbidden) / sizeof(happy_timed_handoff_forbidden[0]));
    failures += file_contains_all(root, "../spiffs/behavior/states.json", happy_ticket_handoff_required,
                                  sizeof(happy_ticket_handoff_required) / sizeof(happy_ticket_handoff_required[0]));
    failures += file_contains_none(root, "app_main.c", legacy_display_animation_api_forbidden,
                                   sizeof(legacy_display_animation_api_forbidden) /
                                       sizeof(legacy_display_animation_api_forbidden[0]));
    failures += file_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", legacy_display_animation_api_forbidden,
        sizeof(legacy_display_animation_api_forbidden) / sizeof(legacy_display_animation_api_forbidden[0]));
    failures +=
        file_contains_all(root, "app_main.c", main_animation_snapshot_required,
                          sizeof(main_animation_snapshot_required) / sizeof(main_animation_snapshot_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_animation_snapshot_required,
        sizeof(voice_animation_snapshot_required) / sizeof(voice_animation_snapshot_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void shutdown_present_user_feedback", shutdown_system_animation_required,
        sizeof(shutdown_system_animation_required) / sizeof(shutdown_system_animation_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static void shutdown_present_user_feedback", shutdown_behavior_animation_forbidden,
        sizeof(shutdown_behavior_animation_forbidden) / sizeof(shutdown_behavior_animation_forbidden[0]));
    failures +=
        file_contains_all(root, "app_main.c", debug_app_connect_runtime_required,
                          sizeof(debug_app_connect_runtime_required) / sizeof(debug_app_connect_runtime_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_runtime_deferred_required,
                                  sizeof(voice_runtime_deferred_required) / sizeof(voice_runtime_deferred_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void voice_app_on_open", voice_on_open_required,
                                      sizeof(voice_on_open_required) / sizeof(voice_on_open_required[0]));
    failures +=
        function_contains_all(root, "app_main.c", "static void voice_app_on_open", voice_on_open_connect_ui_required,
                              sizeof(voice_on_open_connect_ui_required) / sizeof(voice_on_open_connect_ui_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_connect_ui_required,
                                  sizeof(voice_connect_ui_required) / sizeof(voice_connect_ui_required[0]));
    failures += function_contains_all(root, "app_main.c", "static app_connect_action_t app_wifi_gate_action_for_view",
                                      app_wifi_gate_connecting_action_required,
                                      sizeof(app_wifi_gate_connecting_action_required) /
                                          sizeof(app_wifi_gate_connecting_action_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void app_wifi_gate_fill_status", app_wifi_gate_connecting_ui_required,
        sizeof(app_wifi_gate_connecting_ui_required) / sizeof(app_wifi_gate_connecting_ui_required[0]));
    failures += file_contains_all(root, "client_app.h", client_wifi_gate_deps_required,
                                  sizeof(client_wifi_gate_deps_required) / sizeof(client_wifi_gate_deps_required[0]));
    failures += file_contains_all(root, "client_app.c", client_wifi_gate_flow_required,
                                  sizeof(client_wifi_gate_flow_required) / sizeof(client_wifi_gate_flow_required[0]));
    failures += file_contains_all(root, "app_main.c", client_voice_app_main_required,
                                  sizeof(client_voice_app_main_required) / sizeof(client_voice_app_main_required[0]));
    failures +=
        file_contains_none(root, "app_main.c", legacy_client_app_main_forbidden,
                           sizeof(legacy_client_app_main_forbidden) / sizeof(legacy_client_app_main_forbidden[0]));
    failures += file_contains_all(root, "app_center.h", app_center_wifi_gate_header_required,
                                  sizeof(app_center_wifi_gate_header_required) /
                                      sizeof(app_center_wifi_gate_header_required[0]));
    failures +=
        file_contains_all(root, "app_center.c", app_center_wifi_gate_flow_required,
                          sizeof(app_center_wifi_gate_flow_required) / sizeof(app_center_wifi_gate_flow_required[0]));
    failures += file_contains_all(root, "app_main.c", app_center_wifi_gate_app_main_required,
                                  sizeof(app_center_wifi_gate_app_main_required) /
                                      sizeof(app_center_wifi_gate_app_main_required[0]));
    failures += file_contains_none(root, "app_center.c", app_center_wifi_setup_page_forbidden,
                                   sizeof(app_center_wifi_setup_page_forbidden) /
                                       sizeof(app_center_wifi_setup_page_forbidden[0]));
    failures += function_contains_none(
        root, "app_main.c", "static bool voice_app_handle_connect_action", voice_connect_root_settings_forbidden,
        sizeof(voice_connect_root_settings_forbidden) / sizeof(voice_connect_root_settings_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", voice_connect_timeout_close_required,
                                  sizeof(voice_connect_timeout_close_required) /
                                      sizeof(voice_connect_timeout_close_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void voice_app_enter_ws_failed", voice_connect_ws_failed_close_required,
        sizeof(voice_connect_ws_failed_close_required) / sizeof(voice_connect_ws_failed_close_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static void voice_app_enter_ws_failed", voice_connect_timeout_stop_forbidden,
        sizeof(voice_connect_timeout_stop_forbidden) / sizeof(voice_connect_timeout_stop_forbidden[0]));
    failures += function_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", "static esp_err_t voice_recorder_stop_internal",
        voice_recorder_close_releases_hal_required,
        sizeof(voice_recorder_close_releases_hal_required) / sizeof(voice_recorder_close_releases_hal_required[0]));
    failures +=
        function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                              "esp_err_t voice_recorder_close(void)", voice_recorder_close_api_required,
                              sizeof(voice_recorder_close_api_required) / sizeof(voice_recorder_close_api_required[0]));
    failures +=
        function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                              "esp_err_t voice_recorder_stop(void)", voice_recorder_stop_api_required,
                              sizeof(voice_recorder_stop_api_required) / sizeof(voice_recorder_stop_api_required[0]));
    failures += file_contains_all(root, "../components/services/voice_service/include/voice_service.h",
                                  voice_recorder_behavior_feedback_api_required,
                                  sizeof(voice_recorder_behavior_feedback_api_required) /
                                      sizeof(voice_recorder_behavior_feedback_api_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_recorder_behavior_feedback_required,
        sizeof(voice_recorder_behavior_feedback_required) / sizeof(voice_recorder_behavior_feedback_required[0]));
    failures +=
        function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                              "static void show_cloud_not_ready_state", voice_recorder_behavior_feedback_guard_required,
                              sizeof(voice_recorder_behavior_feedback_guard_required) /
                                  sizeof(voice_recorder_behavior_feedback_guard_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "static void show_listening_ui", voice_recorder_behavior_feedback_guard_required,
                                      sizeof(voice_recorder_behavior_feedback_guard_required) /
                                          sizeof(voice_recorder_behavior_feedback_guard_required[0]));
    failures += function_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c",
        "static void show_thinking_expression_without_body_action", voice_waiting_ui_without_animation_required,
        sizeof(voice_waiting_ui_without_animation_required) / sizeof(voice_waiting_ui_without_animation_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_connect_retry_restart_required,
                                  sizeof(voice_connect_retry_restart_required) /
                                      sizeof(voice_connect_retry_restart_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static bool voice_app_enter_ready_ui_if_needed(const char *reason)",
        voice_ready_ui_transition_required,
        sizeof(voice_ready_ui_transition_required) / sizeof(voice_ready_ui_transition_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_runtime_ready_ui_required,
                                  sizeof(voice_runtime_ready_ui_required) / sizeof(voice_runtime_ready_ui_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_connect_ui_required,
                                  sizeof(agent_connect_ui_required) / sizeof(agent_connect_ui_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_ready_ui_required,
                                  sizeof(agent_ready_ui_required) / sizeof(agent_ready_ui_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", agent_ready_idle_random_required,
                          sizeof(agent_ready_idle_random_required) / sizeof(agent_ready_idle_random_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static bool agent_app_enter_ready_ui_if_needed", agent_ready_ui_sleep_forbidden,
        sizeof(agent_ready_ui_sleep_forbidden) / sizeof(agent_ready_ui_sleep_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", agent_activity_ui_required,
                                  sizeof(agent_activity_ui_required) / sizeof(agent_activity_ui_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_ready_prefetch_required,
                                  sizeof(agent_ready_prefetch_required) / sizeof(agent_ready_prefetch_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_sfx_suppression_required,
                                  sizeof(agent_sfx_suppression_required) / sizeof(agent_sfx_suppression_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_failure_clears_activity_required,
                                  sizeof(agent_failure_clears_activity_required) /
                                      sizeof(agent_failure_clears_activity_required[0]));
    failures += patterns_in_order(root, "app_main.c", agent_stage_sfx_suppression_order_required,
                                  sizeof(agent_stage_sfx_suppression_order_required) /
                                      sizeof(agent_stage_sfx_suppression_order_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void agent_app_on_close", agent_on_close_connect_ui_required,
        sizeof(agent_on_close_connect_ui_required) / sizeof(agent_on_close_connect_ui_required[0]));
    failures +=
        function_contains_none(root, "app_main.c", "static void agent_app_stage_changed", agent_stage_old_ui_forbidden,
                               sizeof(agent_stage_old_ui_forbidden) / sizeof(agent_stage_old_ui_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void agent_runtime_set_behavior_feedback", agent_behavior_feedback_required,
        sizeof(agent_behavior_feedback_required) / sizeof(agent_behavior_feedback_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_stable_ram_diag_required,
                                  sizeof(voice_stable_ram_diag_required) / sizeof(voice_stable_ram_diag_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_fade_duration_required,
                                  sizeof(voice_fade_duration_required) / sizeof(voice_fade_duration_required[0]));
    failures += function_contains_none(root, "app_main.c", "static void voice_app_on_open", voice_on_open_forbidden,
                                       sizeof(voice_on_open_forbidden) / sizeof(voice_on_open_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", voice_ready_idle_lightweight_required,
                                  sizeof(voice_ready_idle_lightweight_required) /
                                      sizeof(voice_ready_idle_lightweight_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_ready_idle_flow_required,
                                  sizeof(voice_ready_idle_flow_required) / sizeof(voice_ready_idle_flow_required[0]));
    failures += file_contains_all(root, "app_main.c", random_touch_fondle_required,
                                  sizeof(random_touch_fondle_required) / sizeof(random_touch_fondle_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_tts_completion_ready_idle_required,
                                  sizeof(voice_tts_completion_ready_idle_required) /
                                      sizeof(voice_tts_completion_ready_idle_required[0]));
    failures += file_contains_all(root, "app_main.c", basic_ready_idle_lightweight_required,
                                  sizeof(basic_ready_idle_lightweight_required) /
                                      sizeof(basic_ready_idle_lightweight_required[0]));
    failures += file_contains_all(
        root, "../components/hal/hal_display/src/hal_display.c", hal_display_animation_surface_required,
        sizeof(hal_display_animation_surface_required) / sizeof(hal_display_animation_surface_required[0]));
    failures += file_contains_none(
        root, "../components/hal/hal_display/src/hal_display.c", hal_display_animation_control_forbidden,
        sizeof(hal_display_animation_control_forbidden) / sizeof(hal_display_animation_control_forbidden[0]));
    failures += file_contains_all(
        root, "../components/hal/hal_display/src/hal_display.c", hal_display_voice_connect_status_required,
        sizeof(hal_display_voice_connect_status_required) / sizeof(hal_display_voice_connect_status_required[0]));
    failures +=
        file_contains_all(root, "../components/services/anim_service/src/anim_player.c", anim_player_prefetch_required,
                          sizeof(anim_player_prefetch_required) / sizeof(anim_player_prefetch_required[0]));
    failures +=
        file_contains_all(root, "../components/services/anim_service/src/display_ui.c", display_ui_text_only_required,
                          sizeof(display_ui_text_only_required) / sizeof(display_ui_text_only_required[0]));
    failures +=
        file_contains_none(root, "../components/services/anim_service/src/display_ui.c", display_ui_animation_forbidden,
                           sizeof(display_ui_animation_forbidden) / sizeof(display_ui_animation_forbidden[0]));
    failures += function_contains_all(
        root, "../components/services/behavior_state_service/src/behavior_executor.c",
        "void behavior_executor_apply_display", behavior_display_failure_fallback_required,
        sizeof(behavior_display_failure_fallback_required) / sizeof(behavior_display_failure_fallback_required[0]));
    failures += file_contains_all(
        root, "../components/services/anim_service/src/anim_player.c", anim_direct_lcd_single_dma_strip_required,
        sizeof(anim_direct_lcd_single_dma_strip_required) / sizeof(anim_direct_lcd_single_dma_strip_required[0]));
    failures += function_contains_all(root, "../components/services/anim_service/src/anim_player.c",
                                      "static bool playback_note_load_failure", anim_pending_bad_frame_cancel_required,
                                      sizeof(anim_pending_bad_frame_cancel_required) /
                                          sizeof(anim_pending_bad_frame_cancel_required[0]));
    failures += file_contains_all(
        root, "../components/services/anim_service/src/anim_storage.c", anim_storage_frame_validation_required,
        sizeof(anim_storage_frame_validation_required) / sizeof(anim_storage_frame_validation_required[0]));
    failures += file_contains_none(
        root, "../components/services/anim_service/src/anim_player.c", anim_direct_lcd_fade_color_shift_forbidden,
        sizeof(anim_direct_lcd_fade_color_shift_forbidden) / sizeof(anim_direct_lcd_fade_color_shift_forbidden[0]));
    failures += file_contains_none(
        root, "../components/services/anim_service/src/anim_player.c", anim_direct_lcd_double_dma_strip_forbidden,
        sizeof(anim_direct_lcd_double_dma_strip_forbidden) / sizeof(anim_direct_lcd_double_dma_strip_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void voice_app_on_button", voice_on_button_exit_only_required,
        sizeof(voice_on_button_exit_only_required) / sizeof(voice_on_button_exit_only_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", local_exit_touch_target_required,
                          sizeof(local_exit_touch_target_required) / sizeof(local_exit_touch_target_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", local_exit_pointer_swipe_required,
                          sizeof(local_exit_pointer_swipe_required) / sizeof(local_exit_pointer_swipe_required[0]));
    failures += file_contains_all(root, "app_main.c", local_touch_dispatch_required,
                                  sizeof(local_touch_dispatch_required) / sizeof(local_touch_dispatch_required[0]));
    failures += file_contains_all(root, "app_main.c", voice_touch_double_tap_required,
                                  sizeof(voice_touch_double_tap_required) / sizeof(voice_touch_double_tap_required[0]));
    failures += file_contains_all(root, "app_main.c", agent_touch_double_tap_required,
                                  sizeof(agent_touch_double_tap_required) / sizeof(agent_touch_double_tap_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", local_exit_animation_live_required,
                          sizeof(local_exit_animation_live_required) / sizeof(local_exit_animation_live_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static void local_show_exit", local_show_exit_animation_stop_forbidden,
        sizeof(local_show_exit_animation_stop_forbidden) / sizeof(local_show_exit_animation_stop_forbidden[0]));
    failures +=
        function_contains_none(root, "app_main.c", "static void local_exit_event_cb", local_exit_direct_open_forbidden,
                               sizeof(local_exit_direct_open_forbidden) / sizeof(local_exit_direct_open_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", launcher_return_settle_required,
                                  sizeof(launcher_return_settle_required) / sizeof(launcher_return_settle_required[0]));
    failures +=
        function_contains_none(root, "app_main.c", "static void voice_app_on_button", voice_on_button_record_forbidden,
                               sizeof(voice_on_button_record_forbidden) / sizeof(voice_on_button_record_forbidden[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void voice_app_stop_transport", voice_close_transport_required,
        sizeof(voice_close_transport_required) / sizeof(voice_close_transport_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void voice_app_on_close", voice_on_close_required,
                                      sizeof(voice_on_close_required) / sizeof(voice_on_close_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void basic_app_on_open", basic_on_open_required,
                                      sizeof(basic_on_open_required) / sizeof(basic_on_open_required[0]));
    failures += function_contains_all(root, "app_main.c", "static void basic_app_on_close", basic_on_close_required,
                                      sizeof(basic_on_close_required) / sizeof(basic_on_close_required[0]));
    failures += function_contains_none(root, "app_main.c", "static void basic_app_on_open", voice_on_open_forbidden,
                                       sizeof(voice_on_open_forbidden) / sizeof(voice_on_open_forbidden[0]));
    failures += file_contains_all(root, "app_main.c", voice_runtime_tick_required,
                                  sizeof(voice_runtime_tick_required) / sizeof(voice_runtime_tick_required[0]));
    failures += patterns_in_order(root, "app_main.c", main_loop_active_after_dispatch_order_required,
                                  sizeof(main_loop_active_after_dispatch_order_required) /
                                      sizeof(main_loop_active_after_dispatch_order_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void voice_runtime_start_if_due", voice_runtime_wifi_gate_required,
        sizeof(voice_runtime_wifi_gate_required) / sizeof(voice_runtime_wifi_gate_required[0]));
    failures += function_contains_all(root, "app_main.c", "static esp_err_t app_resource_stop_ble",
                                      ble_stop_nonblocking_required,
                                      sizeof(ble_stop_nonblocking_required) / sizeof(ble_stop_nonblocking_required[0]));
    failures += function_contains_none(root, "app_main.c", "static esp_err_t app_resource_stop_ble", ble_stop_forbidden,
                                       sizeof(ble_stop_forbidden) / sizeof(ble_stop_forbidden[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "int voice_recorder_start", voice_task_internal_stack_required,
                                      sizeof(voice_task_internal_stack_required) /
                                          sizeof(voice_task_internal_stack_required[0]));
    failures +=
        function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c", "int voice_recorder_start",
                               voice_task_psram_stack_forbidden,
                               sizeof(voice_task_psram_stack_forbidden) / sizeof(voice_task_psram_stack_forbidden[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_resume_wake_closing_guard_required,
        sizeof(voice_resume_wake_closing_guard_required) / sizeof(voice_resume_wake_closing_guard_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_wake_runtime_disabled_required,
        sizeof(voice_wake_runtime_disabled_required) / sizeof(voice_wake_runtime_disabled_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_wake_configure_only_required,
        sizeof(voice_wake_configure_only_required) / sizeof(voice_wake_configure_only_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_wake_lock_precreate_required,
        sizeof(voice_wake_lock_precreate_required) / sizeof(voice_wake_lock_precreate_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "void voice_recorder_init(void)", voice_init_precreates_wake_lock_required,
                                      sizeof(voice_init_precreates_wake_lock_required) /
                                          sizeof(voice_init_precreates_wake_lock_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "static void wake_word_configure(void) {", voice_wake_configure_lock_required,
                                      sizeof(voice_wake_configure_lock_required) /
                                          sizeof(voice_wake_configure_lock_required[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "static bool wake_lifecycle_lock", voice_wake_lock_callback_forbidden,
                                       sizeof(voice_wake_lock_callback_forbidden) /
                                           sizeof(voice_wake_lock_callback_forbidden[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "int voice_recorder_start(void)", voice_start_no_wake_resume_forbidden,
                                       sizeof(voice_start_no_wake_resume_forbidden) /
                                           sizeof(voice_start_no_wake_resume_forbidden[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_listening_memory_thresholds_required,
        sizeof(voice_listening_memory_thresholds_required) / sizeof(voice_listening_memory_thresholds_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "static void show_listening_ui(void)", voice_listening_low_memory_required,
                                      sizeof(voice_listening_low_memory_required) /
                                          sizeof(voice_listening_low_memory_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_listening_state_gating_required,
        sizeof(voice_listening_state_gating_required) / sizeof(voice_listening_state_gating_required[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "static void show_listening_ui(void)", voice_listening_old_prefetch_forbidden,
                                       sizeof(voice_listening_old_prefetch_forbidden) /
                                           sizeof(voice_listening_old_prefetch_forbidden[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_remote_single_writer_required,
        sizeof(voice_remote_single_writer_required) / sizeof(voice_remote_single_writer_required[0]));
    failures += file_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_remote_single_writer_forbidden,
        sizeof(voice_remote_single_writer_forbidden) / sizeof(voice_remote_single_writer_forbidden[0]));
    failures += function_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", "void voice_recorder_suspend_cloud_audio(void)",
        voice_suspend_caller_side_effects_forbidden,
        sizeof(voice_suspend_caller_side_effects_forbidden) / sizeof(voice_suspend_caller_side_effects_forbidden[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "static void on_wake_word_detected(const char *wake_word, void *user_data) {",
                                      voice_wake_word_listening_order_required,
                                      sizeof(voice_wake_word_listening_order_required) /
                                          sizeof(voice_wake_word_listening_order_required[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "static void on_wake_word_detected(const char *wake_word, void *user_data) {",
                                       voice_wake_word_callback_forbidden,
                                       sizeof(voice_wake_word_callback_forbidden) /
                                           sizeof(voice_wake_word_callback_forbidden[0]));
    failures += function_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", "static void handle_wake_word_event(void)",
        voice_wake_word_event_handler_required,
        sizeof(voice_wake_word_event_handler_required) / sizeof(voice_wake_word_event_handler_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", voice_sleep_wake_resume_required,
                          sizeof(voice_sleep_wake_resume_required) / sizeof(voice_sleep_wake_resume_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "int voice_recorder_tick(void)", voice_wake_lifecycle_feed_required,
                                      sizeof(voice_wake_lifecycle_feed_required) /
                                          sizeof(voice_wake_lifecycle_feed_required[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "int voice_recorder_tick(void)", voice_wake_lifecycle_feed_forbidden,
                                       sizeof(voice_wake_lifecycle_feed_forbidden) /
                                           sizeof(voice_wake_lifecycle_feed_forbidden[0]));
    failures += function_contains_all(
        root, "../components/services/behavior_state_service/src/behavior_state_service.c",
        "static void behavior_dispatch_expression_locked", behavior_expression_event_dispatch_required,
        sizeof(behavior_expression_event_dispatch_required) / sizeof(behavior_expression_event_dispatch_required[0]));
    failures += function_contains_none(
        root, "../components/services/behavior_state_service/src/behavior_state_service.c",
        "static void behavior_dispatch_expression_locked", behavior_expression_event_dispatch_forbidden,
        sizeof(behavior_expression_event_dispatch_forbidden) / sizeof(behavior_expression_event_dispatch_forbidden[0]));
    failures += function_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", "static void handle_short_press_toggle(void)",
        voice_stop_recording_wait_required,
        sizeof(voice_stop_recording_wait_required) / sizeof(voice_stop_recording_wait_required[0]));
    failures += function_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c", "static void handle_short_press_toggle(void)",
        voice_stop_recording_animation_forbidden,
        sizeof(voice_stop_recording_animation_forbidden) / sizeof(voice_stop_recording_animation_forbidden[0]));
    failures += function_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c",
        "static void handle_remote_control_snapshot(void)", voice_stop_recording_wait_required,
        sizeof(voice_stop_recording_wait_required) / sizeof(voice_stop_recording_wait_required[0]));
    failures += function_contains_none(
        root, "../components/services/voice_service/src/voice_fsm.c",
        "static void handle_remote_control_snapshot(void)", voice_stop_recording_animation_forbidden,
        sizeof(voice_stop_recording_animation_forbidden) / sizeof(voice_stop_recording_animation_forbidden[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "int voice_recorder_tick(void)", voice_stop_recording_wait_required,
                                       sizeof(voice_stop_recording_wait_required) /
                                           sizeof(voice_stop_recording_wait_required[0]));
    failures += function_contains_all(root, "../components/services/voice_service/src/voice_fsm.c",
                                      "static int stop_recording(void)", voice_stop_recording_wait_required,
                                      sizeof(voice_stop_recording_wait_required) /
                                          sizeof(voice_stop_recording_wait_required[0]));
    failures += function_contains_none(root, "../components/services/voice_service/src/voice_fsm.c",
                                       "int voice_recorder_tick(void)", voice_stop_recording_animation_forbidden,
                                       sizeof(voice_stop_recording_animation_forbidden) /
                                           sizeof(voice_stop_recording_animation_forbidden[0]));
    failures +=
        function_contains_all(root, "../components/protocols/ws_client/src/ws_handlers.c", "void on_asr_result_handler",
                              ws_asr_result_log_only_required,
                              sizeof(ws_asr_result_log_only_required) / sizeof(ws_asr_result_log_only_required[0]));
    failures += function_contains_all(root, "../components/protocols/ws_client/src/ws_handlers.c",
                                      "const char *ws_ai_status_to_emoji", ai_status_processing_mapping_required,
                                      sizeof(ai_status_processing_mapping_required) /
                                          sizeof(ai_status_processing_mapping_required[0]));
    failures += function_contains_all(
        root, "../components/services/control_ingress/src/control_ingress.c",
        "static const char *control_ai_status_to_fallback", ai_status_processing_mapping_required,
        sizeof(ai_status_processing_mapping_required) / sizeof(ai_status_processing_mapping_required[0]));
    failures += function_contains_all(
        root, "../components/protocols/ws_client/src/ws_client.c", "static esp_err_t ws_tts_runtime_init(void) {",
        ws_tts_worker_internal_stack_required,
        sizeof(ws_tts_worker_internal_stack_required) / sizeof(ws_tts_worker_internal_stack_required[0]));
    failures += function_contains_none(
        root, "../components/protocols/ws_client/src/ws_client.c", "static esp_err_t ws_tts_runtime_init(void) {",
        ws_tts_worker_psram_stack_forbidden,
        sizeof(ws_tts_worker_psram_stack_forbidden) / sizeof(ws_tts_worker_psram_stack_forbidden[0]));
    failures += function_contains_all(root, "../components/services/sfx_service/src/sfx_service.c",
                                      "void sfx_service_set_cloud_audio_busy", sfx_cloud_busy_no_lazy_init_required,
                                      sizeof(sfx_cloud_busy_no_lazy_init_required) /
                                          sizeof(sfx_cloud_busy_no_lazy_init_required[0]));
    failures += function_contains_all(root, "../components/services/sfx_service/src/sfx_service.c",
                                      "void sfx_service_set_voice_audio_busy", sfx_voice_busy_no_lazy_init_required,
                                      sizeof(sfx_voice_busy_no_lazy_init_required) /
                                          sizeof(sfx_voice_busy_no_lazy_init_required[0]));
    failures += function_contains_none(root, "../components/services/sfx_service/src/sfx_service.c",
                                       "void sfx_service_set_cloud_audio_busy", sfx_cloud_busy_lazy_init_forbidden,
                                       sizeof(sfx_cloud_busy_lazy_init_forbidden) /
                                           sizeof(sfx_cloud_busy_lazy_init_forbidden[0]));
    failures +=
        file_contains_all(root, "../components/services/sfx_service/src/sfx_service.c", sfx_audio_blocker_required,
                          sizeof(sfx_audio_blocker_required) / sizeof(sfx_audio_blocker_required[0]));
    failures += function_contains_all(root, "../components/services/sfx_service/src/sfx_service.c",
                                      "static void sfx_playback_file", sfx_release_idle_after_local_playback_required,
                                      sizeof(sfx_release_idle_after_local_playback_required) /
                                          sizeof(sfx_release_idle_after_local_playback_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_service_sfx_guard_required,
        sizeof(voice_service_sfx_guard_required) / sizeof(voice_service_sfx_guard_required[0]));
    failures +=
        function_contains_all(root, "app_main.c", "static void voice_app_on_open(void)", voice_app_sfx_guard_required,
                              sizeof(voice_app_sfx_guard_required) / sizeof(voice_app_sfx_guard_required[0]));
    failures += function_patterns_in_order(root, "app_main.c", "static void voice_runtime_start_if_due(void)",
                                           voice_runtime_idle_sfx_release_order_required,
                                           sizeof(voice_runtime_idle_sfx_release_order_required) /
                                               sizeof(voice_runtime_idle_sfx_release_order_required[0]));
    failures += function_contains_none(
        root, "app_main.c", "static void voice_runtime_start_if_due(void)", voice_runtime_direct_sfx_release_forbidden,
        sizeof(voice_runtime_direct_sfx_release_forbidden) / sizeof(voice_runtime_direct_sfx_release_forbidden[0]));
    failures += function_patterns_in_order(root, "../components/services/voice_service/src/voice_fsm.c",
                                           "static void voice_process_pending_events(void)",
                                           voice_task_startup_audio_release_order_required,
                                           sizeof(voice_task_startup_audio_release_order_required) /
                                               sizeof(voice_task_startup_audio_release_order_required[0]));
    failures += function_patterns_in_order(root, "../components/services/voice_service/src/voice_fsm.c",
                                           "static void voice_release_startup_audio_guard_if_idle(void)",
                                           voice_startup_audio_release_guard_order_required,
                                           sizeof(voice_startup_audio_release_guard_order_required) /
                                               sizeof(voice_startup_audio_release_guard_order_required[0]));
    failures += file_contains_all(
        root, "../components/services/voice_service/src/voice_fsm.c", voice_startup_audio_release_request_required,
        sizeof(voice_startup_audio_release_request_required) / sizeof(voice_startup_audio_release_request_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void voice_app_on_close(void)", voice_app_sfx_release_required,
        sizeof(voice_app_sfx_release_required) / sizeof(voice_app_sfx_release_required[0]));
    failures +=
        file_contains_all(root, "../tools/sync_anim_sdcard.py", sync_anim_current_project_required,
                          sizeof(sync_anim_current_project_required) / sizeof(sync_anim_current_project_required[0]));
    failures +=
        function_contains_all(root, "../components/services/agent_audio_player/src/agent_audio_player.c",
                              "esp_err_t agent_audio_player_enqueue", agent_audio_enqueue_gain_required,
                              sizeof(agent_audio_enqueue_gain_required) / sizeof(agent_audio_enqueue_gain_required[0]));
    failures += patterns_in_order(root, "../components/services/agent_audio_player/src/agent_audio_player.c",
                                  agent_audio_tail_guard_order_required,
                                  sizeof(agent_audio_tail_guard_order_required) /
                                      sizeof(agent_audio_tail_guard_order_required[0]));
    failures += function_contains_none(
        root, "../components/services/behavior_state_service/src/behavior_state_service.c",
        "esp_err_t behavior_state_load", behavior_load_sfx_preload_forbidden,
        sizeof(behavior_load_sfx_preload_forbidden) / sizeof(behavior_load_sfx_preload_forbidden[0]));
    failures += file_contains_all(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                  behavior_spiffs_mount_required,
                                  sizeof(behavior_spiffs_mount_required) / sizeof(behavior_spiffs_mount_required[0]));
    failures += file_contains_all(
        root, "../components/sensecap-watcher/sensecap-watcher.c", spiffs_mount_failure_visibility_required,
        sizeof(spiffs_mount_failure_visibility_required) / sizeof(spiffs_mount_failure_visibility_required[0]));
    failures += file_contains_all(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                  behavior_missing_state_reload_required,
                                  sizeof(behavior_missing_state_reload_required) /
                                      sizeof(behavior_missing_state_reload_required[0]));
    failures += file_contains_all(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                  behavior_action_active_snapshot_required,
                                  sizeof(behavior_action_active_snapshot_required) /
                                      sizeof(behavior_action_active_snapshot_required[0]));
    failures += file_contains_all(root, "../components/services/behavior_state_service/src/behavior_state_service.c",
                                  behavior_busy_snapshot_required,
                                  sizeof(behavior_busy_snapshot_required) / sizeof(behavior_busy_snapshot_required[0]));
    failures += function_contains_none(
        root, "../components/services/behavior_state_service/src/behavior_state_service.c",
        "bool behavior_state_is_busy(void)", behavior_busy_lock_query_forbidden,
        sizeof(behavior_busy_lock_query_forbidden) / sizeof(behavior_busy_lock_query_forbidden[0]));
    failures += function_contains_none(
        root, "../components/services/behavior_state_service/src/behavior_state_service.c",
        "bool behavior_state_is_action_active(void)", behavior_action_active_lock_query_forbidden,
        sizeof(behavior_action_active_lock_query_forbidden) / sizeof(behavior_action_active_lock_query_forbidden[0]));
    failures +=
        file_contains_all(root, "../components/protocols/ws_client/src/ws_client.c", ws_rx_fragment_psram_required,
                          sizeof(ws_rx_fragment_psram_required) / sizeof(ws_rx_fragment_psram_required[0]));
    failures += file_contains_none(
        root, "../components/protocols/ws_client/src/ws_client.c", ws_rx_fragment_internal_first_forbidden,
        sizeof(ws_rx_fragment_internal_first_forbidden) / sizeof(ws_rx_fragment_internal_first_forbidden[0]));
    failures += file_contains_all(root, "../components/services/voice_service/src/hal_wake_word.c",
                                  wake_word_model_cache_required,
                                  sizeof(wake_word_model_cache_required) / sizeof(wake_word_model_cache_required[0]));
    failures +=
        function_contains_none(root, "../components/services/voice_service/src/hal_wake_word.c",
                               "void hal_wake_word_deinit", wake_word_deinit_model_forbidden,
                               sizeof(wake_word_deinit_model_forbidden) / sizeof(wake_word_deinit_model_forbidden[0]));
    failures += file_contains_all(
        root, "../components/sensecap-watcher/sensecap-watcher.c", bsp_audio_codec_open_state_required,
        sizeof(bsp_audio_codec_open_state_required) / sizeof(bsp_audio_codec_open_state_required[0]));
    failures += function_contains_all(root, "../components/sensecap-watcher/sensecap-watcher.c",
                                      "esp_err_t bsp_codec_deinit(void)", bsp_audio_i2s_disable_guard_required,
                                      sizeof(bsp_audio_i2s_disable_guard_required) /
                                          sizeof(bsp_audio_i2s_disable_guard_required[0]));
    failures +=
        file_contains_all(root, "../components/sensecap-watcher/idf_component.yml", sensecap_codec_override_required,
                          sizeof(sensecap_codec_override_required) / sizeof(sensecap_codec_override_required[0]));
    failures += function_contains_all(root, "../components/esp_codec_dev/platform/audio_codec_data_i2s.c",
                                      "static int _i2s_data_set_fmt", codec_i2s_set_fmt_disable_guard_required,
                                      sizeof(codec_i2s_set_fmt_disable_guard_required) /
                                          sizeof(codec_i2s_set_fmt_disable_guard_required[0]));
    failures += function_contains_all(
        root, "app_main.c", "static void settings_on_tick", settings_callback_refresh_tick_required,
        sizeof(settings_callback_refresh_tick_required) / sizeof(settings_callback_refresh_tick_required[0]));
    failures += function_contains_none(root, "app_main.c", "static void settings_on_wifi", settings_wifi_open_forbidden,
                                       sizeof(settings_wifi_open_forbidden) / sizeof(settings_wifi_open_forbidden[0]));
    failures +=
        file_contains_all(root, "factory_settings_ui.h", settings_git_about_state_required,
                          sizeof(settings_git_about_state_required) / sizeof(settings_git_about_state_required[0]));
    failures += function_contains_all(
        root, "factory_settings_ui.c", "void update_about_labels(void)", settings_git_about_labels_required,
        sizeof(settings_git_about_labels_required) / sizeof(settings_git_about_labels_required[0]));
    failures += function_contains_all(root, "factory_settings_ui.c", "static void update_about_scrolling_label",
                                      settings_git_about_scrolling_label_required,
                                      sizeof(settings_git_about_scrolling_label_required) /
                                          sizeof(settings_git_about_scrolling_label_required[0]));
    failures += function_contains_none(
        root, "factory_settings_ui.c", "void update_about_labels(void)", settings_git_about_labels_forbidden,
        sizeof(settings_git_about_labels_forbidden) / sizeof(settings_git_about_labels_forbidden[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_about_system_overview_required,
                                  sizeof(settings_about_system_overview_required) /
                                      sizeof(settings_about_system_overview_required[0]));
    failures += file_contains_all(root, "factory_settings_ui.c", settings_about_phone_control_variant_required,
                                  sizeof(settings_about_phone_control_variant_required) /
                                      sizeof(settings_about_phone_control_variant_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", settings_resource_manifest_required,
                          sizeof(settings_resource_manifest_required) / sizeof(settings_resource_manifest_required[0]));
    failures += file_contains_none(root, "factory_settings_ui.c", settings_about_sync_snapshot_forbidden,
                                   sizeof(settings_about_sync_snapshot_forbidden) /
                                       sizeof(settings_about_sync_snapshot_forbidden[0]));
    failures += file_contains_all(root, "app_center.h", app_center_manager_api_required,
                                  sizeof(app_center_manager_api_required) / sizeof(app_center_manager_api_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_manager_snapshot_cache_required,
                                  sizeof(app_center_manager_snapshot_cache_required) /
                                      sizeof(app_center_manager_snapshot_cache_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_firmware_install_match_required,
                                  sizeof(app_center_firmware_install_match_required) /
                                      sizeof(app_center_firmware_install_match_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_espnow_legacy_cleanup_required,
                                  sizeof(app_center_espnow_legacy_cleanup_required) /
                                      sizeof(app_center_espnow_legacy_cleanup_required[0]));
    failures += file_contains_none(root, "app_center.c", app_center_legacy_ui_name_forbidden,
                                   sizeof(app_center_legacy_ui_name_forbidden) /
                                       sizeof(app_center_legacy_ui_name_forbidden[0]));
    failures += file_contains_none(root, "app_center.c", app_center_espnow_builtin_forbidden,
                                   sizeof(app_center_espnow_builtin_forbidden) /
                                       sizeof(app_center_espnow_builtin_forbidden[0]));
    failures += file_contains_none(root, "Kconfig.projbuild", app_center_espnow_builtin_forbidden,
                                   sizeof(app_center_espnow_builtin_forbidden) /
                                       sizeof(app_center_espnow_builtin_forbidden[0]));
    failures += file_contains_all(root, "app_center.c", app_center_uninstall_confirm_required,
                                  sizeof(app_center_uninstall_confirm_required) /
                                      sizeof(app_center_uninstall_confirm_required[0]));
    failures +=
        file_contains_all(root, "app_center_manager.c", app_center_manager_policy_required,
                          sizeof(app_center_manager_policy_required) / sizeof(app_center_manager_policy_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_exit_required,
                                  sizeof(app_center_exit_required) / sizeof(app_center_exit_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_wifi_style_status_ui_required,
                                  sizeof(app_center_wifi_style_status_ui_required) /
                                      sizeof(app_center_wifi_style_status_ui_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_list_detail_text_style_required,
                                  sizeof(app_center_list_detail_text_style_required) /
                                      sizeof(app_center_list_detail_text_style_required[0]));
    failures += file_contains_all(root, "app_center.c", app_center_ui_snapshot_required,
                                  sizeof(app_center_ui_snapshot_required) / sizeof(app_center_ui_snapshot_required[0]));
    failures += file_contains_all(root, "app_main.c", app_center_debug_status_hook_required,
                                  sizeof(app_center_debug_status_hook_required) /
                                      sizeof(app_center_debug_status_hook_required[0]));
    failures += check_app_center_status_layout_geometry();
    failures += check_app_center_list_detail_layout_geometry();
    failures +=
        file_contains_none(root, "../app_center_sample/README.md", app_center_sample_espnow_forbidden,
                           sizeof(app_center_sample_espnow_forbidden) / sizeof(app_center_sample_espnow_forbidden[0]));
    failures +=
        file_contains_none(root, "../app_center_sample/apps.json", app_center_sample_espnow_forbidden,
                           sizeof(app_center_sample_espnow_forbidden) / sizeof(app_center_sample_espnow_forbidden[0]));
    failures +=
        file_contains_none(root, "../docs/app_center_app_pack_contract.md", app_center_sample_espnow_forbidden,
                           sizeof(app_center_sample_espnow_forbidden) / sizeof(app_center_sample_espnow_forbidden[0]));
    failures += file_contains_all(root, "app_center_exit_policy.c", app_center_exit_policy_required,
                                  sizeof(app_center_exit_policy_required) / sizeof(app_center_exit_policy_required[0]));
    failures += file_contains_all(
        root, "test_support/host/src/test_app_center_exit_policy.c", app_center_exit_policy_test_required,
        sizeof(app_center_exit_policy_test_required) / sizeof(app_center_exit_policy_test_required[0]));
    failures += file_contains_all(
        root, "../components/services/ota_service/include/ota_service.h", ota_service_cancel_header_required,
        sizeof(ota_service_cancel_header_required) / sizeof(ota_service_cancel_header_required[0]));
    failures +=
        file_contains_all(root, "../components/services/ota_service/src/ota_service.c", ota_service_cancel_required,
                          sizeof(ota_service_cancel_required) / sizeof(ota_service_cancel_required[0]));
    failures +=
        file_contains_all(root, "app_main.c", settings_git_build_info_required,
                          sizeof(settings_git_build_info_required) / sizeof(settings_git_build_info_required[0]));
    failures += file_contains_all(
        root, "../components/protocols/mcu_link/include/mcu_frame.h", mcu_link_git_tlv_constants_required,
        sizeof(mcu_link_git_tlv_constants_required) / sizeof(mcu_link_git_tlv_constants_required[0]));
    failures +=
        file_contains_all(root, "../components/protocols/mcu_link/src/mcu_link.c", mcu_link_git_metadata_required,
                          sizeof(mcu_link_git_metadata_required) / sizeof(mcu_link_git_metadata_required[0]));
    failures += file_contains_all(root, "CMakeLists.txt", esp32_build_info_cmake_required,
                                  sizeof(esp32_build_info_cmake_required) / sizeof(esp32_build_info_cmake_required[0]));
    failures += file_contains_all(root, "../sdkconfig.defaults", sdkconfig_touch_perf_required,
                                  sizeof(sdkconfig_touch_perf_required) / sizeof(sdkconfig_touch_perf_required[0]));
    failures +=
        file_contains_all(root, "../sdkconfig.defaults", sdkconfig_voice_resource_required,
                          sizeof(sdkconfig_voice_resource_required) / sizeof(sdkconfig_voice_resource_required[0]));
    failures +=
        file_contains_all(root, "../components/hal/hal_display/src/hal_display.c", hal_display_text_lock_required,
                          sizeof(hal_display_text_lock_required) / sizeof(hal_display_text_lock_required[0]));
    failures +=
        function_contains_all(root, "../components/hal/hal_display/src/hal_display.c", "void hal_display_ui_deinit",
                              hal_display_deinit_lock_required,
                              sizeof(hal_display_deinit_lock_required) / sizeof(hal_display_deinit_lock_required[0]));
    failures += function_contains_all(root, "../components/hal/hal_display/src/hal_display.c",
                                      "void hal_display_voice_connect_status_clear(void)",
                                      hal_display_voice_connect_public_clear_required,
                                      sizeof(hal_display_voice_connect_public_clear_required) /
                                          sizeof(hal_display_voice_connect_public_clear_required[0]));
    if (file_exists(root, "../managed_components/lvgl__lvgl/src/hal/lv_hal_indev.h")) {
        failures += file_contains_all(root, "../managed_components/lvgl__lvgl/src/hal/lv_hal_indev.h",
                                      lvgl_indev_factory_thresholds,
                                      sizeof(lvgl_indev_factory_thresholds) / sizeof(lvgl_indev_factory_thresholds[0]));
    }
    failures +=
        file_contains_all(root, "../components/sensecap-watcher/Kconfig", watcher_kconfig_factory_defaults,
                          sizeof(watcher_kconfig_factory_defaults) / sizeof(watcher_kconfig_factory_defaults[0]));
    failures +=
        file_contains_all(root, "../components/sensecap-watcher/idf_component.yml", touch_component_versions_required,
                          sizeof(touch_component_versions_required) / sizeof(touch_component_versions_required[0]));
    if (file_exists(root, "../dependencies.lock")) {
        failures +=
            file_contains_all(root, "../dependencies.lock", dependency_lock_versions_required,
                              sizeof(dependency_lock_versions_required) / sizeof(dependency_lock_versions_required[0]));
    }
    failures +=
        file_contains_all(root, "../components/esp_lvgl_port/idf_component.yml", esp_lvgl_port_component_required,
                          sizeof(esp_lvgl_port_component_required) / sizeof(esp_lvgl_port_component_required[0]));
    failures +=
        file_contains_all(root, "../components/hal/hal_display/src/hal_display.c", hal_display_bsp_input_required,
                          sizeof(hal_display_bsp_input_required) / sizeof(hal_display_bsp_input_required[0]));
    failures +=
        file_contains_none(root, "../components/hal/hal_display/src/hal_display.c", forbidden_hal_touch_registration,
                           sizeof(forbidden_hal_touch_registration) / sizeof(forbidden_hal_touch_registration[0]));

    if (failures != 0) {
        fprintf(stderr, "%d factory touch parity static check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
