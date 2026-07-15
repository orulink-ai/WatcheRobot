/**
 * @file hal_display.c
 * @brief Display HAL implementation for LVGL surfaces, text, and input devices.
 */

#include "hal_display.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sensecap-watcher.h"

#include <string.h>

/* PNG support is included via lvgl.h when LV_USE_PNG is enabled */

/* External CJK font for Chinese character support */
#if LV_FONT_SIMSUN_16_CJK
extern const lv_font_t lv_font_simsun_16_cjk;
#endif

#if LV_FONT_MONTSERRAT_14
extern const lv_font_t lv_font_montserrat_14;
#endif

#if LV_FONT_MONTSERRAT_20
extern const lv_font_t lv_font_montserrat_20;
#endif

#if LV_FONT_MONTSERRAT_22
extern const lv_font_t lv_font_montserrat_22;
#endif

#if LV_FONT_MONTSERRAT_24
extern const lv_font_t lv_font_montserrat_24;
#endif

#include "esp_lvgl_port.h"

#define TAG "HAL_DISPLAY"
#define GENERAL_I2C_RECOVERY_PULSES 9
#define GENERAL_I2C_MAX_PREPARE_ATTEMPTS 3
#define GENERAL_I2C_BITBANG_DELAY_US 8
static lv_obj_t *label_text = NULL;
static lv_obj_t *img_emoji = NULL;
static lv_obj_t *text_overlay = NULL;
static lv_obj_t *voice_connect_panel = NULL;
static lv_obj_t *voice_connect_spinner = NULL;
static lv_obj_t *voice_connect_title_label = NULL;
static lv_obj_t *voice_connect_detail_label = NULL;
static lv_obj_t *voice_connect_action_button = NULL;
static lv_obj_t *voice_connect_action_label = NULL;
static hal_display_voice_connect_action_cb_t voice_connect_action_cb = NULL;
static void *voice_connect_action_user_ctx = NULL;
static int64_t voice_connect_action_last_click_us = 0;
static lv_obj_t *s_retained_previous_screen_once = NULL;
static lv_obj_t *s_behavior_screen = NULL;
static bool minimal_initialized = false;
static bool is_initialized = false;
static bool inputs_initialized = false;
static lv_disp_t *s_display = NULL;
static lv_indev_t *s_knob_indev = NULL;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;

esp_lcd_panel_handle_t hal_display_get_panel_handle(void) {
    if (s_panel_handle == NULL) {
        return bsp_lcd_get_panel_handle();
    }
    return s_panel_handle;
}

