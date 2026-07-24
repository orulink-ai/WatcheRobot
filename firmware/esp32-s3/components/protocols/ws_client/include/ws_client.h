#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @file ws_client.h
 * @brief WebSocket client interface (Watcher protocol v0.1.5)
 */

typedef enum {
    WS_FRAME_TYPE_AUDIO = 1,
    WS_FRAME_TYPE_VIDEO = 2,
    WS_FRAME_TYPE_IMAGE = 3,
    WS_FRAME_TYPE_OTA = 4,
    WS_FRAME_TYPE_APP_PACKAGE = 5,
} ws_frame_type_t;

typedef enum {
    WS_FRAME_FLAG_NONE = 0,
    WS_FRAME_FLAG_FIRST = 1 << 0,
    WS_FRAME_FLAG_LAST = 1 << 1,
    WS_FRAME_FLAG_KEYFRAME = 1 << 2,
    WS_FRAME_FLAG_FRAGMENT = 1 << 3,
    WS_FRAME_FLAG_CANCEL = 1 << 4,
} ws_frame_flag_t;

typedef enum {
    WS_AUDIO_UPLINK_CODEC_PCM_S16LE = 0,
    WS_AUDIO_UPLINK_CODEC_OPUS = 1,
} ws_audio_uplink_codec_t;

typedef struct {
    bool valid;
    bool binary;
    uint8_t frame_type;
    size_t payload_len;
    uint32_t packet_len;
    uint32_t lock_wait_us;
    uint32_t send_us;
    uint32_t total_us;
    uint64_t timestamp_us;
} ws_client_media_send_stats_t;

typedef struct {
    bool valid;
    uint32_t queued_frames;
    uint32_t sent_frames;
    uint32_t dropped_frames;
    uint32_t pending_frames;
    uint32_t inflight_frames;
    uint32_t resident_frames;
    uint32_t high_watermark;
    uint32_t last_queue_delay_us;
    bool session_open;
    bool first_frame_pending;
    bool end_pending;
} ws_client_audio_queue_stats_t;

#define WS_APP_PACKAGE_ID_MAX 64
#define WS_APP_PACKAGE_NAME_MAX 96
#define WS_APP_PACKAGE_VERSION_MAX 32
#define WS_APP_PACKAGE_DESCRIPTION_MAX 160
#define WS_APP_PACKAGE_TYPE_MAX 32
#define WS_APP_PACKAGE_URL_MAX 256

typedef struct {
    char command_id[48];
    char app_id[WS_APP_PACKAGE_ID_MAX];
    char name[WS_APP_PACKAGE_NAME_MAX];
    char version[WS_APP_PACKAGE_VERSION_MAX];
    char description[WS_APP_PACKAGE_DESCRIPTION_MAX];
    char package_type[WS_APP_PACKAGE_TYPE_MAX];
    char source_url[WS_APP_PACKAGE_URL_MAX];
    char image_version[WS_APP_PACKAGE_VERSION_MAX];
    char file_name[96];
    char sha256[80];
    size_t size_bytes;
} ws_app_package_transfer_t;

typedef struct {
    char command_id[48];
    char app_id[WS_APP_PACKAGE_ID_MAX];
    char name[WS_APP_PACKAGE_NAME_MAX];
    char version[WS_APP_PACKAGE_VERSION_MAX];
} ws_app_package_command_t;

typedef struct {
    int (*begin)(const ws_app_package_transfer_t *transfer);
    int (*write_frame)(uint8_t flags, const uint8_t *payload, size_t payload_len);
    int (*commit)(const ws_app_package_transfer_t *transfer);
    void (*abort)(const ws_app_package_command_t *command, const char *reason);
    int (*install)(const ws_app_package_transfer_t *transfer);
    int (*open)(const ws_app_package_command_t *command);
    int (*uninstall)(const ws_app_package_command_t *command);
    int (*list)(const ws_app_package_command_t *command);
} ws_app_package_handler_t;

/**
 * Optional application-owned text handler. Return true when the message was
 * consumed. The callback runs on the WebSocket event task and must only copy
 * work into an application queue.
 */
