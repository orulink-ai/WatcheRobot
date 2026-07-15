#include "sdk_control_ui.h"

#include "factory_home_ui/ui.h"

#include <stdio.h>
#include <string.h>

#define SDK_CONTROL_UI_GREEN 0xA9DE2C
#define SDK_CONTROL_UI_AMBER 0xE1B94B
#define SDK_CONTROL_UI_WHITE 0xFFFFFF
#define SDK_CONTROL_UI_MUTED 0x8B918D
#define SDK_CONTROL_UI_DIM 0x838A85
#define SDK_CONTROL_UI_PANEL 0x141815
#define SDK_CONTROL_UI_PANEL_BORDER 0x2A302C
#define SDK_CONTROL_UI_BACKGROUND 0x030504
#define SDK_CONTROL_UI_GREEN_BACKGROUND 0x0B1D08
#define SDK_CONTROL_UI_AMBER_BACKGROUND 0x1C1407
#define SDK_CONTROL_UI_GREEN_EMBLEM_TOP 0x16240E
#define SDK_CONTROL_UI_GREEN_EMBLEM_BOTTOM 0x080D06
#define SDK_CONTROL_UI_AMBER_EMBLEM_TOP 0x251B0B
#define SDK_CONTROL_UI_AMBER_EMBLEM_BOTTOM 0x0D0A05

typedef enum {
    SDK_CONTROL_EMBLEM_PROMPT = 0,
    SDK_CONTROL_EMBLEM_CHECK,
    SDK_CONTROL_EMBLEM_CLOSE,
} sdk_control_emblem_icon_t;

static const lv_point_t s_check_points[] = {
    {0, 22},
    {16, 38},
    {52, 0},
};
static const lv_point_t s_close_stroke_a_points[] = {
    {0, 0},
    {38, 38},
};
static const lv_point_t s_close_stroke_b_points[] = {
    {38, 0},
    {0, 38},
};

