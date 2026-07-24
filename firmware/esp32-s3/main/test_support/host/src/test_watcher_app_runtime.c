#include "watcher_app_runtime.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int g_close_count;
static int g_open_count;
static int g_create_count;
static int g_destroy_count;
static int g_destroy_attempt_count;
static bool g_destroy_ready;
static esp_err_t g_resource_result = ESP_OK;
static watcher_app_resource_mode_t g_last_resource_mode = WATCHER_APP_RESOURCE_OFF;
static char g_last_resource_app[32];
static watcher_app_resource_mode_t g_resource_modes[8];
static watcher_app_resource_set_t g_resource_sets[8];
static char g_resource_apps[8][32];
static int g_resource_count;
static int g_resource_fail_on_call;

static void reset_counters(void) {
    g_close_count = 0;
    g_open_count = 0;
    g_create_count = 0;
    g_destroy_count = 0;
    g_destroy_attempt_count = 0;
    g_destroy_ready = false;
    g_resource_result = ESP_OK;
    g_last_resource_mode = WATCHER_APP_RESOURCE_OFF;
    g_last_resource_app[0] = '\0';
    g_resource_count = 0;
    g_resource_fail_on_call = -1;
    memset(g_resource_modes, 0, sizeof(g_resource_modes));
    memset(g_resource_sets, 0, sizeof(g_resource_sets));
    memset(g_resource_apps, 0, sizeof(g_resource_apps));
}

static void on_open(void) {
    ++g_open_count;
}

static void on_create(void) {
    ++g_create_count;
}

static esp_err_t on_destroy(void) {
    ++g_destroy_count;
    return ESP_OK;
}