void hal_display_invalidate_text_overlay(void) {
    if (text_overlay != NULL && !lv_obj_has_flag(text_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(text_overlay);
    } else if (label_text != NULL && !lv_obj_has_flag(label_text, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(label_text);
    }
}

bool hal_display_get_text_overlay_bounds(int *x1, int *y1, int *x2, int *y2) {
    if (text_overlay == NULL || lv_obj_has_flag(text_overlay, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (x1 == NULL || y1 == NULL || x2 == NULL || y2 == NULL) {
        return false;
    }

    lv_area_t area;
    lv_obj_get_coords(text_overlay, &area);
    *x1 = area.x1;
    *y1 = area.y1;
    *x2 = area.x2 + 1;
    *y2 = area.y2 + 1;
    return *x1 < *x2 && *y1 < *y2;
}

static bool hal_display_text_has_non_ascii(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL) {
        return false;
    }

    while (*cursor != '\0') {
        if (*cursor > 0x7F) {
            return true;
        }
        cursor++;
    }

    return false;
}

static const lv_font_t *hal_display_select_text_font(const char *text, int font_size) {
    bool use_cjk_font = hal_display_text_has_non_ascii(text);

#if LV_FONT_SIMSUN_16_CJK
    if (use_cjk_font) {
        return &lv_font_simsun_16_cjk;
    }
#else
    (void)use_cjk_font;
#endif

    if (font_size == 22) {
#if LV_FONT_MONTSERRAT_22
        return &lv_font_montserrat_22;
#endif
    } else if (font_size == 20) {
#if LV_FONT_MONTSERRAT_20
        return &lv_font_montserrat_20;
#endif
    } else if (font_size == 0 || font_size >= 24) {
#if LV_FONT_MONTSERRAT_24
        return &lv_font_montserrat_24;
#endif
    }

#if LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#elif LV_FONT_SIMSUN_16_CJK
    return &lv_font_simsun_16_cjk;
#else
    return LV_FONT_DEFAULT;
#endif
}

static void hal_display_update_text_overlay_visibility_locked(const char *text) {
    if (text_overlay == NULL) {
        return;
    }

    if (text != NULL && text[0] != '\0') {
        lv_obj_clear_flag(text_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(text_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool hal_display_text_target_ready_locked(void) {
    if (!is_initialized || label_text == NULL || !lv_obj_is_valid(label_text)) {
        ESP_LOGW(TAG, "Display text target unavailable");
        return false;
    }

    if (text_overlay != NULL && !lv_obj_is_valid(text_overlay)) {
        ESP_LOGW(TAG, "Display text overlay is stale; continuing without overlay");
        text_overlay = NULL;
    }

    return true;
}

static void hal_display_apply_text_style_locked(const char *text, int font_size, bool alert_text) {
    const lv_font_t *font = hal_display_select_text_font(text, font_size);
    lv_color_t text_color = alert_text ? lv_palette_main(LV_PALETTE_RED) : lv_color_white();

    lv_obj_set_style_text_font(label_text, font, 0);
    lv_obj_set_style_text_color(label_text, text_color, 0);
    if (text_overlay != NULL) {
        lv_obj_set_style_bg_color(text_overlay, alert_text ? lv_palette_darken(LV_PALETTE_RED, 4) : lv_color_black(),
                                  0);
        lv_obj_set_style_bg_opa(text_overlay, LV_OPA_70, 0);
        lv_obj_set_style_border_color(text_overlay,
                                      alert_text ? lv_palette_lighten(LV_PALETTE_RED, 1) : lv_color_hex(0x303030), 0);
    }
}

static void hal_display_general_i2c_delay(void) {
    esp_rom_delay_us(GENERAL_I2C_BITBANG_DELAY_US);
}

static esp_err_t hal_display_general_i2c_bus_mode(bool output_mode, int sda_level, int scl_level) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BSP_GENERAL_I2C_SDA) | (1ULL << BSP_GENERAL_I2C_SCL),
        .mode = output_mode ? GPIO_MODE_INPUT_OUTPUT_OD : GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    if (output_mode) {
        gpio_set_level(BSP_GENERAL_I2C_SDA, sda_level);
        gpio_set_level(BSP_GENERAL_I2C_SCL, scl_level);
        hal_display_general_i2c_delay();
    }

    return ESP_OK;
}

static void hal_display_general_i2c_stop(void) {
    (void)hal_display_general_i2c_bus_mode(true, 0, 0);
    gpio_set_level(BSP_GENERAL_I2C_SDA, 0);
    gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    hal_display_general_i2c_delay();
}

static void hal_display_general_i2c_start(void) {
    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SDA, 0);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
    hal_display_general_i2c_delay();
}

static bool hal_display_general_i2c_write_byte(uint8_t value) {
    int bit;

    for (bit = 7; bit >= 0; --bit) {
        gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
        gpio_set_level(BSP_GENERAL_I2C_SDA, (value >> bit) & 0x1);
        hal_display_general_i2c_delay();
        gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
        hal_display_general_i2c_delay();
    }

    gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
    hal_display_general_i2c_delay();
    return gpio_get_level(BSP_GENERAL_I2C_SDA) == 0;
}

static uint8_t hal_display_general_i2c_read_byte(bool ack) {
    uint8_t value = 0;
    int bit;

    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    for (bit = 7; bit >= 0; --bit) {
        gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
        hal_display_general_i2c_delay();
        gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
        hal_display_general_i2c_delay();
        if (gpio_get_level(BSP_GENERAL_I2C_SDA) != 0) {
            value |= (uint8_t)(1U << bit);
        }
    }

    gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
    gpio_set_level(BSP_GENERAL_I2C_SDA, ack ? 0 : 1);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
    hal_display_general_i2c_delay();
    gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    hal_display_general_i2c_delay();
    return value;
}

static bool hal_display_general_i2c_read_input_reg(uint16_t *out_value) {
    uint8_t low = 0;
    uint8_t high = 0;

    if (out_value == NULL) {
        return false;
    }
    if (hal_display_general_i2c_bus_mode(true, 1, 1) != ESP_OK) {
        return false;
    }

    hal_display_general_i2c_start();
    if (!hal_display_general_i2c_write_byte((uint8_t)(ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_001 << 1))) {
        hal_display_general_i2c_stop();
        return false;
    }
    if (!hal_display_general_i2c_write_byte(0x00)) {
        hal_display_general_i2c_stop();
        return false;
    }

    hal_display_general_i2c_start();
    if (!hal_display_general_i2c_write_byte((uint8_t)((ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_001 << 1) | 0x1))) {
        hal_display_general_i2c_stop();
        return false;
    }

    low = hal_display_general_i2c_read_byte(true);
    high = hal_display_general_i2c_read_byte(false);
    hal_display_general_i2c_stop();
    *out_value = ((uint16_t)high << 8) | low;
    return true;
}

static void hal_display_recover_general_i2c_bus(void) {
    int pulse;

    if (hal_display_general_i2c_bus_mode(true, 1, 1) != ESP_OK) {
        return;
    }

    gpio_set_level(BSP_GENERAL_I2C_SDA, 1);
    for (pulse = 0; pulse < GENERAL_I2C_RECOVERY_PULSES; ++pulse) {
        gpio_set_level(BSP_GENERAL_I2C_SCL, 0);
        hal_display_general_i2c_delay();
        gpio_set_level(BSP_GENERAL_I2C_SCL, 1);
        hal_display_general_i2c_delay();
    }

    hal_display_general_i2c_stop();
    (void)hal_display_general_i2c_bus_mode(false, 1, 1);
}

static bool hal_display_prepare_general_i2c_bus(void) {
    int attempt;

    for (attempt = 1; attempt <= GENERAL_I2C_MAX_PREPARE_ATTEMPTS; ++attempt) {
        int sda_level;
        int scl_level;
        bool read_ok;
        uint16_t input_reg = 0;

        if (hal_display_general_i2c_bus_mode(false, 1, 1) != ESP_OK) {
            ESP_LOGW(TAG, "General I2C preflight failed to configure GPIOs");
            return false;
        }

        sda_level = gpio_get_level(BSP_GENERAL_I2C_SDA);
        scl_level = gpio_get_level(BSP_GENERAL_I2C_SCL);
        read_ok = hal_display_general_i2c_read_input_reg(&input_reg);

        ESP_LOGI(TAG, "General I2C preflight attempt %d/%d: SDA=%d SCL=%d read_ok=%d input_reg=0x%04x", attempt,
                 GENERAL_I2C_MAX_PREPARE_ATTEMPTS, sda_level, scl_level, read_ok ? 1 : 0, (unsigned int)input_reg);

        if (read_ok) {
            gpio_reset_pin(BSP_GENERAL_I2C_SDA);
            gpio_reset_pin(BSP_GENERAL_I2C_SCL);
            return true;
        }

        hal_display_recover_general_i2c_bus();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    gpio_reset_pin(BSP_GENERAL_I2C_SDA);
    gpio_reset_pin(BSP_GENERAL_I2C_SCL);
    return false;
}

static void hal_display_raise_text_overlay_locked(void) {
    if (text_overlay != NULL) {
        lv_obj_move_foreground(text_overlay);
    } else if (label_text != NULL) {
        lv_obj_move_foreground(label_text);
    }
}

static void hal_display_create_text_overlay_locked(lv_obj_t *parent, const char *initial_text) {
    text_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(text_overlay);
    lv_obj_set_width(text_overlay, 388);
    lv_obj_set_height(text_overlay, LV_SIZE_CONTENT);
    lv_obj_clear_flag(text_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(text_overlay, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(text_overlay, 18, 0);
    lv_obj_set_style_pad_left(text_overlay, 16, 0);
    lv_obj_set_style_pad_right(text_overlay, 16, 0);
    lv_obj_set_style_pad_top(text_overlay, 12, 0);
    lv_obj_set_style_pad_bottom(text_overlay, 12, 0);
    lv_obj_set_style_bg_color(text_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(text_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(text_overlay, 1, 0);
    lv_obj_set_style_border_color(text_overlay, lv_color_hex(0x303030), 0);
    lv_obj_align(text_overlay, LV_ALIGN_TOP_MID, 0, 18);

    label_text = lv_label_create(text_overlay);
    lv_obj_set_width(label_text, LV_PCT(100));
    lv_label_set_long_mode(label_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label_text, initial_text != NULL ? initial_text : "");
    lv_obj_set_style_text_color(label_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_text, hal_display_select_text_font(initial_text, 24), 0);

    hal_display_update_text_overlay_visibility_locked(initial_text);
    hal_display_raise_text_overlay_locked();
}

static void hal_display_voice_connect_status_clear_locked(void) {
    if (voice_connect_panel != NULL && lv_obj_is_valid(voice_connect_panel)) {
        lv_obj_del(voice_connect_panel);
    }
    voice_connect_panel = NULL;
    voice_connect_spinner = NULL;
    voice_connect_title_label = NULL;
    voice_connect_detail_label = NULL;
    voice_connect_action_button = NULL;
    voice_connect_action_label = NULL;
}

static void hal_display_reset_voice_connect_status_handles_locked(void) {
    voice_connect_panel = NULL;
    voice_connect_spinner = NULL;
    voice_connect_title_label = NULL;
    voice_connect_detail_label = NULL;
    voice_connect_action_button = NULL;
    voice_connect_action_label = NULL;
}

static lv_obj_t *hal_display_create_voice_connect_label_locked(lv_obj_t *parent, int width, const char *text,
                                                               int font_size, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, hal_display_select_text_font(text, font_size), 0);
    lv_obj_set_style_text_line_space(label, 5, 0);
    lv_label_set_text(label, text != NULL ? text : "");
    return label;
}

static const char *hal_display_action_button_text(const char *action) {
    static const char prefix[] = "Button: ";

    if (action == NULL) {
        return "";
    }

    if (strncmp(action, prefix, sizeof(prefix) - 1U) == 0) {
        return action + sizeof(prefix) - 1U;
    }

    return action;
}

static void hal_display_voice_connect_action_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us - voice_connect_action_last_click_us < 300000LL) {
        return;
    }
    voice_connect_action_last_click_us = now_us;

    ESP_LOGI(TAG, "Voice connect action event code=%d has_cb=%d", (int)code, voice_connect_action_cb != NULL);
    if (voice_connect_action_cb != NULL) {
        voice_connect_action_cb(voice_connect_action_user_ctx);
    }
}

static bool hal_display_ensure_voice_connect_status_locked(bool show_spinner, bool alert) {
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_color_t accent = alert ? lv_palette_main(LV_PALETTE_ORANGE) : lv_color_hex(0x2F80FF);

    if (scr == NULL) {
        return false;
    }

    if (voice_connect_panel != NULL && !lv_obj_is_valid(voice_connect_panel)) {
        hal_display_reset_voice_connect_status_handles_locked();
    }

    if (voice_connect_panel == NULL) {
        voice_connect_panel = lv_obj_create(scr);
        if (voice_connect_panel == NULL) {
            return false;
        }
        lv_obj_remove_style_all(voice_connect_panel);
        lv_obj_set_size(voice_connect_panel, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(voice_connect_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(voice_connect_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_layout(voice_connect_panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(voice_connect_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(voice_connect_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(voice_connect_panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(voice_connect_panel, 0, 0);
        lv_obj_set_style_pad_all(voice_connect_panel, 0, 0);
        lv_obj_set_style_pad_row(voice_connect_panel, 12, 0);
        lv_obj_align(voice_connect_panel, LV_ALIGN_CENTER, 0, 0);
    }

    if (show_spinner && voice_connect_spinner == NULL) {
        voice_connect_spinner = lv_spinner_create(voice_connect_panel, 900, 70);
        if (voice_connect_spinner == NULL) {
            return false;
        }
        lv_obj_set_size(voice_connect_spinner, 44, 44);
        lv_obj_set_style_arc_width(voice_connect_spinner, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(voice_connect_spinner, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(voice_connect_spinner, lv_color_hex(0x303030), LV_PART_MAIN);
    }

    if (voice_connect_spinner != NULL && lv_obj_is_valid(voice_connect_spinner)) {
        lv_obj_set_style_arc_color(voice_connect_spinner, accent, LV_PART_INDICATOR);
        if (show_spinner) {
            lv_obj_clear_flag(voice_connect_spinner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(voice_connect_spinner, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (voice_connect_title_label == NULL) {
        voice_connect_title_label =
            hal_display_create_voice_connect_label_locked(voice_connect_panel, 340, "", 22, lv_color_white());
    }
    if (voice_connect_detail_label == NULL) {
        voice_connect_detail_label =
            hal_display_create_voice_connect_label_locked(voice_connect_panel, 340, "", 14, lv_color_hex(0xB8C0CC));
    }
    if (voice_connect_action_button == NULL) {
        voice_connect_action_button = lv_btn_create(voice_connect_panel);
        if (voice_connect_action_button == NULL) {
            return false;
        }
        lv_obj_set_size(voice_connect_action_button, 160, 56);
        lv_obj_set_style_radius(voice_connect_action_button, 24, 0);
        lv_obj_set_style_bg_color(voice_connect_action_button, lv_color_hex(0x2A3038), 0);
        lv_obj_set_style_border_width(voice_connect_action_button, 2, 0);
        lv_obj_set_style_border_color(voice_connect_action_button, lv_color_hex(0x7DFFD6), 0);
        lv_obj_set_style_shadow_color(voice_connect_action_button, lv_color_hex(0x7DFFD6), 0);
        lv_obj_set_style_shadow_width(voice_connect_action_button, 0, 0);
        lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(voice_connect_action_button, 18);
        lv_obj_add_event_cb(voice_connect_action_button, hal_display_voice_connect_action_event_cb, LV_EVENT_PRESSED,
                            NULL);
        lv_obj_add_event_cb(voice_connect_action_button, hal_display_voice_connect_action_event_cb, LV_EVENT_CLICKED,
                            NULL);

        voice_connect_action_label = lv_label_create(voice_connect_action_button);
        if (voice_connect_action_label == NULL) {
            return false;
        }
        lv_obj_set_style_text_align(voice_connect_action_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(voice_connect_action_label, lv_color_white(), 0);
        lv_obj_clear_flag(voice_connect_action_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(voice_connect_action_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(voice_connect_action_label);
    }

    return voice_connect_title_label != NULL && voice_connect_detail_label != NULL &&
           voice_connect_action_button != NULL && voice_connect_action_label != NULL;
}

int hal_display_voice_connect_status_set(const char *title, const char *detail, const char *action, bool show_spinner,
                                         bool alert) {
    if (!is_initialized) {
        ESP_LOGW(TAG, "Display not initialized for voice connect status");
        return -1;
    }

    bool lvgl_locked = lvgl_port_lock(0);
    if (!lvgl_locked) {
        ESP_LOGW(TAG, "Failed to lock LVGL for voice connect status");
        return -1;
    }

    if (!hal_display_ensure_voice_connect_status_locked(show_spinner, alert)) {
        lvgl_port_unlock();
        ESP_LOGW(TAG, "Failed to create voice connect status page");
        return -1;
    }

    const char *safe_title = title != NULL ? title : "";
    const char *safe_detail = detail != NULL ? detail : "";
    const char *safe_action = action != NULL ? action : "";
    const char *display_action = hal_display_action_button_text(safe_action);

    if (label_text != NULL && lv_obj_is_valid(label_text)) {
        lv_label_set_text(label_text, "");
    }
    hal_display_update_text_overlay_visibility_locked("");

    lv_obj_set_style_text_font(voice_connect_title_label, hal_display_select_text_font(safe_title, 22), 0);
    lv_obj_set_style_text_font(voice_connect_detail_label, hal_display_select_text_font(safe_detail, 14), 0);
    lv_obj_set_style_text_font(voice_connect_action_label, hal_display_select_text_font(display_action, 20), 0);
    lv_obj_set_style_text_color(voice_connect_detail_label,
                                alert ? lv_palette_lighten(LV_PALETTE_ORANGE, 2) : lv_color_hex(0xB8C0CC), 0);
    lv_obj_set_style_text_color(voice_connect_action_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(voice_connect_action_button, lv_color_hex(0x0F8F68), 0);
    lv_obj_set_style_border_color(voice_connect_action_button, lv_color_hex(0xA8FFE8), 0);
    lv_obj_set_style_shadow_color(voice_connect_action_button, lv_color_hex(0x7DFFD6), 0);

    lv_label_set_text(voice_connect_title_label, safe_title);
    lv_label_set_text(voice_connect_detail_label, safe_detail);
    lv_label_set_text(voice_connect_action_label, display_action);
    lv_obj_center(voice_connect_action_label);
    if (safe_action[0] != '\0') {
        lv_obj_clear_flag(voice_connect_panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(voice_connect_action_button, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(voice_connect_action_button, 18, 0);
        lv_obj_set_style_shadow_opa(voice_connect_action_button, LV_OPA_50, 0);
    } else {
        lv_obj_clear_flag(voice_connect_panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(voice_connect_action_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(voice_connect_action_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(voice_connect_action_button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(voice_connect_action_button, 0, 0);
    }
    lv_obj_move_foreground(voice_connect_panel);

    lvgl_port_unlock();
    return 0;
}

void hal_display_voice_connect_status_set_action_callback(hal_display_voice_connect_action_cb_t cb, void *user_ctx) {
    voice_connect_action_cb = cb;
    voice_connect_action_user_ctx = user_ctx;
    ESP_LOGI(TAG, "Voice connect action callback set has_cb=%d", cb != NULL);
}

void hal_display_voice_connect_status_clear(void) {
    if (voice_connect_panel == NULL) {
        return;
    }

    bool lvgl_locked = lvgl_port_lock(0);
    if (!lvgl_locked) {
        ESP_LOGW(TAG, "Failed to lock LVGL for voice connect status clear");
        return;
    }

    hal_display_voice_connect_status_clear_locked();
    hal_display_raise_text_overlay_locked();
    lvgl_port_unlock();
}

static void hal_display_capture_bsp_inputs(void) {
    lv_indev_t *indev = NULL;

    s_panel_handle = bsp_lcd_get_panel_handle();
    s_touch_handle = bsp_lcd_get_touch_handle();

    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_type_t type = lv_indev_get_type(indev);

        if (type == LV_INDEV_TYPE_ENCODER && s_knob_indev == NULL) {
            s_knob_indev = indev;
        } else if (type == LV_INDEV_TYPE_POINTER && s_touch_indev == NULL) {
            s_touch_indev = indev;
        }
    }
    inputs_initialized = (s_knob_indev != NULL) || (s_touch_indev != NULL);
}

/* ------------------------------------------------------------------ */
/* Minimal init for boot animation                                            */
/* ------------------------------------------------------------------ */

int hal_display_minimal_init(void) {
    if (minimal_initialized || is_initialized) {
        return 0; /* Already done */
    }

    ESP_LOGI(TAG, "Minimal display init for boot animation...");

    if (!hal_display_prepare_general_i2c_bus()) {
        ESP_LOGE(TAG, "General I2C preflight failed before IO expander init");
        return -1;
    }

    /* 1. Initialize IO expander */
    if (bsp_io_expander_init() == NULL) {
        ESP_LOGE(TAG, "Failed to initialize IO expander");
        return -1;
    }
    ESP_LOGI(TAG, "IO expander initialized, LCD power ON");

    /* 2. Use the board BSP path, matching the factory firmware: display,
     * encoder, and touch are registered with LVGL as one initialization step.
     */
    s_display = bsp_lvgl_init();
    if (s_display == NULL) {
        ESP_LOGE(TAG, "Failed to initialize BSP LVGL display and inputs");
        return -1;
    }
    hal_display_capture_bsp_inputs();
    ESP_LOGI(TAG, "LVGL initialized through BSP path (knob=%d touch=%d)", s_knob_indev != NULL ? 1 : 0,
             s_touch_indev != NULL ? 1 : 0);

    /* 3. Keep the BSP-configured default brightness. */
    esp_err_t ret = bsp_lcd_brightness_set(CONFIG_BSP_LCD_DEFAULT_BRIGHTNESS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set brightness: %d", ret);
    } else {
        ESP_LOGI(TAG, "Backlight set to %d%%", CONFIG_BSP_LCD_DEFAULT_BRIGHTNESS);
    }

    /* 4. Initialize LVGL PNG decoder (needed for boot animation error emoji) */
#if LV_USE_PNG
    lv_png_init();
    ESP_LOGI(TAG, "PNG decoder initialized");
#endif

    /* Note: SPIFFS and emoji will be loaded later by hal_display_init() */

    minimal_initialized = true;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Full display initialization (with SPIFFS and emoji)            */
/* ------------------------------------------------------------------ */

int hal_display_init(void) {
    if (is_initialized) {
        return 0; /* Already initialized */
    }

    ESP_LOGI(TAG, "Full display initialization...");

    /* If minimal init was not done, do it now */
    if (hal_display_minimal_init() != 0) {
        return -1;
    }
    if (s_display == NULL) {
        ESP_LOGE(TAG, "Display is not available");
        return -1;
    }

    /* 2. Get current active screen */
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);

    /* Set screen background to dark color */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Disable scrolling/dragging on screen */
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    /* 6. Create emoji image FIRST (centered) - so it's in background */
    img_emoji = lv_img_create(scr);
    lv_obj_align(img_emoji, LV_ALIGN_CENTER, 0, 0);

    /* 7. Create text overlay AFTER emoji - so it stays in foreground */
    hal_display_create_text_overlay_locked(scr, "Ready");

    is_initialized = true;
    ESP_LOGI(TAG, "Display initialized with LVGL animation surface and text overlay");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Display UI init - called after boot animation finishes                */
/* ------------------------------------------------------------------ */

static int hal_display_ui_init_internal(const char *initial_text, bool enable_animation) {
    if (is_initialized) {
        return 0; /* Already initialized */
    }

    ESP_LOGI(TAG, "Initializing display UI%s...", enable_animation ? "" : " (text only)");

    /* 1. Minimal init is already done by hal_display_minimal_init() at boot.
     * hal_display_minimal_init() is idempotent and returns early if already done. */
    if (hal_display_minimal_init() != 0) {
        ESP_LOGE(TAG, "Failed minimal display init");
        return -1;
    }

    /* 4. Create new main screen and load it (under LVGL lock) */
    lvgl_port_lock(0);
    hal_display_reset_voice_connect_status_handles_locked();

    /* Replace only the currently active screen. Other feature-owned cached
     * screens remain the responsibility of their owning subsystem. */
    lv_obj_t *old_active_scr = lv_disp_get_scr_act(NULL);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    if (enable_animation) {
        img_emoji = lv_img_create(scr);
        lv_obj_align(img_emoji, LV_ALIGN_CENTER, 0, 0);
    } else {
        img_emoji = NULL;
    }

    /* 5. Create text overlay AFTER emoji - so it's in foreground.
     * The first-frame text belongs to the app that opens this UI. Do not use a
     * hard-coded "Ready" here, otherwise every local app can briefly flash a
     * stale label before its behavior state is applied.
     */
    hal_display_create_text_overlay_locked(scr, initial_text != NULL ? initial_text : "");

    /* Load the new main screen FIRST - so user sees black screen immediately */
    lv_disp_load_scr(scr);
    s_behavior_screen = scr;

    /* Defer old screen deletion until LVGL is idle to avoid refresh-time use-after-free. */
    lv_obj_t *retained_screen = s_retained_previous_screen_once;
    s_retained_previous_screen_once = NULL;
    if (old_active_scr != NULL && old_active_scr != scr && old_active_scr != retained_screen) {
        lv_obj_del_async(old_active_scr);
    }

    lvgl_port_unlock();

    /* Give LVGL time to make the new surface active before its owner binds it. */
    vTaskDelay(pdMS_TO_TICKS(50));

    is_initialized = true;
    ESP_LOGI(TAG, "Display UI initialized with LVGL%s", enable_animation ? " animation surface" : " text only");
    return 0;
}

int hal_display_ui_init_with_text(const char *initial_text) {
    return hal_display_ui_init_internal(initial_text, true);
}

int hal_display_ui_init_text_only(const char *initial_text) {
    return hal_display_ui_init_internal(initial_text, false);
}

int hal_display_ui_upgrade_to_animation(void) {
    lvgl_port_lock(0);
    if (!is_initialized || s_behavior_screen == NULL || !lv_obj_is_valid(s_behavior_screen) ||
        lv_disp_get_scr_act(NULL) != s_behavior_screen) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Cannot upgrade animation surface outside the HAL-owned behavior screen");
        return -1;
    }
    if (img_emoji != NULL) {
        lvgl_port_unlock();
        return 0;
    }

    img_emoji = lv_img_create(s_behavior_screen);
    if (img_emoji == NULL) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Failed to create animation surface on active text UI");
        return -1;
    }
    lv_obj_align(img_emoji, LV_ALIGN_CENTER, 0, 0);
    if (text_overlay != NULL) {
        lv_obj_move_foreground(text_overlay);
    }
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Text-only behavior UI upgraded with LVGL animation surface");
    return 0;
}

int hal_display_ui_init(void) {
    return hal_display_ui_init_with_text("Ready");
}

void hal_display_retain_previous_screen_once(void *screen) {
    s_retained_previous_screen_once = (lv_obj_t *)screen;
}

static void hal_display_release_behavior_screen_locked(void) {
    lv_obj_t *old_active_scr = lv_disp_get_scr_act(NULL);
    lv_obj_t *blank_scr;

    if (s_behavior_screen == NULL || !lv_obj_is_valid(s_behavior_screen)) {
        return;
    }
    if (old_active_scr != s_behavior_screen) {
        lv_obj_del_async(s_behavior_screen);
        return;
    }

    blank_scr = lv_obj_create(NULL);

    if (blank_scr == NULL) {
        ESP_LOGW(TAG, "Failed to create blank behavior-exit screen");
        return;
    }

    lv_obj_set_size(blank_scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(blank_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(blank_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(blank_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(blank_scr, LV_SCROLLBAR_MODE_OFF);

    lv_disp_load_scr(blank_scr);
    lv_obj_del(old_active_scr);
}

void hal_display_ui_deinit(void) {
    if (!is_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing behavior display UI handles");

    lvgl_port_lock(0);
    hal_display_voice_connect_status_clear_locked();
    hal_display_release_behavior_screen_locked();
    img_emoji = NULL;
    label_text = NULL;
    text_overlay = NULL;
    s_behavior_screen = NULL;
    hal_display_reset_voice_connect_status_handles_locked();
    is_initialized = false;
    lvgl_port_unlock();
}

void *hal_display_get_animation_surface(void) {
    return (void *)img_emoji;
}

int hal_display_input_init(void) {
    if (inputs_initialized) {
        ESP_LOGI(TAG, "BSP display inputs already initialized (knob=%d touch=%d)", s_knob_indev != NULL ? 1 : 0,
                 s_touch_indev != NULL ? 1 : 0);
        return 0;
    }

    if (hal_display_minimal_init() != 0) {
        ESP_LOGW(TAG, "BSP display input check aborted because minimal display init is unavailable");
        return -1;
    }

    hal_display_capture_bsp_inputs();
    ESP_LOGI(TAG, "BSP display inputs ready: knob=%d touch=%d any=%d", s_knob_indev != NULL ? 1 : 0,
             s_touch_indev != NULL ? 1 : 0, inputs_initialized ? 1 : 0);
    return inputs_initialized ? 0 : -1;
}

bool hal_display_has_knob_input(void) {
    return s_knob_indev != NULL;
}

bool hal_display_has_touch_input(void) {
    return s_touch_indev != NULL;
}

int hal_display_set_text_with_style(const char *text, int font_size, bool alert_text) {
    if (!is_initialized || !label_text) {
        ESP_LOGW(TAG, "Display not initialized");
        return -1;
    }

    if (!text) {
        return -1;
    }

#define MAX_DISPLAY_CHARS 30
    char truncated[MAX_DISPLAY_CHARS + 4];
    int len = strlen(text);
    bool has_line_break = strchr(text, '\n') != NULL;
    bool should_truncate = !has_line_break && !hal_display_text_has_non_ascii(text) && len > MAX_DISPLAY_CHARS;
    if (should_truncate) {
        strncpy(truncated, text, MAX_DISPLAY_CHARS);
        strcpy(truncated + MAX_DISPLAY_CHARS, "...");
        ESP_LOGI(TAG, "Set text (truncated): '%s' -> '%s'", text, truncated);
        lvgl_port_lock(0);
        if (!hal_display_text_target_ready_locked()) {
            lvgl_port_unlock();
            return -1;
        }
        hal_display_apply_text_style_locked(text, font_size, alert_text);
        lv_label_set_text(label_text, truncated);
        hal_display_update_text_overlay_visibility_locked(truncated);
        hal_display_raise_text_overlay_locked();
        lvgl_port_unlock();
    } else {
        ESP_LOGI(TAG, "Set text: '%s' (size %d)", text, font_size);
        lvgl_port_lock(0);
        if (!hal_display_text_target_ready_locked()) {
            lvgl_port_unlock();
            return -1;
        }
        hal_display_apply_text_style_locked(text, font_size, alert_text);
        lv_label_set_text(label_text, text);
        hal_display_update_text_overlay_visibility_locked(text);
        hal_display_raise_text_overlay_locked();
        lvgl_port_unlock();
    }

    return 0;
}

int hal_display_set_text(const char *text, int font_size) {
    return hal_display_set_text_with_style(text, font_size, false);
}