static void clear_container_style(lv_obj_t *object) {
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_outline_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_centered_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, 350);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static void set_hidden(lv_obj_t *object, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_screen_tone(sdk_control_ui_t *ui, uint32_t accent) {
    const uint32_t background_top =
        accent == SDK_CONTROL_UI_AMBER ? SDK_CONTROL_UI_AMBER_BACKGROUND : SDK_CONTROL_UI_GREEN_BACKGROUND;

    lv_obj_set_style_bg_color(ui->screen, lv_color_hex(background_top), 0);
    lv_obj_set_style_bg_grad_color(ui->screen, lv_color_hex(SDK_CONTROL_UI_BACKGROUND), 0);
    lv_obj_set_style_bg_grad_dir(ui->screen, LV_GRAD_DIR_VER, 0);
}

static void set_vector_line(lv_obj_t *line, const lv_point_t *points, uint16_t point_count, uint32_t color,
                            lv_coord_t width) {
    lv_line_set_points(line, points, point_count);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_center(line);
    set_hidden(line, false);
}

static void set_emblem_icon(sdk_control_ui_t *ui, uint32_t color, sdk_control_emblem_icon_t icon) {
    set_hidden(ui->icon_prompt, true);
    set_hidden(ui->icon_prompt_bar, true);
    set_hidden(ui->icon_stroke_a, true);
    set_hidden(ui->icon_stroke_b, true);

    if (icon == SDK_CONTROL_EMBLEM_PROMPT) {
        lv_label_set_text(ui->icon_prompt, ">");
        lv_obj_set_style_text_color(ui->icon_prompt, lv_color_hex(SDK_CONTROL_UI_WHITE), 0);
        lv_obj_set_style_text_font(ui->icon_prompt, &ui_font_semibold28, 0);
        lv_obj_align(ui->icon_prompt, LV_ALIGN_CENTER, -10, -2);
        lv_obj_set_style_bg_color(ui->icon_prompt_bar, lv_color_hex(color), 0);
        lv_obj_align(ui->icon_prompt_bar, LV_ALIGN_CENTER, 16, 3);
        set_hidden(ui->icon_prompt, false);
        set_hidden(ui->icon_prompt_bar, false);
        return;
    }

    if (icon == SDK_CONTROL_EMBLEM_CHECK) {
        set_vector_line(ui->icon_stroke_a, s_check_points,
                        (uint16_t)(sizeof(s_check_points) / sizeof(s_check_points[0])), color, 9);
        lv_obj_align(ui->icon_stroke_a, LV_ALIGN_CENTER, 0, -1);
        return;
    }

    set_vector_line(ui->icon_stroke_a, s_close_stroke_a_points,
                    (uint16_t)(sizeof(s_close_stroke_a_points) / sizeof(s_close_stroke_a_points[0])), color, 7);
    set_vector_line(ui->icon_stroke_b, s_close_stroke_b_points,
                    (uint16_t)(sizeof(s_close_stroke_b_points) / sizeof(s_close_stroke_b_points[0])), color, 7);
}

static void set_emblem(sdk_control_ui_t *ui, uint32_t color, sdk_control_emblem_icon_t icon, int size, int y) {
    const bool amber = color == SDK_CONTROL_UI_AMBER;
    const uint32_t emblem_top = amber ? SDK_CONTROL_UI_AMBER_EMBLEM_TOP : SDK_CONTROL_UI_GREEN_EMBLEM_TOP;
    const uint32_t emblem_bottom =
        amber ? SDK_CONTROL_UI_AMBER_EMBLEM_BOTTOM : SDK_CONTROL_UI_GREEN_EMBLEM_BOTTOM;

    lv_obj_set_size(ui->emblem, size, size);
    lv_obj_align(ui->emblem, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(ui->emblem, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui->emblem, lv_color_hex(emblem_top), 0);
    lv_obj_set_style_bg_grad_color(ui->emblem, lv_color_hex(emblem_bottom), 0);
    lv_obj_set_style_bg_grad_dir(ui->emblem, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ui->emblem, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui->emblem, 2, 0);
    lv_obj_set_style_border_color(ui->emblem, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_color(ui->emblem, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(ui->emblem, 28, 0);
    lv_obj_set_style_shadow_spread(ui->emblem, 3, 0);
    lv_obj_set_style_shadow_opa(ui->emblem, LV_OPA_50, 0);

    set_emblem_icon(ui, color, icon);
}

static void prepare_state(sdk_control_ui_t *ui) {
    set_hidden(ui->headline, true);
    set_hidden(ui->caption, true);
    set_hidden(ui->code, true);
    set_hidden(ui->status_pill, true);
    set_hidden(ui->detail, true);
    lv_obj_set_style_text_color(ui->headline, lv_color_hex(SDK_CONTROL_UI_WHITE), 0);
    lv_obj_set_style_text_font(ui->headline, &ui_font_semibold28, 0);
    lv_obj_set_style_bg_color(ui->status_dot, lv_color_hex(SDK_CONTROL_UI_GREEN), 0);
    lv_obj_set_style_border_color(ui->status_pill, lv_color_hex(SDK_CONTROL_UI_PANEL_BORDER), 0);
}

static void show_status_pill(sdk_control_ui_t *ui, const char *status, int width, int y, uint32_t color) {
    lv_obj_set_size(ui->status_pill, width, 42);
    lv_obj_align(ui->status_pill, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(ui->status_pill, lv_color_hex(SDK_CONTROL_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(ui->status_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui->status_pill, 1, 0);
    lv_obj_set_style_border_color(ui->status_pill, lv_color_hex(SDK_CONTROL_UI_PANEL_BORDER), 0);
    lv_obj_set_style_radius(ui->status_pill, 21, 0);
    lv_obj_set_style_bg_color(ui->status_dot, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(ui->status, &lv_font_montserrat_14, 0);
    lv_label_set_text(ui->status, status);
    set_hidden(ui->status_pill, false);
}

static void show_inline_status(sdk_control_ui_t *ui, const char *status, int width, int y, uint32_t color) {
    lv_obj_set_size(ui->status_pill, width, 42);
    lv_obj_align(ui->status_pill, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(ui->status_pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->status_pill, 0, 0);
    lv_obj_set_style_bg_color(ui->status_dot, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(ui->status, &lv_font_montserrat_20, 0);
    lv_label_set_text(ui->status, status);
    set_hidden(ui->status_pill, false);
}

bool sdk_control_ui_build(sdk_control_ui_t *ui, lv_obj_t *screen) {
    if (ui == NULL || screen == NULL) {
        return false;
    }

    memset(ui, 0, sizeof(*ui));
    ui->screen = screen;
    ui->state = SDK_CONTROL_UI_WAITING;
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(SDK_CONTROL_UI_BACKGROUND), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x111512), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    ui->title = create_centered_label(screen, &ui_font_Font12, SDK_CONTROL_UI_MUTED);
    lv_label_set_text(ui->title, "PYTHON SDK");
    lv_obj_set_style_text_letter_space(ui->title, 2, 0);
    lv_obj_align(ui->title, LV_ALIGN_TOP_MID, 0, 29);

    ui->emblem = lv_obj_create(screen);
    clear_container_style(ui->emblem);
    ui->icon_prompt = lv_label_create(ui->emblem);
    lv_obj_set_style_text_font(ui->icon_prompt, &ui_font_semibold28, 0);
    ui->icon_prompt_bar = lv_obj_create(ui->emblem);
    clear_container_style(ui->icon_prompt_bar);
    lv_obj_set_size(ui->icon_prompt_bar, 18, 5);
    lv_obj_set_style_radius(ui->icon_prompt_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui->icon_prompt_bar, LV_OPA_COVER, 0);
    ui->icon_stroke_a = lv_line_create(ui->emblem);
    ui->icon_stroke_b = lv_line_create(ui->emblem);

    ui->headline = create_centered_label(screen, &ui_font_semibold28, SDK_CONTROL_UI_WHITE);
    ui->caption = create_centered_label(screen, &lv_font_montserrat_20, SDK_CONTROL_UI_MUTED);
    ui->code = create_centered_label(screen, &ui_font_semibold42, SDK_CONTROL_UI_WHITE);

    ui->status_pill = lv_obj_create(screen);
    clear_container_style(ui->status_pill);
    ui->status_dot = lv_obj_create(ui->status_pill);
    clear_container_style(ui->status_dot);
    lv_obj_set_size(ui->status_dot, 10, 10);
    lv_obj_set_style_radius(ui->status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui->status_dot, LV_OPA_COVER, 0);
    lv_obj_align(ui->status_dot, LV_ALIGN_LEFT_MID, 24, 0);
    ui->status = lv_label_create(ui->status_pill);
    lv_obj_set_style_text_font(ui->status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ui->status, lv_color_hex(0xE9EDEA), 0);
    lv_obj_align(ui->status, LV_ALIGN_CENTER, 14, 0);

    ui->detail = create_centered_label(screen, &lv_font_montserrat_14, SDK_CONTROL_UI_DIM);
    ui->built = true;
    sdk_control_ui_show_pairing(ui, "------", false);
    return true;
}

void sdk_control_ui_show_pairing(sdk_control_ui_t *ui, const char *pairing_code, bool reconnecting) {
    char formatted_code[8] = "--- ---";
    const char *safe_code = pairing_code != NULL ? pairing_code : "------";

    if (ui == NULL || !ui->built) {
        return;
    }
    if (strlen(safe_code) == 6U) {
        (void)snprintf(formatted_code, sizeof(formatted_code), "%.3s %.3s", safe_code, safe_code + 3);
    }

    prepare_state(ui);
    ui->state = reconnecting ? SDK_CONTROL_UI_RECONNECTING : SDK_CONTROL_UI_WAITING;
    set_screen_tone(ui, reconnecting ? SDK_CONTROL_UI_AMBER : SDK_CONTROL_UI_GREEN);
    if (reconnecting) {
        set_emblem(ui, SDK_CONTROL_UI_AMBER, SDK_CONTROL_EMBLEM_CLOSE, 90, 69);
        lv_label_set_text(ui->headline, "Connection lost");
        lv_obj_set_style_text_color(ui->headline, lv_color_hex(SDK_CONTROL_UI_AMBER), 0);
        lv_obj_set_style_text_font(ui->headline, &lv_font_montserrat_20, 0);
        lv_obj_align(ui->headline, LV_ALIGN_TOP_MID, 0, 183);
        set_hidden(ui->headline, false);
        lv_label_set_text(ui->caption, "NEW PAIR CODE");
        lv_obj_align(ui->caption, LV_ALIGN_TOP_MID, 0, 220);
        lv_obj_align(ui->code, LV_ALIGN_TOP_MID, 0, 249);
        show_inline_status(ui, "Waiting to reconnect", 286, 300, SDK_CONTROL_UI_AMBER);
        lv_label_set_text(ui->detail, "The previous session was closed safely");
        lv_obj_align(ui->detail, LV_ALIGN_TOP_MID, 0, 349);
    } else {
        set_emblem(ui, SDK_CONTROL_UI_GREEN, SDK_CONTROL_EMBLEM_PROMPT, 96, 79);
        lv_label_set_text(ui->caption, "PAIR CODE");
        lv_obj_align(ui->caption, LV_ALIGN_TOP_MID, 0, 192);
        lv_obj_align(ui->code, LV_ALIGN_TOP_MID, 0, 222);
        show_inline_status(ui, "Waiting for desktop", 270, 286, SDK_CONTROL_UI_GREEN);
        lv_label_set_text(ui->detail, "Run watcherobot on the same Wi-Fi");
        lv_obj_align(ui->detail, LV_ALIGN_TOP_MID, 0, 348);
    }
    lv_label_set_text(ui->code, formatted_code);
    set_hidden(ui->caption, false);
    set_hidden(ui->code, false);
    set_hidden(ui->detail, false);
}

void sdk_control_ui_show_connected(sdk_control_ui_t *ui) {
    if (ui == NULL || !ui->built) {
        return;
    }

    prepare_state(ui);
    ui->state = SDK_CONTROL_UI_CONNECTED;
    set_screen_tone(ui, SDK_CONTROL_UI_GREEN);
    set_emblem(ui, SDK_CONTROL_UI_GREEN, SDK_CONTROL_EMBLEM_CHECK, 148, 64);
    lv_label_set_text(ui->headline, "Connected");
    lv_obj_align(ui->headline, LV_ALIGN_TOP_MID, 0, 222);
    set_hidden(ui->headline, false);
    lv_label_set_text(ui->caption, "Desktop control active");
    lv_obj_align(ui->caption, LV_ALIGN_TOP_MID, 0, 263);
    set_hidden(ui->caption, false);
    show_status_pill(ui, "Ready for commands", 252, 315, SDK_CONTROL_UI_GREEN);
}

void sdk_control_ui_show_error(sdk_control_ui_t *ui, const char *headline, const char *detail) {
    if (ui == NULL || !ui->built) {
        return;
    }

    prepare_state(ui);
    ui->state = SDK_CONTROL_UI_ERROR;
    set_screen_tone(ui, SDK_CONTROL_UI_AMBER);
    set_emblem(ui, SDK_CONTROL_UI_AMBER, SDK_CONTROL_EMBLEM_CLOSE, 96, 76);
    lv_label_set_text(ui->headline, headline != NULL ? headline : "Unable to start");
    lv_obj_set_style_text_color(ui->headline, lv_color_hex(SDK_CONTROL_UI_AMBER), 0);
    lv_obj_align(ui->headline, LV_ALIGN_TOP_MID, 0, 218);
    set_hidden(ui->headline, false);
    show_status_pill(ui, "Python SDK unavailable", 286, 274, SDK_CONTROL_UI_AMBER);
    lv_label_set_text(ui->detail, detail != NULL ? detail : "Please reopen the app");
    lv_obj_align(ui->detail, LV_ALIGN_TOP_MID, 0, 331);
    set_hidden(ui->detail, false);
}

bool sdk_control_ui_is_attached(const sdk_control_ui_t *ui, const lv_obj_t *screen) {
    return ui != NULL && ui->built && ui->screen != NULL && ui->screen == screen;
}

void sdk_control_ui_reset(sdk_control_ui_t *ui) {
    if (ui != NULL) {
        memset(ui, 0, sizeof(*ui));
    }
}
