#ifndef CLIENT_APP_H
#define CLIENT_APP_H

#include "watcher_app_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*start_transport)(void);
    void (*stop_transport)(void);
    bool (*is_transport_ready)(void);
    bool (*is_transport_stale)(uint32_t stale_ms);
    void (*restart_transport)(const char *reason);
    void (*open_behavior_ui)(const char *state_id, const char *text, const char *anim_id, const char *sound_id);
    void (*open_wifi_gate_base_ui)(void);
    bool (*show_wifi_gate)(const char *app_label);
    bool (*handle_wifi_gate_action)(const char *reason);
    void (*clear_wifi_gate)(void);
    void (*set_wifi_gate_action_enabled)(bool enabled);
    void (*tick_local_app)(void);
    void (*cleanup_local_behavior_app)(void);
    void (*on_local_button)(void);
} client_app_deps_t;

void client_app_configure(const client_app_deps_t *deps);
void client_app_process_connect_action_click(void);
const watcher_app_t *client_app_get_app(void);

#ifdef __cplusplus
}
#endif

#endif /* CLIENT_APP_H */