typedef bool (*ws_client_text_handler_t)(const char *json, size_t json_len, void *context);

/**
 * Optional application-owned gate for inbound TTS frames. Applications use
 * this to bind the binary data plane to an authenticated control session.
 */
typedef bool (*ws_client_tts_frame_guard_t)(uint8_t flags, uint16_t stream_id, uint32_t sequence, size_t payload_len,
                                            void *context);

/**
 * Initialize WebSocket client
 */
int ws_client_init(void);

/**
 * Set server URL (before ws_client_start)
 * @param url WebSocket URL (e.g., "ws://192.168.1.100:8765")
 * @return 0 on success, -1 on error
 */
int ws_client_set_server_url(const char *url);

/**
 * Set the temporary SDK Control pairing code included in the next
 * sys.client.hello message. Pass NULL to clear it for non-SDK sessions.
 */
void ws_client_set_session_pairing_code(const char *pairing_code);

/**
 * Get current server URL
 * @return Server URL string (static buffer, do not free)
 */
const char *ws_client_get_server_url(void);

/**
 * Start WebSocket connection
 */
int ws_client_start(void);

/**
 * Stop WebSocket connection
 */
void ws_client_stop(void);

/**
 * Stop WebSocket connection while releasing an app-owned resource set.
 *
 * This aborts media without resuming wake-word capture, because the owning app
 * is leaving and its audio resources must stay idle until another app starts.
 */
void ws_client_stop_for_resource_release(void);

/**
 * Destroy the current WebSocket client handle.
 */
esp_err_t ws_client_deinit(void);

/**
 * Send binary data via WebSocket
 * @param data Data buffer
 * @param len Data length
 * @return Bytes sent on success, -1 on error
 */
int ws_client_send_binary(const uint8_t *data, int len);

/**
 * Send text message via WebSocket
 * @param text Text message
 * @return Bytes sent on success, -1 on error
 */
int ws_client_send_text(const char *text);

/**
 * Check if WebSocket is connected
 */
int ws_client_is_connected(void);

/**
 * Check if the WebSocket transport has been started.
 */
int ws_client_is_started(void);

/**
 * Apply cleanup requested by a synchronous WebSocket error/disconnect callback.
 * Call this from the transport coordinator task so cleanup cannot recursively
 * acquire the outbound send mutex from inside the ESP WebSocket callback.
 */
void ws_client_process_deferred_cleanup(void);

/**
 * Check if the WebSocket session is ready for business traffic.
 */
int ws_client_is_session_ready(void);

/**
 * Check if the server rejected the current hello handshake.
 */
bool ws_client_has_hello_rejected(void);

/**
 * Check if the ready session has not received any inbound traffic recently.
 */
bool ws_client_is_session_stale(uint32_t stale_ms);

/**
 * Invalidate the current logical session without destroying the client handle.
 */
void ws_client_invalidate_session(const char *reason);

/**
 * Enable or suppress behavior-state UI feedback emitted by the shared
 * WebSocket transport. Local apps that own their own connection UI should
 * disable this and render app-specific feedback instead.
 */
void ws_client_set_behavior_feedback_enabled(bool enabled);

/**
 * Register device-side App.Center package handlers.
 *
 * The WebSocket component owns protocol parsing and frame routing only. The
 * app layer owns package storage, validation, install state, open, and
 * uninstall behavior.
 */
void ws_client_register_app_package_handler(const ws_app_package_handler_t *handler);
void ws_client_register_text_handler(ws_client_text_handler_t handler, void *context);
void ws_client_register_tts_frame_guard(ws_client_tts_frame_guard_t guard, void *context);

/**
 * Mark that the server has responded to the current voice session.
 */
void ws_client_mark_server_response(void);

/**
 * Mark the hello handshake as acknowledged.
 */
void ws_client_mark_hello_acked(void);

/** Apply the server-selected microphone uplink codec before accepting hello. */
bool ws_client_apply_audio_uplink_negotiation(const char *codec, int sample_rate, int channels, int frame_duration_ms,
                                              const char *packetization, int version);

