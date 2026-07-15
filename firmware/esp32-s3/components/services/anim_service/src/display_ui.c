#include "display_ui.h"
#include "esp_log.h"
#include "hal_display.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Private: Current state                                             */
/* ------------------------------------------------------------------ */

#define MAX_TEXT_LEN 128
#define TAG "DISPLAY_UI"

static char g_current_text[MAX_TEXT_LEN] = {0};
static const int DEFAULT_FONT_SIZE = 24;
static int g_current_font_size = 0;
static display_text_style_t g_current_text_style = DISPLAY_TEXT_STYLE_NORMAL;
static bool g_text_suppressed = false;

static int text_equals_current(const char *text) {
    if (text == NULL) {
        return 0;
    }

    return strncmp(g_current_text, text, MAX_TEXT_LEN) == 0;
}

static int normalize_font_size(int font_size) {
    return (font_size > 0) ? font_size : DEFAULT_FONT_SIZE;
}

void display_ui_set_text_suppressed(bool suppressed) {
    g_text_suppressed = suppressed;
    ESP_LOGI(TAG, "Display text updates %s", suppressed ? "suppressed" : "enabled");
}

/* ------------------------------------------------------------------ */
/* Public: Update display                                             */
/* ------------------------------------------------------------------ */

int display_update_with_style(const char *text, int font_size, display_text_style_t text_style,
                              display_result_t *out_result) {
    int text_changed = 0;
    int normalized_font_size = normalize_font_size(font_size);

    if (out_result) {
        memset(out_result, 0, sizeof(*out_result));
    }

    text_changed = (!g_text_suppressed && text != NULL &&
                    (!text_equals_current(text) || g_current_font_size != normalized_font_size ||
                     g_current_text_style != text_style));

    /* Update text if provided */
    if (text_changed) {
        if (hal_display_set_text_with_style(text, normalized_font_size, text_style == DISPLAY_TEXT_STYLE_ALERT) != 0) {
            return -1;
        }
        /* Store current text */
        strncpy(g_current_text, text, MAX_TEXT_LEN - 1);
        g_current_text[MAX_TEXT_LEN - 1] = '\0';
        g_current_font_size = normalized_font_size;
        g_current_text_style = text_style;

        if (out_result) {
            out_result->text_updated = 1;
        }
    }

    if (text != NULL && !text_changed) {
        ESP_LOGI(TAG, "Display text update no-op");
    } else if (text_changed) {
        ESP_LOGI(TAG, "Display text update applied");
    }

    return 0;
}

int display_update(const char *text, int font_size, display_result_t *out_result) {
    return display_update_with_style(text, font_size, DISPLAY_TEXT_STYLE_NORMAL, out_result);
}

/* ------------------------------------------------------------------ */
/* Public: Get current text                                           */
/* ------------------------------------------------------------------ */

int display_get_text(char *out_buf, int buf_size) {
    if (!out_buf || buf_size <= 0) {
        return -1;
    }

    if (g_current_text[0] == '\0') {
        return -1; /* No text set */
    }

    strncpy(out_buf, g_current_text, buf_size - 1);
    out_buf[buf_size - 1] = '\0';

    return 0;
}