static esp_err_t on_destroy_when_ready(void) {
    ++g_destroy_attempt_count;
    if (!g_destroy_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_destroy_count;
    return ESP_OK;
}

static void on_close(void) {
    ++g_close_count;
}

static esp_err_t apply_resource(watcher_app_resource_mode_t mode, watcher_app_resource_set_t resources,
                                const char *app_id) {
    g_last_resource_mode = mode;
    snprintf(g_last_resource_app, sizeof(g_last_resource_app), "%s", app_id != NULL ? app_id : "");
    if (g_resource_count < (int)(sizeof(g_resource_modes) / sizeof(g_resource_modes[0]))) {
        g_resource_modes[g_resource_count] = mode;
        g_resource_sets[g_resource_count] = resources;
        snprintf(g_resource_apps[g_resource_count], sizeof(g_resource_apps[g_resource_count]), "%s",
                 app_id != NULL ? app_id : "");
        ++g_resource_count;
    }
    return g_resource_fail_on_call == g_resource_count ? ESP_FAIL : g_resource_result;
}

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int test_register_requires_explicit_input_context(void) {
    int failures = 0;
    static const watcher_app_t unspecified_app = {
        .id = "runtime.test.input.unspecified",
        .name = "Unspecified Input",
    };
    static const watcher_app_t invalid_app = {
        .id = "runtime.test.input.invalid",
        .name = "Invalid Input",
        .input_context = (watcher_input_context_t)(WATCHER_INPUT_CONTEXT_APP_EVENT + 1),
    };
    static const watcher_app_t event_app = {
        .id = "runtime.test.input.event",
        .name = "Event Input",
        .input_context = WATCHER_INPUT_CONTEXT_APP_EVENT,
    };

    failures += expect_true(watcher_app_register(&unspecified_app) == ESP_ERR_INVALID_ARG,
                            "registration rejects an unspecified input context");
    failures += expect_true(watcher_app_register(&invalid_app) == ESP_ERR_INVALID_ARG,
                            "registration rejects an invalid input context");
    failures +=
        expect_true(watcher_app_register(&event_app) == ESP_OK, "registration accepts the app event input context");
    return failures;
}

static int test_close_reports_resource_release_failure(void) {
    int failures = 0;
    static const watcher_app_t app = {
        .id = "runtime.test.close.fail",
        .name = "Close Failure",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&app) == ESP_OK, "register app");
    failures += expect_true(watcher_app_open(app.id) == ESP_OK, "open app before close failure");
    failures += expect_true(g_open_count == 1, "on_open called once");

    g_resource_fail_on_call = 2;
    failures += expect_true(watcher_app_close_current() == ESP_FAIL, "close returns resource release failure");
    failures += expect_true(g_close_count == 1, "on_close called once");
    failures += expect_true(g_resource_count == 3, "failed close reapplies previous resource set");
    failures += expect_true(g_resource_sets[1] == WATCHER_APP_RESOURCE_SET_WIFI_STA,
                            "close first requests always-on wifi baseline");
    failures += expect_true(g_resource_sets[2] == (WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD),
                            "failed close rolls back cloud resource");
    failures += expect_true(strcmp(g_last_resource_app, app.id) == 0, "close reports app id to resource callback");
    failures += expect_true(watcher_app_get_active() == &app, "failed close restores active app");
    failures += expect_true(g_open_count == 2, "failed close reopens previous app");

    g_resource_fail_on_call = -1;
    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_switch_failure_rolls_back_previous_app(void) {
    int failures = 0;
    static const watcher_app_t first_app = {
        .id = "runtime.test.rollback.first",
        .name = "Rollback First",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };
    static const watcher_app_t second_app = {
        .id = "runtime.test.rollback.second",
        .name = "Rollback Second",
        .resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_BLE,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&first_app) == ESP_OK, "register rollback first app");
    failures += expect_true(watcher_app_register(&second_app) == ESP_OK, "register rollback second app");
    failures += expect_true(watcher_app_open(first_app.id) == ESP_OK, "open rollback first app");
    g_resource_fail_on_call = 2;
    failures += expect_true(watcher_app_open(second_app.id) == ESP_FAIL, "target resource failure is reported");
    failures += expect_true(g_resource_count == 3, "failed target is followed by rollback apply");
    failures += expect_true(g_resource_sets[1] == (WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_BLE),
                            "failed target resource set was attempted");
    failures += expect_true(g_resource_sets[2] == (WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD),
                            "previous resource set was restored");
    failures += expect_true(watcher_app_get_active() == &first_app, "previous app active after rollback");
    failures += expect_true(g_close_count == 1, "previous app closed once before attempted switch");
    failures += expect_true(g_open_count == 2, "previous app reopened after rollback");

    g_resource_fail_on_call = -1;
    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_switch_reconciles_directly_to_next_resources(void) {
    int failures = 0;
    static const watcher_app_t first_app = {
        .id = "runtime.test.switch.first",
        .name = "Switch First",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };
    static const watcher_app_t second_app = {
        .id = "runtime.test.switch.second",
        .name = "Switch Second",
        .resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&first_app) == ESP_OK, "register first app");
    failures += expect_true(watcher_app_register(&second_app) == ESP_OK, "register second app");
    failures += expect_true(watcher_app_open(first_app.id) == ESP_OK, "open first app");
    failures += expect_true(watcher_app_open(second_app.id) == ESP_OK, "switch to second app");

    failures += expect_true(g_close_count == 1, "switch closes previous app once");
    failures += expect_true(g_open_count == 2, "both apps opened");
    failures += expect_true(g_resource_count == 2, "resource callback called only for first and second targets");
    failures += expect_true(g_resource_modes[0] == WATCHER_APP_RESOURCE_WIFI_ONLY, "first app resource requested");
    failures += expect_true(strcmp(g_resource_apps[0], first_app.id) == 0, "first app resource tagged");
    failures += expect_true(g_resource_modes[1] == WATCHER_APP_RESOURCE_BLE_ONLY, "second app resource requested");
    failures += expect_true(strcmp(g_resource_apps[1], second_app.id) == 0, "second app resource tagged");
    failures += expect_true(watcher_app_get_active() == &second_app, "second app active after switch");

    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_switch_to_phone_control_requests_ble_resource(void) {
    int failures = 0;
    static const watcher_app_t wifi_app = {
        .id = "runtime.test.phone.previous",
        .name = "Previous WiFi",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };
    static const watcher_app_t phone_control_app = {
        .id = "phone.control.app",
        .name = "Phone Control",
        .resource_mode = WATCHER_APP_RESOURCE_BLE_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_MCU_RUNTIME,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&wifi_app) == ESP_OK, "register previous wifi app");
    failures += expect_true(watcher_app_register(&phone_control_app) == ESP_OK, "register phone control app");
    failures += expect_true(watcher_app_open(wifi_app.id) == ESP_OK, "open previous wifi app");
    failures += expect_true(watcher_app_open(phone_control_app.id) == ESP_OK, "switch to phone control app");

    failures += expect_true(g_resource_count == 2, "phone control switch reconciles directly");
    failures += expect_true(g_resource_modes[0] == WATCHER_APP_RESOURCE_WIFI_ONLY, "previous wifi resource requested");
    failures += expect_true(g_resource_modes[1] == WATCHER_APP_RESOURCE_BLE_ONLY, "phone control requests BLE mode");
    failures += expect_true(g_resource_sets[1] == (WATCHER_APP_RESOURCE_SET_BLE | WATCHER_APP_RESOURCE_SET_MCU_RUNTIME),
                            "phone control requests only BLE and MCU resources");
    failures += expect_true(strcmp(g_resource_apps[1], phone_control_app.id) == 0, "phone control resource tagged");
    failures += expect_true(watcher_app_get_active() == &phone_control_app, "phone control active after switch");

    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_switch_keeps_shared_resource_mode_during_handoff(void) {
    int failures = 0;
    static const watcher_app_t first_app = {
        .id = "runtime.test.shared.first",
        .name = "Shared First",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };
    static const watcher_app_t second_app = {
        .id = "runtime.test.shared.second",
        .name = "Shared Second",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&first_app) == ESP_OK, "register shared first app");
    failures += expect_true(watcher_app_register(&second_app) == ESP_OK, "register shared second app");
    failures += expect_true(watcher_app_open(first_app.id) == ESP_OK, "open shared first app");
    failures += expect_true(watcher_app_open(second_app.id) == ESP_OK, "switch to shared second app");

    failures += expect_true(g_close_count == 1, "shared switch closes previous app once");
    failures += expect_true(g_open_count == 2, "shared switch opens both apps");
    failures += expect_true(g_resource_count == 2, "shared switch does not release resource to off");
    failures += expect_true(g_resource_modes[0] == WATCHER_APP_RESOURCE_WIFI_ONLY, "shared first resource requested");
    failures += expect_true(strcmp(g_resource_apps[0], first_app.id) == 0, "shared first resource tagged");
    failures += expect_true(g_resource_modes[1] == WATCHER_APP_RESOURCE_WIFI_ONLY, "shared second resource requested");
    failures += expect_true(strcmp(g_resource_apps[1], second_app.id) == 0, "shared second resource retagged");
    failures += expect_true(watcher_app_get_active() == &second_app, "shared second app active after switch");

    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_explicit_resource_sets_reconcile_without_global_off(void) {
    int failures = 0;
    static const watcher_app_t cloud_app = {
        .id = "runtime.test.resources.cloud",
        .name = "Cloud",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };
    static const watcher_app_t launcher_app = {
        .id = "runtime.test.resources.launcher",
        .name = "Launcher",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&cloud_app) == ESP_OK, "register explicit cloud app");
    failures += expect_true(watcher_app_register(&launcher_app) == ESP_OK, "register explicit launcher app");
    failures += expect_true(watcher_app_open(cloud_app.id) == ESP_OK, "open explicit cloud app");
    failures += expect_true(watcher_app_open(launcher_app.id) == ESP_OK, "switch to explicit launcher app");
    failures += expect_true(g_resource_count == 2, "different resource sets reconcile directly");
    failures += expect_true(g_resource_sets[0] == (WATCHER_APP_RESOURCE_SET_WIFI_STA | WATCHER_APP_RESOURCE_SET_CLOUD),
                            "cloud resource set applied");
    failures +=
        expect_true(g_resource_sets[1] == WATCHER_APP_RESOURCE_SET_WIFI_STA, "launcher receives only wifi resource");

    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_deferred_destroy_waits_until_background_work_is_idle(void) {
    int failures = 0;
    static const watcher_app_t deferred_app = {
        .id = "runtime.test.lifecycle.deferred",
        .name = "Deferred Destroy",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
        .lifecycle = WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_create = on_create,
        .on_open = on_open,
        .on_close = on_close,
        .on_destroy = on_destroy_when_ready,
    };
    static const watcher_app_t next_app = {
        .id = "runtime.test.lifecycle.deferred.next",
        .name = "Deferred Next",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&deferred_app) == ESP_OK, "register deferred app");
    failures += expect_true(watcher_app_register(&next_app) == ESP_OK, "register deferred next app");
    failures += expect_true(watcher_app_open(deferred_app.id) == ESP_OK, "open deferred app");
    failures += expect_true(watcher_app_open(next_app.id) == ESP_OK, "switch while destroy is deferred");
    failures += expect_true(g_destroy_attempt_count == 1, "destroy attempted once on close");
    failures += expect_true(g_destroy_count == 0, "busy app is not destroyed");
    failures += expect_true(watcher_app_open(deferred_app.id) == ESP_ERR_INVALID_STATE,
                            "busy deferred app cannot reopen over old background work");

    g_destroy_ready = true;
    watcher_app_tick();
    failures += expect_true(g_destroy_count == 1, "deferred destroy completes from runtime tick");
    failures += expect_true(watcher_app_open(deferred_app.id) == ESP_OK, "destroyed app can reopen safely");
    failures += expect_true(g_create_count == 2, "deferred app is recreated after destroy");

    watcher_app_close_current();
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

static int test_destroy_on_close_recreates_ephemeral_app(void) {
    int failures = 0;
    static const watcher_app_t ephemeral_app = {
        .id = "runtime.test.lifecycle.ephemeral",
        .name = "Ephemeral",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
        .lifecycle = WATCHER_APP_LIFECYCLE_DESTROY_ON_CLOSE,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_create = on_create,
        .on_open = on_open,
        .on_close = on_close,
        .on_destroy = on_destroy,
    };
    static const watcher_app_t next_app = {
        .id = "runtime.test.lifecycle.next",
        .name = "Next",
        .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
        .resources = WATCHER_APP_RESOURCE_SET_WIFI_STA,
        .input_context = WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
        .on_open = on_open,
        .on_close = on_close,
    };

    reset_counters();
    watcher_app_set_resource_apply_cb(apply_resource);
    failures += expect_true(watcher_app_register(&ephemeral_app) == ESP_OK, "register ephemeral app");
    failures += expect_true(watcher_app_register(&next_app) == ESP_OK, "register lifecycle next app");
    failures += expect_true(watcher_app_open(ephemeral_app.id) == ESP_OK, "open ephemeral app");
    failures += expect_true(watcher_app_open(next_app.id) == ESP_OK, "close ephemeral via switch");
    failures += expect_true(g_create_count == 1, "ephemeral app created once");
    failures += expect_true(g_destroy_count == 1, "ephemeral app destroyed on close");
    failures += expect_true(watcher_app_open(ephemeral_app.id) == ESP_OK, "reopen ephemeral app");
    failures += expect_true(g_create_count == 2, "destroyed app recreated on next open");
    failures += expect_true(watcher_app_destroy(ephemeral_app.id) == ESP_ERR_INVALID_STATE,
                            "active app cannot be destroyed directly");

    watcher_app_close_current();
    failures += expect_true(g_destroy_count == 2, "explicit close destroys ephemeral app again");
    watcher_app_set_resource_apply_cb(NULL);
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_register_requires_explicit_input_context();
    failures += test_close_reports_resource_release_failure();
    failures += test_switch_failure_rolls_back_previous_app();
    failures += test_switch_reconciles_directly_to_next_resources();
    failures += test_switch_to_phone_control_requests_ble_resource();
    failures += test_switch_keeps_shared_resource_mode_during_handoff();
    failures += test_explicit_resource_sets_reconcile_without_global_off();
    failures += test_deferred_destroy_waits_until_background_work_is_idle();
    failures += test_destroy_on_close_recreates_ephemeral_app();

    if (failures != 0) {
        fprintf(stderr, "%d watcher_app_runtime host test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
