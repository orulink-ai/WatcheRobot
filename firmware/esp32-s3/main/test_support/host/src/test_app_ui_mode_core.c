#include "app_ui_mode_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    unsigned create_text_calls;
    unsigned create_animation_calls;
    unsigned create_fullscreen_calls;
    unsigned bind_calls;
    unsigned unbind_calls;
    unsigned rollback_calls;
    unsigned release_calls;
    bool fail_create;
    bool fail_bind;
    bool fail_unbind;
} fake_ui_t;

static int prepare_mode(void *ctx, app_ui_mode_t previous, app_ui_mode_t mode, void **surface_out) {
    fake_ui_t *fake = ctx;
    (void)previous;
    if (mode == APP_UI_MODE_TEXT_ONLY) {
        fake->create_text_calls++;
    } else if (mode == APP_UI_MODE_ANIMATION) {
        fake->create_animation_calls++;
    } else if (mode == APP_UI_MODE_FULLSCREEN) {
        fake->create_fullscreen_calls++;
    }
    if (fake->fail_create) {
        return -1;
    }
    *surface_out = mode == APP_UI_MODE_ANIMATION ? fake : NULL;
    return 0;
}

static int bind_surface(void *ctx, void *surface) {
    fake_ui_t *fake = ctx;
    assert(surface == fake);
    fake->bind_calls++;
    return fake->fail_bind ? -1 : 0;
}

static int unbind_surface(void *ctx) {
    fake_ui_t *fake = ctx;
    fake->unbind_calls++;
    return fake->fail_unbind ? -1 : 0;
}

static void rollback_mode(void *ctx, app_ui_mode_t previous, app_ui_mode_t mode) {
    (void)previous;
    (void)mode;
    ((fake_ui_t *)ctx)->rollback_calls++;
}

static void release_mode(void *ctx, app_ui_mode_t mode) {
    (void)mode;
    ((fake_ui_t *)ctx)->release_calls++;
}

static app_ui_mode_ops_t make_ops(fake_ui_t *fake) {
    app_ui_mode_ops_t ops = {
        .prepare_mode = prepare_mode,
        .bind_surface = bind_surface,
        .unbind_surface = unbind_surface,
        .rollback_mode = rollback_mode,
        .release_mode = release_mode,
        .ctx = fake,
    };
    return ops;
}

static void test_text_only_upgrades_and_binds_before_animation_mode_is_visible(void) {
    fake_ui_t fake = {0};
    app_ui_mode_core_t core;
    app_ui_mode_ops_t ops = make_ops(&fake);

    app_ui_mode_core_init(&core);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_TEXT_ONLY, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_TEXT_ONLY);
    assert(!app_ui_mode_core_surface_bound(&core));

    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    assert(fake.create_animation_calls == 1U);
    assert(fake.bind_calls == 1U);
    assert(fake.rollback_calls == 0U);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_ANIMATION);
    assert(app_ui_mode_core_surface_bound(&core));

    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    assert(fake.create_animation_calls == 1U);
    assert(fake.bind_calls == 1U);
}

static void test_bind_failure_never_claims_animation_capability(void) {
    fake_ui_t fake = {0};
    app_ui_mode_core_t core;
    app_ui_mode_ops_t ops = make_ops(&fake);

    app_ui_mode_core_init(&core);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_TEXT_ONLY, &ops) == APP_UI_MODE_RESULT_OK);
    fake.fail_bind = true;
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_BIND_FAILED);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_TEXT_ONLY);
    assert(!app_ui_mode_core_surface_bound(&core));
    assert(fake.rollback_calls == 1U);
}

static void test_close_and_reenter_recreate_and_rebind(void) {
    fake_ui_t fake = {0};
    app_ui_mode_core_t core;
    app_ui_mode_ops_t ops = make_ops(&fake);

    app_ui_mode_core_init(&core);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_close(&core, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_NONE);
    assert(!app_ui_mode_core_surface_bound(&core));
    assert(fake.unbind_calls == 1U);
    assert(fake.release_calls == 1U);

    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_TEXT_ONLY, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    assert(fake.bind_calls == 2U);
}

static void test_close_keeps_surface_alive_when_unbind_fails(void) {
    fake_ui_t fake = {0};
    app_ui_mode_core_t core;
    app_ui_mode_ops_t ops = make_ops(&fake);

    app_ui_mode_core_init(&core);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    fake.fail_unbind = true;
    assert(app_ui_mode_core_close(&core, &ops) == APP_UI_MODE_RESULT_UNBIND_FAILED);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_ANIMATION);
    assert(app_ui_mode_core_surface_bound(&core));
    assert(fake.release_calls == 0U);

    fake.fail_unbind = false;
    assert(app_ui_mode_core_close(&core, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_NONE);
    assert(fake.release_calls == 1U);
}

static void test_fullscreen_phone_page_can_upgrade_to_animation(void) {
    fake_ui_t fake = {0};
    app_ui_mode_core_t core;
    app_ui_mode_ops_t ops = make_ops(&fake);

    app_ui_mode_core_init(&core);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_FULLSCREEN, &ops) == APP_UI_MODE_RESULT_OK);
    assert(app_ui_mode_core_ensure(&core, APP_UI_MODE_ANIMATION, &ops) == APP_UI_MODE_RESULT_OK);
    assert(fake.create_fullscreen_calls == 1U);
    assert(fake.create_animation_calls == 1U);
    assert(fake.bind_calls == 1U);
    assert(app_ui_mode_core_get(&core) == APP_UI_MODE_ANIMATION);
}

int main(void) {
    test_text_only_upgrades_and_binds_before_animation_mode_is_visible();
    test_bind_failure_never_claims_animation_capability();
    test_close_and_reenter_recreate_and_rebind();
    test_close_keeps_surface_alive_when_unbind_fails();
    test_fullscreen_phone_page_can_upgrade_to_animation();
    puts("app ui mode core host tests passed");
    return 0;
}
