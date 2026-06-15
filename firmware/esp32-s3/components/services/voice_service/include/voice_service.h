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
    VOICE_EVENT_REMOTE_APPLY,       /* Apply latest remote recording target */
} voice_event_t;

/* Voice recorder statistics */
typedef struct {
    int record_count;  /* Number of recordings completed */
    int encode_count;  /* Number of audio frames accepted */
    int error_count;   /* Number of errors */
    int current_state; /* Current state (voice_state_t) */
} voice_stats_t;

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
 * @return ESP_OK if accepted or already recording, ESP_ERR_INVALID_STATE if the
 * recorder task is not running, ESP_ERR_TIMEOUT if the event queue is full.
 */
esp_err_t voice_recorder_request_open(void);

/**
 * Request recording stop from a remote control command.
 *
 * @return ESP_OK if accepted or already idle, ESP_ERR_INVALID_STATE if the
 * recorder task is not running, ESP_ERR_TIMEOUT if the event queue is full.
 */
esp_err_t voice_recorder_request_close(void);

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
 * Start voice recorder system (button + task)
 * This initializes the button and starts the recorder task
 * @return 0 on success, -1 on error
 */
int voice_recorder_start(void);

/**
 * Stop voice recorder system
 */
void voice_recorder_stop(void);

/**
 * Release active recording/cloud audio state while keeping the button
 * event consumer running.
 */
void voice_recorder_suspend_cloud_audio(void);

/**
 * Resume wake word detection after TTS playback
 * Called when TTS playback completes to re-enable wake word detection
 */
void voice_recorder_resume_wake_word(void);

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