/** Return the codec selected for the current WebSocket connection. */
ws_audio_uplink_codec_t ws_client_get_audio_uplink_codec(void);

/**
 * Send sys.pong in response to a server ping.
 */
int ws_send_sys_pong(void);

/**
 * Send sys.ack for an accepted control command.
 */
int ws_send_sys_ack(const char *message_type, const char *command_id);

/**
 * Send sys.nack for a rejected control command.
 */
int ws_send_sys_nack(const char *message_type, const char *command_id, const char *reason);

/**
 * Send the current device firmware metadata.
 */
int ws_send_device_firmware(void);

/**
 * Send a device error event to desktop observers.
 */
int ws_send_device_error(int code, const char *message);

/**
 * Send OTA progress to desktop observers.
 */
int ws_send_ota_progress(int progress, const char *state, const char *message);

/**
 * Send OTA handshake metadata for the current firmware.
 */
int ws_send_ota_handshake(const char *transfer_id, const char *status);

/**
 * Report App.Center package state to desktop clients.
 */
int ws_send_app_package_status(const char *command_id, const char *app_id, const char *name, const char *version,
                               const char *state, const char *message);

/**
 * Send a JSON array payload for evt.app.package.list.
 */
int ws_send_app_package_list_json(const char *command_id, const char *apps_json);

/**
 * Send the current servo position.
 */
int ws_send_servo_position(float x_deg, float y_deg);
int ws_send_servo_feedback(const char *axis, uint16_t raw, float angle);

/**
 * Send a camera state event.
 */
int ws_send_camera_state(const char *action, const char *state, int fps, const char *message);

/**
 * Send one MJPEG video frame using the WSPK binary header.
 */
int ws_send_video_frame(const uint8_t *jpeg, size_t len, bool first_frame);

/**
 * Send the terminal marker of a video stream using a zero-payload WSPK frame.
 */
int ws_send_video_end(void);

/**
 * Send one JPEG image using the WSPK binary header.
 */
int ws_send_image_frame(const uint8_t *jpeg, size_t len);

/**
 * Get the most recent media send timing sample.
 */
void ws_client_get_media_send_stats(ws_client_media_send_stats_t *stats);

/**
 * Send audio data via WebSocket using WSPK audio frames.
 * @param data Audio data (PCM 16-bit, 16kHz, mono, LE)
 * @param len Data length
 * @note This call only enqueues audio; a worker task performs the actual send.
 * @return 0 on success, -1 on error
 */
int ws_send_audio(const uint8_t *data, int len);

/**
 * Send audio end marker via WebSocket using a zero-payload LAST audio frame.
 * @return 0 on success, -1 on error
 */
int ws_send_audio_end(void);

/**
 * Cancel the active audio upload without recognizing its partial contents.
 * Pending PCM is discarded and a terminal CANCEL frame is sent after the
 * current WebSocket write returns.
 */
int ws_cancel_audio_upload(void);

/**
 * Handle TTS binary frame from WebSocket.
 * @param data Binary frame data (PCM 16-bit, 24kHz, mono, LE)
 * @param len Frame length
 */
void ws_handle_tts_binary(const uint8_t *data, int len);

/**
 * Signal TTS playback complete
 * Call this when the LAST audio frame is received.
 */
void ws_tts_complete(void);

/**
 * Record the server's textual TTS completion notice. If the binary LAST frame
 * is lost, this arms a bounded fallback so the speaking presentation cannot
 * remain active indefinitely.
 */
void ws_client_note_tts_downlink_complete(void);

/**
 * Abort any in-progress or queued TTS playback.
 *
 * This function is idempotent and may be called before starting microphone capture
 * to ensure the audio path is no longer in playback mode.
 */
void ws_client_abort_tts_playback(void);

/**
 * Return whether cloud TTS playback currently owns the audio path.
 */
bool ws_client_is_tts_playing(void);

/**
 * Check response timeout and recover wake-word mode if needed.
 */
void ws_tts_timeout_check(void);

/**
 * Get the current audio queue statistics.
 */
void ws_client_get_audio_queue_stats(ws_client_audio_queue_stats_t *stats);

#endif /* WS_CLIENT_H */
