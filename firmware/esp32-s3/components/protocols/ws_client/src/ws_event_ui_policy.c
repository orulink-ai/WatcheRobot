#include "ws_event_ui_policy.h"

#include <ctype.h>
#include <string.h>

static bool ws_ui_string_equals_nocase(const char *lhs, const char *rhs) {
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

bool ws_event_ui_should_apply_asr_result(void) {
    return false;
}

bool ws_event_ui_should_apply_thinking(const char *kind, bool tts_playing) {
    (void)kind;
    (void)tts_playing;
    return false;
}

bool ws_event_ui_should_apply_state(const char *state_id) {
    return state_id == NULL || strcmp(state_id, "thinking") != 0;
}

bool ws_event_ui_should_apply_task_status(const char *state_domain, const char *status) {
    if (!ws_ui_string_equals_nocase(state_domain, "task") || status == NULL) {
        return true;
    }

    return !ws_ui_string_equals_nocase(status, "happy") && !ws_ui_string_equals_nocase(status, "done") &&
           !ws_ui_string_equals_nocase(status, "completed") && !ws_ui_string_equals_nocase(status, "success") &&
           !ws_ui_string_equals_nocase(status, "succeeded");
}

bool ws_event_ui_should_apply_tts_completion(const char *completion_state, bool behavior_feedback_enabled,
                                             bool foreground_lease_active) {
    if (foreground_lease_active) {
        return true;
    }

    if (ws_ui_string_equals_nocase(completion_state, "happy") || ws_ui_string_equals_nocase(completion_state, "done") ||
        ws_ui_string_equals_nocase(completion_state, "completed") ||
        ws_ui_string_equals_nocase(completion_state, "success") ||
        ws_ui_string_equals_nocase(completion_state, "succeeded")) {
        return false;
    }

    return behavior_feedback_enabled;
}
