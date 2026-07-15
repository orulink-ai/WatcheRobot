#include "watcher_input_router.h"

#include <assert.h>
#include <stdio.h>

static watcher_input_scope_t scope(watcher_input_context_t context, uintptr_t token) {
    watcher_input_scope_t value = {
        .context = context,
        .owner_token = token,
    };
    return value;
}

static watcher_input_router_t new_router(watcher_input_scope_t initial_scope) {
    watcher_input_router_t router;
    watcher_input_router_init(&router, NULL, initial_scope);
    return router;
}

static void test_lvgl_short_click_has_one_lvgl_owner(void) {
    watcher_input_scope_t current = scope(WATCHER_INPUT_CONTEXT_LVGL_NAV, 1);
    watcher_input_router_t router = new_router(current);
    bool cancelled = false;

    watcher_input_result_t down = watcher_input_router_on_button_down(&router, current, 1000);
    assert(down.owner == WATCHER_INPUT_OWNER_LVGL);
    assert(watcher_input_router_lvgl_button_pressed(&router, current, &cancelled));
    assert(!cancelled);

    watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, 1120);
    assert(up.event == WATCHER_INPUT_EVENT_SHORT_CLICK);
    assert(up.owner == WATCHER_INPUT_OWNER_LVGL);
    assert(!watcher_input_router_lvgl_button_pressed(&router, current, &cancelled));
    assert(!watcher_input_router_consume_app_click(&router, current));
}

static void test_app_short_click_has_one_app_owner(void) {
    watcher_input_scope_t current = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 2);
    watcher_input_router_t router = new_router(current);
    bool cancelled = false;

    watcher_input_result_t down = watcher_input_router_on_button_down(&router, current, 1000);
    assert(down.owner == WATCHER_INPUT_OWNER_APP);
    assert(!watcher_input_router_lvgl_button_pressed(&router, current, &cancelled));

    watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, 1130);
    assert(up.event == WATCHER_INPUT_EVENT_SHORT_CLICK);
    assert(up.owner == WATCHER_INPUT_OWNER_APP);
    assert(watcher_input_router_consume_app_click(&router, current));
    assert(!watcher_input_router_consume_app_click(&router, current));
}

static void test_system_only_rejects_short_click(void) {
    watcher_input_scope_t current = scope(WATCHER_INPUT_CONTEXT_SYSTEM_ONLY, 3);
    watcher_input_router_t router = new_router(current);

    assert(watcher_input_router_on_button_down(&router, current, 1000).owner == WATCHER_INPUT_OWNER_NONE);
    watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, 1120);
    assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
    assert(up.reject == WATCHER_INPUT_REJECT_SYSTEM_ONLY);
    assert(!watcher_input_router_consume_app_click(&router, current));
}

static void test_unspecified_context_fails_closed(void) {
    watcher_input_scope_t current = scope(WATCHER_INPUT_CONTEXT_UNSPECIFIED, 4);
    watcher_input_router_t router = new_router(current);

    assert(watcher_input_router_on_rotate(&router, current, 1).owner == WATCHER_INPUT_OWNER_NONE);
    assert(watcher_input_router_on_button_down(&router, current, 1000).owner == WATCHER_INPUT_OWNER_NONE);
    watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, 1100);
    assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
    assert(up.reject == WATCHER_INPUT_REJECT_SYSTEM_ONLY);
}

