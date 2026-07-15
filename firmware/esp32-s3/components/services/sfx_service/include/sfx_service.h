#ifndef SFX_SERVICE_H
#define SFX_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sfx_service_init(void);
esp_err_t sfx_service_reload(void);
esp_err_t sfx_service_deinit(void);
esp_err_t sfx_service_play(const char *sound_id);
esp_err_t sfx_service_play_delayed(const char *sound_id, int delay_ms);
void sfx_service_stop(void);
bool sfx_service_is_busy(void);
void sfx_service_set_cloud_audio_busy(bool busy);
bool sfx_service_is_cloud_audio_busy(void);
void sfx_service_set_voice_audio_busy(bool busy);
bool sfx_service_is_voice_audio_busy(void);
size_t sfx_service_stack_high_watermark(void);
size_t sfx_service_stack_size(void);

#ifdef __cplusplus
}
#endif

#endif /* SFX_SERVICE_H */
