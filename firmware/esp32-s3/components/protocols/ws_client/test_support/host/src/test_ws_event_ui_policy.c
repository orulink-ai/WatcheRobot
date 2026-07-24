#include "ws_event_ui_policy.h"

#include <assert.h>
#include <stdio.h>

static void test_speaking_blocks_all_thinking_events(void) {
    assert(!ws_event_ui_should_apply_thinking("", true));
    assert(!ws_event_ui_should_apply_thinking("dialogue.thinking", true));
    assert(!ws_event_ui_should_apply_thinking("watcher_vnext", true));
}

static void test_progress_events_never_drive_ui(void) {
    assert(!ws_event_ui_should_apply_thinking("voice.latency.stage", false));
    assert(!ws_event_ui_should_apply_thinking("llm.first_reply.delta", false));
    assert(!ws_event_ui_should_apply_thinking("llm.voice_chat.delta", false));
    assert(!ws_event_ui_should_apply_thinking("communication_brain.delayed_ack", false));
    assert(!ws_event_ui_should_apply_thinking("watcher_vnext", false));
    assert(!ws_event_ui_should_apply_thinking("tool_call", false));
    assert(!ws_event_ui_should_apply_thinking("tool_result", false));
}

static void test_remote_thinking_events_do_not_restart_local_presentation(void) {
    assert(!ws_event_ui_should_apply_asr_result());
    assert(!ws_event_ui_should_apply_thinking("", false));
    assert(!ws_event_ui_should_apply_thinking("dialogue.thinking", false));
    assert(!ws_event_ui_should_apply_state("thinking"));
    assert(ws_event_ui_should_apply_state("listening"));
    assert(ws_event_ui_should_apply_state("custom2"));
    assert(ws_event_ui_should_apply_state("speaking"));
}

static void test_task_success_releases_lifecycle_without_success_presentation(void) {
    assert(!ws_event_ui_should_apply_task_status("task", "happy"));
    assert(!ws_event_ui_should_apply_task_status("task", "completed"));
    assert(!ws_event_ui_should_apply_task_status("task", "success"));
    assert(!ws_event_ui_should_apply_task_status("task", "succeeded"));
    assert(ws_event_ui_should_apply_task_status("task", "error"));
    assert(ws_event_ui_should_apply_task_status("task", "custom3"));
    assert(ws_event_ui_should_apply_task_status("dialogue", "happy"));
}

static void test_tts_completion_skips_unowned_success_but_preserves_foreground_and_errors(void) {
    assert(!ws_event_ui_should_apply_tts_completion("happy", true, false));
    assert(!ws_event_ui_should_apply_tts_completion("completed", true, false));
    assert(ws_event_ui_should_apply_tts_completion("happy", true, true));
    assert(ws_event_ui_should_apply_tts_completion("custom3", false, true));
    assert(ws_event_ui_should_apply_tts_completion("error", true, false));
    assert(!ws_event_ui_should_apply_tts_completion("error", false, false));
}

int main(void) {
    test_speaking_blocks_all_thinking_events();
    test_progress_events_never_drive_ui();
    test_remote_thinking_events_do_not_restart_local_presentation();
    test_task_success_releases_lifecycle_without_success_presentation();
    test_tts_completion_skips_unowned_success_but_preserves_foreground_and_errors();
    puts("ws event UI policy host tests passed");
    return 0;
}