static void test_long_hold_is_system_owned_in_every_context(void) {
    const watcher_input_context_t contexts[] = {
        WATCHER_INPUT_CONTEXT_LVGL_NAV,
        WATCHER_INPUT_CONTEXT_APP_ACTION,
        WATCHER_INPUT_CONTEXT_SYSTEM_ONLY,
    };

    for (size_t i = 0; i < sizeof(contexts) / sizeof(contexts[0]); ++i) {
        watcher_input_scope_t current = scope(contexts[i], i + 10);
        watcher_input_router_t router = new_router(current);
        bool cancelled = false;

        watcher_input_router_on_button_down(&router, current, 1000);
        watcher_input_result_t hold = watcher_input_router_on_long_hold(&router, current, 4000);
        assert(hold.event == WATCHER_INPUT_EVENT_LONG_HOLD);
        assert(hold.owner == WATCHER_INPUT_OWNER_SYSTEM);
        watcher_input_result_t duplicate_hold = watcher_input_router_on_long_hold(&router, current, 4050);
        assert(duplicate_hold.event == WATCHER_INPUT_EVENT_REJECTED);
        assert(duplicate_hold.reject == WATCHER_INPUT_REJECT_LONG_ALREADY_FIRED);
        if (contexts[i] == WATCHER_INPUT_CONTEXT_LVGL_NAV) {
            assert(!watcher_input_router_lvgl_button_pressed(&router, current, &cancelled));
            assert(cancelled);
        }
        watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, 4100);
        assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
        assert(up.reject == WATCHER_INPUT_REJECT_LONG_ALREADY_FIRED);
        assert(!watcher_input_router_consume_app_click(&router, current));
    }
}

static void test_context_change_between_down_and_up_is_rejected(void) {
    watcher_input_scope_t old_scope = scope(WATCHER_INPUT_CONTEXT_LVGL_NAV, 20);
    watcher_input_scope_t new_scope = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 21);
    watcher_input_router_t router = new_router(old_scope);
    bool cancelled = false;

    watcher_input_router_on_button_down(&router, old_scope, 1000);
    watcher_input_router_set_scope(&router, new_scope);
    assert(!watcher_input_router_lvgl_button_pressed(&router, new_scope, &cancelled));
    assert(cancelled);

    watcher_input_result_t up = watcher_input_router_on_button_up(&router, new_scope, 1120);
    assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
    assert(up.reject == WATCHER_INPUT_REJECT_NOT_PRESSED);
    assert(!watcher_input_router_consume_app_click(&router, new_scope));
}

static void test_unannounced_context_change_between_down_and_up_is_rejected(void) {
    watcher_input_scope_t old_scope = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 22);
    watcher_input_scope_t new_scope = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 23);
    watcher_input_router_t router = new_router(old_scope);

    watcher_input_router_on_button_down(&router, old_scope, 1000);
    watcher_input_result_t up = watcher_input_router_on_button_up(&router, new_scope, 1120);
    assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
    assert(up.reject == WATCHER_INPUT_REJECT_CONTEXT_CHANGED);
    assert(!watcher_input_router_consume_app_click(&router, new_scope));
}

static void test_same_mode_app_switch_discards_pending_click(void) {
    watcher_input_scope_t first = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 30);
    watcher_input_scope_t second = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 31);
    watcher_input_router_t router = new_router(first);

    watcher_input_router_on_button_down(&router, first, 1000);
    assert(watcher_input_router_on_button_up(&router, first, 1120).owner == WATCHER_INPUT_OWNER_APP);
    assert(!watcher_input_router_consume_app_click(&router, second));
}

static void test_short_click_boundaries(void) {
    watcher_input_scope_t current = scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 40);
    watcher_input_router_t router = new_router(current);

    watcher_input_router_on_button_down(&router, current, 1000);
    assert(watcher_input_router_on_button_up(&router, current, 1000).owner == WATCHER_INPUT_OWNER_APP);
    assert(watcher_input_router_consume_app_click(&router, current));

    watcher_input_router_on_button_down(&router, current, 2000);
    assert(watcher_input_router_on_button_up(&router, current, 3200).owner == WATCHER_INPUT_OWNER_APP);
    assert(watcher_input_router_consume_app_click(&router, current));

    watcher_input_router_on_button_down(&router, current, 4000);
    assert(watcher_input_router_on_button_up(&router, current, 5201).reject == WATCHER_INPUT_REJECT_TOO_LONG);
}

