#include "client_app.h"

#include "behavior_state_service.h"
#include "esp_log.h"

#include <string.h>

#define TAG "CLIENT_APP"

static client_app_deps_t s_deps = {0};
static bool s_session_ready = false;
static bool s_main_ui_shown = false;

static void client_app_copy_deps(const client_app_deps_t *deps) {
    if (deps == NULL) {
        memset(&s_deps, 0, sizeof(s_deps));
        return;
    }
    s_deps = *deps;
}

void client_app_configure(const client_app_deps_t *deps) {
    client_app_copy_deps(deps);
}

static void client_app_open_ui_waiting(void) {
    if (s_deps.clear_wifi_gate != NULL) {
        s_deps.clear_wifi_gate();
    }
    if (s_deps.open_behavior_ui != NULL) {
        s_deps.open_behavior_ui("processing", "Waiting Desktop Link", "processing", "");
    }
    s_main_ui_shown = true;
}

static void client_app_open_wifi_gate_base(void) {
    if (s_deps.open_wifi_gate_base_ui != NULL) {
        s_deps.open_wifi_gate_base_ui();
        return;
    }

    if (s_deps.open_behavior_ui != NULL) {
        s_deps.open_behavior_ui("standby", "", "", "");
    }
}

static bool client_app_show_wifi_gate_if_needed(void) {
    if (s_deps.show_wifi_gate == NULL || !s_deps.show_wifi_gate("Desktop Link")) {
        return false;
    }

    s_session_ready = false;
    s_main_ui_shown = false;
    return true;
}

static bool client_app_handle_wifi_gate_action(const char *reason) {
    return s_deps.handle_wifi_gate_action != NULL && s_deps.handle_wifi_gate_action(reason);
}

static void client_app_on_open(void) {
    s_session_ready = false;
    s_main_ui_shown = false;

    if (s_deps.set_wifi_gate_action_enabled != NULL) {
        s_deps.set_wifi_gate_action_enabled(true);
    }
    if (s_deps.start_transport != NULL) {
        s_deps.start_transport();
    }

    client_app_open_wifi_gate_base();
    if (!client_app_show_wifi_gate_if_needed()) {
        client_app_open_ui_waiting();
    }
    ESP_LOGI(TAG, "Desktop Link app opened");
}

static void client_app_on_tick(void) {
    bool session_ready;

    if (s_deps.tick_local_app != NULL) {
        s_deps.tick_local_app();
    }

    if (client_app_show_wifi_gate_if_needed()) {
        return;
    }
    if (!s_main_ui_shown) {
        client_app_open_ui_waiting();
    }

    session_ready = s_deps.is_transport_ready != NULL && s_deps.is_transport_ready();
    if (session_ready == s_session_ready) {
        return;
    }

    if (s_session_ready && !session_ready && s_deps.restart_transport != NULL) {
        ESP_LOGW(TAG, "Desktop Link transport lost ready state; restarting");
        s_deps.restart_transport("client app lost session");
    }

    s_session_ready = session_ready;
    if (session_ready) {
        (void)behavior_state_set_with_resources("happy", "Desktop Link connected", 0, "happy", "");
        s_main_ui_shown = true;
    } else {
        client_app_open_ui_waiting();
    }
}

static void client_app_on_close(void) {
    if (s_deps.set_wifi_gate_action_enabled != NULL) {
        s_deps.set_wifi_gate_action_enabled(false);
    }
    if (s_deps.clear_wifi_gate != NULL) {
        s_deps.clear_wifi_gate();
    }
    if (s_deps.stop_transport != NULL) {
        s_deps.stop_transport();
    }
    if (s_deps.cleanup_local_behavior_app != NULL) {
        s_deps.cleanup_local_behavior_app();
    }

    s_session_ready = false;
    s_main_ui_shown = false;
    ESP_LOGI(TAG, "Desktop Link app closed");
}

static void client_app_on_button(void) {
    if (client_app_handle_wifi_gate_action("button")) {
        return;
    }

    if (s_deps.on_local_button != NULL) {
        s_deps.on_local_button();
    }
}

void client_app_process_connect_action_click(void) {
    (void)client_app_handle_wifi_gate_action("touch");
}

static const watcher_app_t s_client_app = {
    .id = "client.app",
    .name = "Desktop Link",
    .icon = "client",
    .theme_color = 0x184D7A,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .input_context = WATCHER_INPUT_CONTEXT_APP_ACTION,
    .on_open = client_app_on_open,
    .on_tick = client_app_on_tick,
    .on_close = client_app_on_close,
    .on_button = client_app_on_button,
};

const watcher_app_t *client_app_get_app(void) {
    return &s_client_app;
}
