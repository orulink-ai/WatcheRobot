#include "animation_playback_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_explicit_modes_override_resource_default(void) {
    assert(!animation_playback_should_cycle(ANIM_PLAYBACK_ONCE, 0U, true));
    assert(animation_playback_should_cycle(ANIM_PLAYBACK_LOOP_UNTIL_REPLACED, 0U, false));
    assert(animation_playback_should_cycle(ANIM_PLAYBACK_REPEAT_COUNT, 2U, false));
}

static void test_legacy_mode_preserves_existing_resource_fallback(void) {
    assert(animation_playback_should_cycle(ANIM_PLAYBACK_RESOURCE_DEFAULT, 0U, true));
    assert(!animation_playback_should_cycle(ANIM_PLAYBACK_RESOURCE_DEFAULT, 0U, false));
    assert(animation_playback_should_cycle(ANIM_PLAYBACK_RESOURCE_DEFAULT, 2U, false));
}

static void test_request_validation_rejects_ambiguous_combinations(void) {
    assert(animation_playback_request_is_valid(ANIM_PLAYBACK_RESOURCE_DEFAULT, 0U));
    assert(animation_playback_request_is_valid(ANIM_PLAYBACK_RESOURCE_DEFAULT, 2U));
    assert(animation_playback_request_is_valid(ANIM_PLAYBACK_ONCE, 0U));
    assert(animation_playback_request_is_valid(ANIM_PLAYBACK_REPEAT_COUNT, 2U));
    assert(animation_playback_request_is_valid(ANIM_PLAYBACK_LOOP_UNTIL_REPLACED, 0U));

    assert(!animation_playback_request_is_valid(ANIM_PLAYBACK_ONCE, 1U));
    assert(!animation_playback_request_is_valid(ANIM_PLAYBACK_REPEAT_COUNT, 0U));
    assert(!animation_playback_request_is_valid(ANIM_PLAYBACK_LOOP_UNTIL_REPLACED, 1U));
    assert(!animation_playback_request_is_valid(ANIM_PLAYBACK_MODE_COUNT, 0U));
}

int main(void) {
    test_explicit_modes_override_resource_default();
    test_legacy_mode_preserves_existing_resource_fallback();
    test_request_validation_rejects_ambiguous_combinations();
    puts("animation playback policy host tests passed");
    return 0;
}