static void test_rotation_policy(void) {
    watcher_input_router_t router = new_router(scope(WATCHER_INPUT_CONTEXT_LVGL_NAV, 50));

    assert(watcher_input_router_on_rotate(&router, scope(WATCHER_INPUT_CONTEXT_LVGL_NAV, 50), 1).owner ==
           WATCHER_INPUT_OWNER_LVGL);
    assert(watcher_input_router_on_rotate(&router, scope(WATCHER_INPUT_CONTEXT_APP_ACTION, 51), -1).owner ==
           WATCHER_INPUT_OWNER_LVGL);
    assert(watcher_input_router_on_rotate(&router, scope(WATCHER_INPUT_CONTEXT_SYSTEM_ONLY, 52), 1).owner ==
           WATCHER_INPUT_OWNER_NONE);
}

static void test_repeated_short_clicks_never_accumulate_consumers(void) {
    watcher_input_router_t router = new_router(scope(WATCHER_INPUT_CONTEXT_LVGL_NAV, 60));
    size_t lvgl_clicks = 0;
    size_t app_clicks = 0;
    size_t rejected_clicks = 0;

    for (size_t i = 0; i < 10000; ++i) {
        const watcher_input_context_t context = (watcher_input_context_t)(WATCHER_INPUT_CONTEXT_LVGL_NAV + (i % 3));
        const watcher_input_scope_t current = scope(context, 60 + i);
        const uint32_t down_ms = (uint32_t)(1000 + i * 20);
        bool cancelled = false;

        watcher_input_router_set_scope(&router, current);
        watcher_input_result_t down = watcher_input_router_on_button_down(&router, current, down_ms);
        watcher_input_result_t up = watcher_input_router_on_button_up(&router, current, down_ms + 10);

        if (context == WATCHER_INPUT_CONTEXT_LVGL_NAV) {
            assert(down.owner == WATCHER_INPUT_OWNER_LVGL);
            assert(up.event == WATCHER_INPUT_EVENT_SHORT_CLICK);
            assert(up.owner == WATCHER_INPUT_OWNER_LVGL);
            assert(!watcher_input_router_consume_app_click(&router, current));
            ++lvgl_clicks;
        } else if (context == WATCHER_INPUT_CONTEXT_APP_ACTION) {
            assert(down.owner == WATCHER_INPUT_OWNER_APP);
            assert(up.event == WATCHER_INPUT_EVENT_SHORT_CLICK);
            assert(up.owner == WATCHER_INPUT_OWNER_APP);
            assert(watcher_input_router_consume_app_click(&router, current));
            assert(!watcher_input_router_consume_app_click(&router, current));
            ++app_clicks;
        } else {
            assert(down.owner == WATCHER_INPUT_OWNER_NONE);
            assert(up.event == WATCHER_INPUT_EVENT_REJECTED);
            assert(up.reject == WATCHER_INPUT_REJECT_SYSTEM_ONLY);
            assert(!watcher_input_router_consume_app_click(&router, current));
            ++rejected_clicks;
        }

        assert(!watcher_input_router_lvgl_button_pressed(&router, current, &cancelled));
        assert(!cancelled);
    }

    assert(lvgl_clicks == 3334);
    assert(app_clicks == 3333);
    assert(rejected_clicks == 3333);
}

int main(void) {
    test_lvgl_short_click_has_one_lvgl_owner();
    test_app_short_click_has_one_app_owner();
    test_system_only_rejects_short_click();
    test_unspecified_context_fails_closed();
    test_long_hold_is_system_owned_in_every_context();
    test_context_change_between_down_and_up_is_rejected();
    test_unannounced_context_change_between_down_and_up_is_rejected();
    test_same_mode_app_switch_discards_pending_click();
    test_short_click_boundaries();
    test_rotation_policy();
    test_repeated_short_clicks_never_accumulate_consumers();
    puts("watcher input router tests passed");
    return 0;
}
