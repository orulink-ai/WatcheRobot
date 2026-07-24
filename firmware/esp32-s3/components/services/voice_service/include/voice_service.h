#ifndef BUTTON_VOICE_H
#define BUTTON_VOICE_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/* Voice recorder states */
typedef enum {
    VOICE_STATE_IDLE = 0,  /* Not recording */
    VOICE_STATE_RECORDING, /* Currently recording */
} voice_state_t;

/* Voice recorder events */
typedef enum {
    VOICE_EVENT_NONE = 0,
    VOICE_EVENT_BUTTON_SHORT_CLICK, /* Debounced single-click toggle */
    VOICE_EVENT_TIMEOUT,            /* Max recording time reached */
    VOICE_EVENT_WAKE_WORD,          /* Wake word detected - start recording */
} voice_event_t;

/* Voice recorder statistics */
typedef struct {
    int record_count;  /* Number of recordings completed */
    int encode_count;  /* Number of audio frames accepted */
    int error_count;   /* Number of errors */
    int current_state; /* Current state (voice_state_t) */
} voice_stats_t;

typedef struct {
    bool (*is_ready)(void *user_ctx);
    void (*abort_playback)(void *user_ctx);
    int (*send_audio)(const uint8_t *data, int len, void *user_ctx);
    int (*send_audio_end)(void *user_ctx);
    int (*cancel_audio)(void *user_ctx);
    void *user_ctx;
} voice_transport_t;

/**
 * Initialize voice recorder
 */
void voice_recorder_init(void);

/**
 * Get current state
 */
voice_state_t voice_recorder_get_state(void);

/**
 * Process an event (called from recorder task or timer)
 */
void voice_recorder_process_event(voice_event_t event);

/**
 * Request recording start from a remote control command.
 *
 * @return ESP_OK if the latest-value target was published, or
 * ESP_ERR_INVALID_STATE if the recorder task is not running.
 */
esp_err_t voice_recorder_request_open(void);

/**
 * Request recording stop from a remote control command.
 *
 * @return ESP_OK if the latest-value target was published, or
 * ESP_ERR_INVALID_STATE if the recorder task is not running.
 */
esp_err_t voice_recorder_request_close(void);

/**
 * Request task-owned release of the startup audio guard.
 *
 * The recorder task applies the release only while the recorder is idle. This
 * prevents a concurrent recording transition from being overwritten by a
 * caller-side audio guard clear.
 *
 * @return ESP_OK if the request was accepted, or ESP_ERR_INVALID_STATE if the
 * recorder task is not running.
 */
esp_err_t voice_recorder_request_startup_audio_release(void);

/**
 * Process a tick (called periodically to read audio and enqueue upload)
 * Should be called at audio frame rate (e.g., every 60ms for PCM frames)
 *
 * @return Number of frames accepted, or -1 on error
 */
int voice_recorder_tick(void);

/**
 * Get statistics
 */
void voice_recorder_get_stats(voice_stats_t *out_stats);

/**
 * Reset statistics
 */
void voice_recorder_reset_stats(void);

/**
 * Override the voice recorder upload transport.
 *
 * Passing NULL restores the default Watcher WebSocket transport. The recorder
 * does not own user_ctx; callers must keep it valid until the transport is
 * reset or replaced.
 */
void voice_recorder_set_transport(const voice_transport_t *transport);

/**
 * Restore the default Watcher WebSocket transport.
 */
void voice_recorder_reset_transport(void);

/**
 * Enable or suppress recorder-owned behavior UI feedback.
 *
 * Apps that reuse the recorder with their own session state, such as Agent,
 * can disable this so remote recorder open/close events do not overwrite the
 * app-level listening/thinking/ready UI.
 */
void voice_recorder_set_behavior_feedback_enabled(bool enabled);

/**
 * Gate physical recording start without discarding the latest remote target.
 *
 * Agent closes this gate while its wake animation chain is incomplete. An
 * early open request is retained and applied after the gate is opened. Other
 * apps leave the gate open by default.
 */
void voice_recorder_set_recording_permitted(bool permitted);

/**
 * Start voice recorder system (button + task)
 * This initializes the button and starts the recorder task
 * @return 0 on success, -1 on error
 */
int voice_recorder_start(void);

/**
 * Temporarily ignore physical button clicks in the voice recorder.
 *
 * This is used when another UI flow consumes the same button click before the
 * voice button runtime starts, preventing the release/queued click from being
 * interpreted as a voice command.
 */
void voice_recorder_suppress_button_clicks(uint32_t duration_ms);

/**
 * Stop voice recorder system
 */
esp_err_t voice_recorder_stop(void);

/**
 * Close the voice recorder application runtime and release its audio HAL
 * resources.
 */
esp_err_t voice_recorder_close(void);

/**
 * Request the Voice Task to release active recording/cloud audio state while
 * keeping the button event consumer running. The call waits for task-side
 * application up to the internal bounded timeout and logs an explicit error
 * if confirmation is not received.
 */
void voice_recorder_suspend_cloud_audio(void);

/**
 * Resume wake word detection for legacy callers.
 *
 * New Voice App flows should prefer voice_recorder_resume_wake_word_for_sleep()
 * so WakeNet is rebuilt only when the UI enters its sleep standby path.
 */
void voice_recorder_resume_wake_word(void);

/**
 * Resume wake word detection when Voice App actually enters sleep standby.
 */
void voice_recorder_resume_wake_word_for_sleep(void);

/**
 * Pause wake word detection before TTS playback
 * Called when TTS starts to prevent I2S conflicts between wake word (in) and TTS (out)
 */
void voice_recorder_pause_wake_word(void);

/* ------------------------------------------------------------------ */
/* HAL Interface (to be implemented by hardware/audio layer)          */
/* ------------------------------------------------------------------ */

/**
 * Start audio capture (HAL)
 */
int hal_audio_start(void);

/**
 * Read audio samples (HAL)
 * @param out_buf Output buffer
 * @param max_len Maximum length
 * @return Number of bytes read, or -1 on error
 */
int hal_audio_read(uint8_t *out_buf, int max_len);

/**
 * Stop audio capture (HAL)
 */
int hal_audio_stop(void);

/**
 * Prepare audio for upload (HAL passthrough)
 * @param pcm_in PCM input data
 * @param pcm_len PCM data length
 * @param opus_out Output buffer for transport data
 * @param out_max Maximum output size
 * @return Transport data length, or -1 on error
 */
int hal_opus_encode(const uint8_t *pcm_in, int pcm_len, uint8_t *opus_out, int out_max);

/* ------------------------------------------------------------------ */
/* WebSocket Interface (to be implemented by network layer)           */
/* ------------------------------------------------------------------ */

/**
 * Send audio data to cloud (WS)
 * @param data Audio data (PCM 16-bit, 16kHz, mono, LE)
 * @param len Data length
 * @return 0 on success, -1 on error
 */
int ws_send_audio(const uint8_t *data, int len);

/**
 * Send audio end marker (WS)
 * @return 0 on success, -1 on error
 */
int ws_send_audio_end(void);

#endif /* BUTTON_VOICE_H */
