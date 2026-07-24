#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *text;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    rewind(file);
    text = (char *)malloc((size_t)size + 1U);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    fclose(file);
    text[size] = '\0';
    return text;
}

static int expect_error_callback_is_lock_free(const char *root) {
    char path[1024];
    char *text;
    char *begin;
    char *end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client.c\n");
        return 1;
    }

    begin = strstr(text, "case WEBSOCKET_EVENT_ERROR:");
    end = begin != NULL ? strstr(begin, "default:") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate WebSocket error callback section\n");
        free(text);
        return 1;
    }
    *end = '\0';

    if (strstr(begin, "ws_mark_session_unavailable_from_callback();") == NULL) {
        fprintf(stderr, "FAIL: error callback must use the lock-free session invalidation path\n");
        failures++;
    }
    if (strstr(begin, "ws_reset_session_state();") != NULL || strstr(begin, "xSemaphoreTake(") != NULL) {
        fprintf(stderr, "FAIL: error callback must not reset media or acquire a mutex\n");
        failures++;
    }

    free(text);
    return failures;
}

static int expect_disconnect_callback_defers_cleanup(const char *root) {
    char path[1024];
    char *text;
    char *begin;
    char *end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client.c\n");
        return 1;
    }

    begin = strstr(text, "case WEBSOCKET_EVENT_DISCONNECTED:");
    end = begin != NULL ? strstr(begin, "case WEBSOCKET_EVENT_DATA:") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate WebSocket disconnected callback section\n");
        free(text);
        return 1;
    }
    *end = '\0';

    if (strstr(begin, "ws_mark_session_unavailable_from_callback();") == NULL ||
        strstr(begin, "s_ws_deferred_cleanup_pending = true;") == NULL) {
        fprintf(stderr, "FAIL: disconnected callback must only mark unavailable and defer cleanup\n");
        failures++;
    }
    if (strstr(begin, "ws_reset_session_state();") != NULL ||
        strstr(begin, "ws_abort_tts_playback_internal(") != NULL || strstr(begin, "xSemaphoreTake(") != NULL) {
        fprintf(stderr, "FAIL: disconnected callback must not reset media, abort TTS, or acquire a mutex\n");
        failures++;
    }

    free(text);

    snprintf(path, sizeof(path), "%s/app_main.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read app_main.c\n");
        return failures + 1;
    }
    if (strstr(text, "ws_client_process_deferred_cleanup();") == NULL) {
        fprintf(stderr, "FAIL: transport coordinator must process deferred WebSocket cleanup\n");
        failures++;
    }
    free(text);
    return failures;
}

static int expect_recorder_has_bounded_upload_abort(const char *root) {
    char path[1024];
    char *text;
    char *abort_begin;
    char *abort_end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/services/voice_service/src/voice_fsm.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read voice_fsm.c\n");
        return 1;
    }
    if (strstr(text, "voice_upload_guard_observe(&g_upload_guard") == NULL) {
        fprintf(stderr, "FAIL: recorder must evaluate upload health every frame\n");
        failures++;
    }
    if (strstr(text, "abort_recording_after_upload_failure(upload_result);") == NULL) {
        fprintf(stderr, "FAIL: recorder must converge failed uploads to a safe idle state\n");
        failures++;
    }
    if (strstr(text, "(void)voice_remote_publish_desired(false);") == NULL) {
        fprintf(stderr, "FAIL: recorder abort must clear the remote recording target\n");
        failures++;
    }
    abort_begin = strstr(text, "static void abort_recording_after_upload_failure(");
    abort_end = abort_begin != NULL ? strstr(abort_begin, "void voice_recorder_suspend_cloud_audio(") : NULL;
    if (abort_begin == NULL || abort_end == NULL) {
        fprintf(stderr, "FAIL: could not locate recorder upload-abort section\n");
        failures++;
    } else {
        *abort_end = '\0';
        if (strstr(abort_begin, "voice_transport_cancel_audio()") == NULL) {
            fprintf(stderr, "FAIL: recorder abort must enqueue an audio CANCEL terminal when transport is ready\n");
            failures++;
        }
        if (strstr(abort_begin, "voice_transport_send_audio_end()") != NULL) {
            fprintf(stderr, "FAIL: recorder abort must not submit partial audio as a normal LAST frame\n");
            failures++;
        }
        if (strstr(abort_begin, "schedule_upload_error_recovery();") == NULL) {
            fprintf(stderr, "FAIL: recorder abort must schedule recovery from the transient error UI\n");
            failures++;
        }
    }
    if (strstr(text, "behavior_state_cancel();") == NULL) {
        fprintf(stderr, "FAIL: upload error recovery must return behavior UI to its default state\n");
        failures++;
    }
    free(text);
    return failures;
}

