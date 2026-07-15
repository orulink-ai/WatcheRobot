#ifndef WATCHER_LAUNCHER_HOME_MODEL_H
#define WATCHER_LAUNCHER_HOME_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t translate_x;
    uint8_t opacity;
} launcher_home_scroll_projection_t;

typedef enum {
    LAUNCHER_HOME_BATTERY_UNKNOWN = 0,
    LAUNCHER_HOME_BATTERY_NORMAL,
    LAUNCHER_HOME_BATTERY_CHARGING,
    LAUNCHER_HOME_BATTERY_LOW,
} launcher_home_battery_state_t;

#define LAUNCHER_HOME_TIME_TEXT_LEN 6
#define LAUNCHER_HOME_BATTERY_TEXT_LEN 4
#define LAUNCHER_HOME_LOW_BATTERY_PERCENT 15

int launcher_home_wrap_index(int index, int count);
int launcher_home_next_index(int selected, int direction, int count);
bool launcher_home_scroll_project(int32_t diff_y, int32_t radius, launcher_home_scroll_projection_t *projection);
bool launcher_home_format_time_text(int hour, int minute, char *out, size_t out_size);
bool launcher_home_format_battery_text(int percent, char *out, size_t out_size);
launcher_home_battery_state_t launcher_home_resolve_battery_state(bool sample_valid,
                                                                  bool vbus_present,
                                                                  bool charging,
                                                                  bool battery_present,
                                                                  bool percent_valid,
                                                                  int percent);

#ifdef __cplusplus
}
#endif

#endif
