#include "hal_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sensecap-watcher.h"

#define TAG "HAL_AUDIO"

/* Default sample rates */
#define SAMPLE_RATE_RECORD 16000 /* ASR expects 16kHz */
#define SAMPLE_RATE_PLAY 24000   /* 火山引擎 TTS uses 24kHz */

static bool codec_initialized = false;                    /* codec init is global, only once */
static bool is_running = false;                           /* current running state */
static uint32_t current_sample_rate = SAMPLE_RATE_RECORD; /* current sample rate */
static esp_codec_dev_handle_t mic_handle = NULL;
static esp_codec_dev_handle_t speaker_handle = NULL;
static uint32_t consecutive_read_failures = 0;

static void hal_audio_prepare_output(void) {
    esp_err_t ret;

    (void)bsp_io_expander_init();

    ret = bsp_exp_io_set_level(BSP_PWR_CODEC_PA, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable codec PA power: %s", esp_err_to_name(ret));
    }

    ret = bsp_codec_mute_set(false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unmute codec output: %s", esp_err_to_name(ret));
    }

    ret = bsp_codec_volume_set(CONFIG_WATCHER_AUDIO_VOLUME, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set codec output volume: %s", esp_err_to_name(ret));
    }
}

/* Initialize codec once at system startup */
int hal_audio_init(void) {
    uint32_t initial_sample_rate = current_sample_rate;

    if (codec_initialized) {
        return 0;
    }

    ESP_LOGI(TAG, "Initializing audio codec via SDK...");

    esp_err_t ret = bsp_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init codec: %s", esp_err_to_name(ret));
        return -1;
    }

    /* Get microphone and speaker handles */
    mic_handle = bsp_codec_microphone_get();
    speaker_handle = bsp_codec_speaker_get();

    if (!mic_handle) {
        ESP_LOGE(TAG, "Failed to get microphone handle");
        return -1;
    }

    if (!speaker_handle) {
        ESP_LOGE(TAG, "Failed to get speaker handle");
        return -1;
    }

    codec_initialized = true;
    is_running = true; /* Keep codec running always */

    bsp_codec_set_fs(initial_sample_rate, 16, 1);
    current_sample_rate = initial_sample_rate;
    hal_audio_prepare_output();

    ESP_LOGI(TAG, "Audio codec initialized (%luHz, volume=%d)", current_sample_rate, CONFIG_WATCHER_AUDIO_VOLUME);
    return 0;
}

/* Track audio mode: recording (input) or playback (output) */
static bool is_playback_mode = false;

/* Set sample rate for playback (call before TTS playback) */
void hal_audio_set_sample_rate(uint32_t sample_rate) {
    if (!codec_initialized) {
        current_sample_rate = sample_rate;
        return;
    }

    if (current_sample_rate == sample_rate) {
        return; /* No change needed */
    }

    ESP_LOGI(TAG, "Switching sample rate: %lu -> %lu (playback_mode=%d)", current_sample_rate, sample_rate,
             is_playback_mode);

    /* For playback mode, we don't need to stop input channel (which may not be enabled).
     * Just set the new sample rate directly. */
    if (is_playback_mode) {
        /* Set new sample rate (16-bit, mono channel) */
        bsp_codec_set_fs(sample_rate, 16, 1);
        current_sample_rate = sample_rate;
        ESP_LOGI(TAG, "Sample rate switch complete (playback mode, no stop needed)");
        return;
    }

    /* For recording mode (input only), we need to reconfigure I2S.
     * Use bsp_codec_set_fs which handles I2S reconfiguration properly. */
    bsp_codec_set_fs(sample_rate, 16, 1);
    current_sample_rate = sample_rate;

    ESP_LOGI(TAG, "Sample rate switch complete");
}

/* Mark audio as being used for playback (not just recording) */
void hal_audio_set_playback_mode(bool enable) {
    is_playback_mode = enable;
    ESP_LOGI(TAG, "Audio mode: %s", enable ? "playback" : "recording");
}

bool hal_audio_is_running(void) {
    return is_running;
}

bool hal_audio_is_playback_mode(void) {
    return is_playback_mode;
}