static int expect_audio_uplink_tolerates_transient_network_stalls(const char *root) {
    char path[1024];
    char *text;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client.c\n");
        return 1;
    }
    if (strstr(text, "#define WS_AUDIO_SEND_TIMEOUT_MS 4000") == NULL) {
        fprintf(stderr, "FAIL: audio upload must tolerate observed multi-second TCP retransmission stalls\n");
        failures++;
    }
    if (strstr(text, "frame_type == WS_FRAME_TYPE_AUDIO ? WS_AUDIO_SEND_TIMEOUT_MS : WS_SEND_TIMEOUT_MS") == NULL) {
        fprintf(stderr, "FAIL: binary audio packets must select the dedicated short send deadline\n");
        failures++;
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/Kconfig", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client Kconfig\n");
        return failures + 1;
    }
    if (strstr(text, "default 128") == NULL || strstr(text, "range 8 128") == NULL) {
        fprintf(stderr, "FAIL: audio upload queue must provide about 7.7s of PSRAM-backed headroom\n");
        failures++;
    }
    free(text);
    return failures;
}

static int expect_tts_downlink_is_buffered_without_callback_lock_reentry(const char *root) {
    char path[1024];
    char *text;
    char *begin;
    char *end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client.c\n");
        return 1;
    }
    if (strstr(text, "#define WS_BUFFER_SIZE 8192") == NULL ||
        strstr(text, "WS_TTS_WIRE_FRAME_BYTES <= WS_BUFFER_SIZE") == NULL) {
        fprintf(stderr, "FAIL: one complete 4096-byte TTS payload plus protocol header must fit the WS RX buffer\n");
        failures++;
    }
    if (strstr(text, "ws_tts_buffer_policy_should_finish(s_tts_end_pending, pending,") == NULL ||
        strstr(text, "hal_audio_drain_playback(WS_TTS_DMA_DRAIN_TIMEOUT_MS)") == NULL ||
        strstr(text, "TTS playout event=dma_drained") == NULL ||
        strstr(text, "TTS playout event=speaking_released") == NULL) {
        fprintf(stderr, "FAIL: TTS completion must wait for worker ownership and hardware DMA drain\n");
        failures++;
    }
    begin = strstr(text, "static void ws_tts_worker_task(void *arg)");
    end = begin != NULL ? strstr(begin, "static void ws_write_u32_le(") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate TTS playback worker section\n");
        failures++;
    } else {
        char saved = *end;
        *end = '\0';
        if (strstr(begin, "ws_maybe_send_tts_buffer_status(\"buffering\")") == NULL) {
            fprintf(stderr, "FAIL: TTS worker must report credit while waiting for the start buffer\n");
            failures++;
        }
        *end = saved;
    }
    begin = strstr(text, "void ws_handle_tts_binary(");
    end = begin != NULL ? strstr(begin, "void ws_tts_complete(") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate TTS receive callback section\n");
        failures++;
    } else {
        *end = '\0';
        if (strstr(begin, "ws_maybe_send_tts_buffer_status(\"enqueue\")") != NULL ||
            strstr(begin, "ws_send_tts_buffer_status(") != NULL) {
            fprintf(stderr, "FAIL: TTS receive callback must not re-enter the websocket send lock\n");
            failures++;
        }
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/Kconfig", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client Kconfig\n");
        return failures + 1;
    }
    begin = strstr(text, "config WATCHER_WS_TTS_START_BUFFER_FRAMES");
    end = begin != NULL ? strstr(begin, "config WATCHER_WS_TTS_STARVATION_WARN_MS") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate TTS buffering configuration\n");
        failures++;
    } else {
        *end = '\0';
        if (strstr(begin, "default 16") == NULL || strstr(begin, "default 12") == NULL) {
            fprintf(stderr, "FAIL: TTS buffering must cover the observed hotspot jitter before playback/resume\n");
            failures++;
        }
    }
    free(text);
    return failures;
}

