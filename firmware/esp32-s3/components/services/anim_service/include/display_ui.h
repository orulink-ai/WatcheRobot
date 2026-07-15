#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdbool.h>

typedef enum {
    DISPLAY_TEXT_STYLE_NORMAL = 0,
    DISPLAY_TEXT_STYLE_ALERT,
} display_text_style_t;

/* Display update result */
typedef struct {
    int text_updated; /* Non-zero if text was updated */
} display_result_t;

/**
 * Suppress display text updates.
 * Useful for app modes that reserve the display surface for non-text content.
 */
void display_ui_set_text_suppressed(bool suppressed);

/**
 * Update display text.
 * @param text Text to display (can be NULL)
 * @param font_size Font size (0 for default)
 * @param out_result Output result (can be NULL)
 * @return 0 on success, -1 on error
 */
int display_update(const char *text, int font_size, display_result_t *out_result);

/**
 * Update display text using an explicit style.
 * @param text Text to display (can be NULL)
 * @param font_size Font size (0 for default)
 * @param text_style Text style to apply when text is updated
 * @param out_result Output result (can be NULL)
 * @return 0 on success, -1 on error
 */
int display_update_with_style(const char *text, int font_size, display_text_style_t text_style,
                              display_result_t *out_result);

/**
 * Get current text
 * @param out_buf Output buffer
 * @param buf_size Buffer size
 * @return 0 on success, -1 if no text or error
 */
int display_get_text(char *out_buf, int buf_size);

#endif /* DISPLAY_UI_H */
