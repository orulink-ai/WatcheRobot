#include "voice_listening_ui_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_sleep_chain_uses_wake_intro(void) {
    assert(voice_listening_ui_should_use_wake_intro("standby_entry", false));
    assert(voice_listening_ui_should_use_wake_intro("standby_start", false));
    assert(voice_listening_ui_should_use_wake_intro("standby_loop", false));
    assert(voice_listening_ui_should_use_wake_intro("listening_wake", false));
    assert(voice_listening_ui_should_use_wake_intro(NULL, true));
}

static void test_awake_and_active_states_skip_wake_intro(void) {
    assert(!voice_listening_ui_should_use_wake_intro("standby", false));
    assert(!voice_listening_ui_should_use_wake_intro("listening", false));
    assert(!voice_listening_ui_should_use_wake_intro("thinking", false));
    assert(!voice_listening_ui_should_use_wake_intro(NULL, false));
}

int main(void) {
    test_sleep_chain_uses_wake_intro();
    test_awake_and_active_states_skip_wake_intro();
    puts("voice listening UI policy host tests passed");
    return 0;
}
