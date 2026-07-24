#pragma once

#include <stdbool.h>

bool ws_event_ui_should_apply_asr_result(void);
bool ws_event_ui_should_apply_thinking(const char *kind, bool tts_playing);
bool ws_event_ui_should_apply_state(const char *state_id);
bool ws_event_ui_should_apply_task_status(const char *state_domain, const char *status);
bool ws_event_ui_should_apply_tts_completion(const char *completion_state, bool behavior_feedback_enabled,
                                             bool foreground_lease_active);