int hal_audio_start(void) {
    /* Ensure codec is initialized */
    if (!codec_initialized) {
        if (hal_audio_init() != 0) {
            return -1;
        }
    }

    if (is_running) {
        consecutive_read_failures = 0;
        if (is_playback_mode) {
            hal_audio_prepare_output();
        }
        ESP_LOGD(TAG, "Audio already running (sample rate: %lu Hz, playback=%d)", current_sample_rate,
                 is_playback_mode);
        return 0;
    }

    esp_err_t ret = bsp_codec_set_fs(current_sample_rate, 16, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio path at %lu Hz: %s", current_sample_rate, esp_err_to_name(ret));
        return -1;
    }

    is_running = true;
    consecutive_read_failures = 0;
    if (is_playback_mode) {
        hal_audio_prepare_output();
    }
    ESP_LOGI(TAG, "Audio started (sample rate: %lu Hz)", current_sample_rate);
    return 0;
}

int hal_audio_read(uint8_t *out_buf, int max_len) {
    if (!mic_handle) {
        return -1;
    }

    /* In wake word mode, audio should always be available */
    /* If is_running is false, return 0 (no data) instead of -1 (error) to avoid error spam */
    if (!is_running) {
#ifdef CONFIG_ENABLE_WAKE_WORD
        return 0; /* In wake word mode, gracefully handle temporary unavailability */
#else
        return -1;
#endif
    }

    size_t bytes_read = 0;
    esp_err_t ret = bsp_i2s_read(out_buf, max_len, &bytes_read, 100);

    if (ret != ESP_OK) {
#ifdef CONFIG_ENABLE_WAKE_WORD
        consecutive_read_failures++;
        if (consecutive_read_failures == 1U || (consecutive_read_failures % 50U) == 0U) {
            ESP_LOGW(TAG, "Read temporarily unavailable: %s (count=%lu running=%d playback=%d rate=%lu)",
                     esp_err_to_name(ret), (unsigned long)consecutive_read_failures, is_running, is_playback_mode,
                     current_sample_rate);
        }
        return 0;
#else
        ESP_LOGE(TAG, "Read error: %s", esp_err_to_name(ret));
        return -1;
#endif
    }

    consecutive_read_failures = 0;
    return (int)bytes_read;
}

int hal_audio_write(const uint8_t *data, int len) {
    if (!is_running || !speaker_handle) {
        ESP_LOGW(TAG, "Write blocked: is_running=%d, speaker_handle=%p", is_running, speaker_handle);
        return -1;
    }

    size_t bytes_written = 0;
    /* Use ESP_LOGD for high-frequency audio writes to avoid UART bottleneck */
    ESP_LOGD(TAG, "Writing %d bytes to speaker...", len);
    esp_err_t ret = bsp_i2s_write((void *)data, len, &bytes_written, 100);
    ESP_LOGD(TAG, "Write result: ret=%d, written=%d", ret, (int)bytes_written);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write error: %s", esp_err_to_name(ret));
        return -1;
    }

    return (int)bytes_written;
}

int hal_audio_stop(void) {
    if (!is_running) {
        return 0;
    }

#ifdef CONFIG_ENABLE_WAKE_WORD
    /* Wake word detection needs a continuous 16 kHz microphone stream.
     * Playback users call stop after local SFX/TTS; in wake mode, restore
     * the shared codec path to recording instead of disabling it. */
    if (is_playback_mode) {
        is_playback_mode = false;
    }
    current_sample_rate = SAMPLE_RATE_RECORD;
    esp_err_t ret = bsp_codec_set_fs(current_sample_rate, 16, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore wake audio path: %s", esp_err_to_name(ret));
        is_running = false;
        return -1;
    }
    consecutive_read_failures = 0;
    ESP_LOGI(TAG, "Audio stop requested; wake word path restored at %lu Hz", current_sample_rate);
    return 0;
#else
    /* No wake-word detector needs a continuous microphone stream. Treat stop
     * after SFX/TTS as an idle audio path, not as a recording restore. */
    is_playback_mode = false;
    is_running = false;
    current_sample_rate = SAMPLE_RATE_RECORD;
    consecutive_read_failures = 0;
    ESP_LOGI(TAG, "Audio stopped (codec stays initialized, mode=idle)");
    return 0;
#endif
}