static int expect_ai_status_cannot_preempt_active_speaking(const char *root) {
    char path[1024];
    char *text;
    char *begin;
    char *end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        return 1;
    }
    begin = strstr(text, "void ws_tts_complete(void)");
    end = begin != NULL ? strstr(begin, "void ws_tts_timeout_check(void)") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate TTS EOS callback\n");
        failures++;
    } else {
        *end = '\0';
        if (strstr(begin, "s_tts_end_pending = true") == NULL || strstr(begin, "ws_finish_tts_playback(") != NULL) {
            fprintf(stderr, "FAIL: network EOS must only arm worker-owned completion\n");
            failures++;
        }
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL || strstr(text, "ws_event_ui_should_apply_tts_completion(completion_state") == NULL ||
        strstr(text, "foreground_lease_active") == NULL ||
        strstr(text, "Skipping TTS completion presentation") == NULL ||
        strstr(text, "TTS playout event=speaking_released") == NULL) {
        fprintf(stderr,
                "FAIL: TTS completion must preserve foreground handoff and skip unowned success presentation\n");
        failures++;
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_handlers.c", root);
    text = read_text(path);
    if (text == NULL || strstr(text, "req.defer_ui_until_tts_complete") == NULL ||
        strstr(text, "ws_client_is_tts_playing()") == NULL) {
        fprintf(stderr, "FAIL: AI statuses must defer visual replacement while TTS is playing\n");
        failures++;
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/services/control_ingress/src/control_ingress.c", root);
    text = read_text(path);
    if (text == NULL || strstr(text, "if (req->defer_ui_until_tts_complete)") == NULL ||
        strstr(text, "Deferred AI status UI until TTS playout completes") == NULL) {
        fprintf(stderr, "FAIL: deferred AI status must update completion state without replacing speaking\n");
        failures++;
    }
    free(text);
    return failures;
}

static int expect_opus_encoder_has_safe_stack_budget(const char *root) {
    char path[1024];
    char *text;
    char *begin;
    char *end;
    int failures = 0;

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read ws_client.c\n");
        return 1;
    }
    if (strstr(text, "#define WS_AUDIO_WORKER_STACK HAL_OPUS_MIN_TASK_STACK_BYTES") == NULL ||
        strstr(text, "xTaskCreateWithCaps(ws_audio_worker_task") == NULL ||
        strstr(text, "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") == NULL ||
        strstr(text, "audio worker created: stack=%u memory=%s") == NULL) {
        fprintf(stderr, "FAIL: Opus worker needs a 40KB PSRAM-backed stack\n");
        failures++;
    }
    begin = strstr(text, "static void ws_audio_worker_task(");
    begin = begin != NULL ? strstr(begin + 1, "static void ws_audio_worker_task(") : NULL;
    end = begin != NULL ? strstr(begin, "static bool ws_wait_for_audio_worker_exit(") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate audio worker section\n");
        failures++;
    } else {
        *end = '\0';
        if (strstr(begin, "hal_opus_encode(") == NULL) {
            fprintf(stderr, "FAIL: Opus encoding must run in the PSRAM audio worker\n");
            failures++;
        }
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/protocols/ws_client/src/ws_client.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not reread ws_client.c\n");
        return failures + 1;
    }
    begin = strstr(text, "int ws_send_audio(");
    end = begin != NULL ? strstr(begin, "int ws_send_audio_end(") : NULL;
    if (begin == NULL || end == NULL) {
        fprintf(stderr, "FAIL: could not locate audio producer section\n");
        failures++;
    } else {
        *end = '\0';
        if (strstr(begin, "hal_opus_encode(") != NULL || strstr(begin, "memcpy(slot->data, data") == NULL) {
            fprintf(stderr, "FAIL: voice producer must only enqueue PCM and never run Opus inline\n");
            failures++;
        }
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/hal/hal_audio/src/hal_opus.c", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read hal_opus.c\n");
        return failures + 1;
    }
    if (strstr(text, "HAL_OPUS_STACK_WARN_BYTES") == NULL || strstr(text, "Opus encode stack watermark") == NULL) {
        fprintf(stderr, "FAIL: Opus encoding must report its observed stack safety margin\n");
        failures++;
    }
    free(text);

    snprintf(path, sizeof(path), "%s/../components/hal/hal_audio/include/hal_opus.h", root);
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "FAIL: could not read hal_opus.h\n");
        return failures + 1;
    }
    if (strstr(text, "HAL_OPUS_MIN_TASK_STACK_BYTES (40U * 1024U)") == NULL) {
        fprintf(stderr, "FAIL: Opus stack contract must remain at least 40KB\n");
        failures++;
    }
    free(text);
    return failures;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <main-component-dir>\n", argv[0]);
        return 2;
    }
    return (expect_error_callback_is_lock_free(argv[1]) + expect_disconnect_callback_defers_cleanup(argv[1]) +
            expect_recorder_has_bounded_upload_abort(argv[1]) +
            expect_audio_uplink_tolerates_transient_network_stalls(argv[1]) +
            expect_tts_downlink_is_buffered_without_callback_lock_reentry(argv[1]) +
            expect_ai_status_cannot_preempt_active_speaking(argv[1]) +
            expect_opus_encoder_has_safe_stack_budget(argv[1])) == 0
               ? 0
               : 1;
}
