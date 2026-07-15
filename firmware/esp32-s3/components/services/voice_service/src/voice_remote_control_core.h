#ifndef VOICE_REMOTE_CONTROL_CORE_H
#define VOICE_REMOTE_CONTROL_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VOICE_REMOTE_ACTION_NONE = 0,
    VOICE_REMOTE_ACTION_START,
    VOICE_REMOTE_ACTION_STOP,
} voice_remote_action_t;

typedef struct {
    bool recording_desired;
    bool recording_permitted;
    uint32_t generation;
} voice_remote_snapshot_t;

typedef struct {
    bool recording_desired;
    bool recording_permitted;
    uint32_t applied_generation;
} voice_remote_control_state_t;

typedef struct {
    voice_remote_snapshot_t target;
    bool accepting_remote;
    uint32_t suspend_requested_generation;
} voice_remote_mailbox_state_t;

typedef struct {
    uint32_t generation;
    bool needs_ack;
} voice_remote_suspend_request_t;

void voice_remote_mailbox_init(voice_remote_mailbox_state_t *mailbox);
void voice_remote_mailbox_prepare_session(voice_remote_mailbox_state_t *mailbox);
void voice_remote_mailbox_open_session(voice_remote_mailbox_state_t *mailbox);
void voice_remote_mailbox_close_session(voice_remote_mailbox_state_t *mailbox);
bool voice_remote_mailbox_publish_desired(voice_remote_mailbox_state_t *mailbox, bool desired);
bool voice_remote_mailbox_publish_permitted(voice_remote_mailbox_state_t *mailbox, bool permitted);
voice_remote_suspend_request_t voice_remote_mailbox_request_suspend(voice_remote_mailbox_state_t *mailbox);

void voice_remote_control_init(voice_remote_control_state_t *state);

voice_remote_action_t voice_remote_control_apply(voice_remote_control_state_t *state,
                                                  voice_remote_snapshot_t snapshot,
                                                  bool recording_active);

#endif
