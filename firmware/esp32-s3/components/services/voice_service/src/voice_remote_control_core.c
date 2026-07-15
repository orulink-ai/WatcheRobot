#include "voice_remote_control_core.h"

#include <stddef.h>

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation == 0U ? 1U : generation;
}

void voice_remote_mailbox_init(voice_remote_mailbox_state_t *mailbox) {
    if (mailbox == NULL) {
        return;
    }
    mailbox->target.recording_desired = false;
    mailbox->target.recording_permitted = true;
    mailbox->target.generation = 1U;
    mailbox->accepting_remote = false;
    mailbox->suspend_requested_generation = 0U;
}

void voice_remote_mailbox_prepare_session(voice_remote_mailbox_state_t *mailbox) {
    if (mailbox == NULL) {
        return;
    }
    mailbox->accepting_remote = false;
    mailbox->target.recording_desired = false;
    mailbox->target.generation = next_generation(mailbox->target.generation);
    mailbox->suspend_requested_generation = 0U;
}

void voice_remote_mailbox_open_session(voice_remote_mailbox_state_t *mailbox) {
    if (mailbox != NULL) {
        mailbox->accepting_remote = true;
    }
}

void voice_remote_mailbox_close_session(voice_remote_mailbox_state_t *mailbox) {
    if (mailbox == NULL) {
        return;
    }
    mailbox->accepting_remote = false;
    mailbox->target.recording_desired = false;
    mailbox->target.recording_permitted = true;
    mailbox->target.generation = next_generation(mailbox->target.generation);
}

bool voice_remote_mailbox_publish_desired(voice_remote_mailbox_state_t *mailbox, bool desired) {
    if (mailbox == NULL || !mailbox->accepting_remote) {
        return false;
    }
    mailbox->target.recording_desired = desired;
    mailbox->target.generation = next_generation(mailbox->target.generation);
    return true;
}

bool voice_remote_mailbox_publish_permitted(voice_remote_mailbox_state_t *mailbox, bool permitted) {
    if (mailbox == NULL) {
        return false;
    }
    bool changed = mailbox->target.recording_permitted != permitted;
    if (changed) {
        mailbox->target.recording_permitted = permitted;
        mailbox->target.generation = next_generation(mailbox->target.generation);
    }
    return changed;
}

voice_remote_suspend_request_t voice_remote_mailbox_request_suspend(voice_remote_mailbox_state_t *mailbox) {
    voice_remote_suspend_request_t request = {0};
    if (mailbox == NULL) {
        return request;
    }
    mailbox->target.recording_desired = false;
    mailbox->target.generation = next_generation(mailbox->target.generation);
    mailbox->suspend_requested_generation = mailbox->target.generation;
    request.generation = mailbox->suspend_requested_generation;
    request.needs_ack = mailbox->accepting_remote;
    return request;
}

void voice_remote_control_init(voice_remote_control_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->recording_desired = false;
    state->recording_permitted = true;
    state->applied_generation = 0U;
}

voice_remote_action_t voice_remote_control_apply(voice_remote_control_state_t *state,
                                                  voice_remote_snapshot_t snapshot,
                                                  bool recording_active) {
    if (state == NULL || snapshot.generation == state->applied_generation) {
        return VOICE_REMOTE_ACTION_NONE;
    }

    state->recording_desired = snapshot.recording_desired;
    state->recording_permitted = snapshot.recording_permitted;
    state->applied_generation = snapshot.generation;

    if (!snapshot.recording_desired && recording_active) {
        return VOICE_REMOTE_ACTION_STOP;
    }
    if (snapshot.recording_desired && snapshot.recording_permitted && !recording_active) {
        return VOICE_REMOTE_ACTION_START;
    }
    return VOICE_REMOTE_ACTION_NONE;
}
