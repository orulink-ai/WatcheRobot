#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdbool.h>

#include "esp_lcd_types.h"

typedef void (*hal_display_voice_connect_action_cb_t)(void *user_ctx);

/**
 * Set text on display
 * @param text Text to display
 * @param font_size Font size
 * @return 0 on success, -1 on error
 */
int hal_display_set_text(const char *text, int font_size);

/**
 * Set text on display with an explicit text style.
 * @param text Text to display
 * @param font_size Font size
 * @param alert_text True for alert styling, false for normal styling
 * @return 0 on success, -1 on error
 */
int hal_display_set_text_with_style(const char *text, int font_size, bool alert_text);

/**
 * @brief Minimal display init for boot animation
 *
 * Initializes IO expander, LVGL display, backlight, and BSP input devices.
 * Does NOT load SPIFFS or emoji images.
 *
 * @return 0 on success, -1 on error
 */
int hal_display_minimal_init(void);

/**
 * @brief Full display initialization
 *
 * Initializes everything including SPIFFS and emoji images.
 *
 * @return 0 on success, -1 on error
 */
int hal_display_init(void);

/**
 * @brief Display UI init - called after boot animation finishes
 *
 * Creates main UI with emoji animation and text label.
 * Call hal_display_minimal_init() internally if needed.
 *
 * @return 0 on success, -1 on error
 */
int hal_display_ui_init(void);

/**
 * @brief Display UI init with an explicit first-frame text.
 *
 * This avoids showing a stale default label while an app-specific behavior state
 * is still being applied.
 *
 * @param initial_text Text to show when the overlay is first created. Pass NULL
 *                     to use an empty overlay.
 * @return 0 on success, -1 on error
 */
int hal_display_ui_init_with_text(const char *initial_text);

/**
 * @brief Display UI init with only the text overlay.
 *
 * This is intended for memory-heavy transport apps that need a local status
 * page but should not start the emoji animation player.
 */
int hal_display_ui_init_text_only(const char *initial_text);

/**
 * @brief Upgrade the active text-only behavior UI with an animation surface.
 *
 * The active screen and its text overlay remain in place. This operation only
 * adds the HAL-owned LVGL image object and is idempotent when the surface
 * already exists. Animation Service binding remains the caller's
 * responsibility and must happen after this function returns.
 *
 * @return 0 on success, -1 when no behavior UI is active or allocation fails
 */
int hal_display_ui_upgrade_to_animation(void);

/**
 * @brief Keep a specific active screen alive across the next behavior UI init.
 *
 * Pass the current LVGL screen pointer as an opaque value when the caller owns a
 * reusable screen cache, such as the launcher. The retain request is consumed by
 * the next hal_display_ui_init* call; all other previous screens are still
 * released normally.
 */
void hal_display_retain_previous_screen_once(void *screen);

/**
 * @brief Show or update the Voice app connection status page.
 *
 * The Voice entry flow uses this on top of the text-only behavior UI so the
 * user sees a lightweight WebSocket/Wi-Fi status page before emoji animation
 * resources are started.
 */
int hal_display_voice_connect_status_set(const char *title, const char *detail, const char *action, bool show_spinner,
                                         bool alert);

/**
 * @brief Register a callback for the Voice connection status action label.
 *
 * The callback is invoked from the LVGL event context. Keep it non-blocking and
 * hand work back to the main application task.
 */
void hal_display_voice_connect_status_set_action_callback(hal_display_voice_connect_action_cb_t cb, void *user_ctx);

/**
 * @brief Clear the Voice app connection status page if it is visible.
 */
void hal_display_voice_connect_status_clear(void);

/**
 * @brief Mark the behavior display UI as inactive after its screen is replaced.
 *
 * This stops emoji animation and clears LVGL object handles owned by the
 * behavior UI. It does not deinitialize the minimal display, touch, knob, or
 * LCD panel.
 */
void hal_display_ui_deinit(void);

/**
 * @brief Return the LVGL image object owned by the current behavior UI.
 *
 * The returned pointer is an opaque animation surface. HAL owns its lifetime;
 * callers must unbind it from Animation Service before hal_display_ui_deinit().
 * Text-only UIs return NULL.
 */
void *hal_display_get_animation_surface(void);

/**
 * @brief Ensure touch/knob inputs are available after the display is up
 *
 * The factory-aligned display path registers inputs during minimal init; this
 * function remains as an idempotent readiness check for app startup paths.
 *
 * @return 0 on success, -1 on error
 */
int hal_display_input_init(void);

/**
 * @brief Return whether knob/encoder input is attached successfully
 */
bool hal_display_has_knob_input(void);

/**
 * @brief Return whether touch input is attached successfully
 */
bool hal_display_has_touch_input(void);

/**
 * @brief Return the initialized LCD panel handle for low-level display paths.
 */
esp_lcd_panel_handle_t hal_display_get_panel_handle(void);

/**
 * @brief Request LVGL to redraw the text overlay.
 *
 * This is intended for low-level display paths that draw behind LVGL and then
 * need the LVGL-managed overlay restored.
 */
void hal_display_invalidate_text_overlay(void);

/**
 * @brief Return the visible text overlay bounds in screen coordinates.
 *
 * The returned x2/y2 values are exclusive.
 */
bool hal_display_get_text_overlay_bounds(int *x1, int *y1, int *x2, int *y2);

#endif /* HAL_DISPLAY_H */
