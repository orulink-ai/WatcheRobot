/**
 * @file anim_storage.h
 * @brief SD-backed animation catalog and animpack streaming helpers.
 */

#ifndef ANIM_STORAGE_H
#define ANIM_STORAGE_H

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef CONFIG_WATCHER_ANIM_RING_FRAMES
#define WATCHER_ANIM_RING_FRAMES CONFIG_WATCHER_ANIM_RING_FRAMES
#else
#define WATCHER_ANIM_RING_FRAMES 3
#endif

#define ANIM_MAX_PATH_LEN 96
#define ANIM_STORAGE_ROOT "/sdcard/anim"
#define ANIM_MANIFEST_PATH ANIM_STORAGE_ROOT "/anim_manifest.bin"
#define ANIM_MANIFEST_FALLBACK_PATH "/sdcard/anim_manifest.bin"
#define ANIM_FRAME_FLAG_INDEXED8 0x0001U

#ifdef CONFIG_WATCHER_ANIM_SWITCH_FADE_MS
#define WATCHER_ANIM_SWITCH_FADE_MS CONFIG_WATCHER_ANIM_SWITCH_FADE_MS
#else
#define WATCHER_ANIM_SWITCH_FADE_MS 140
#endif

typedef enum {
    EMOJI_ANIM_BOOT = 0,
    EMOJI_ANIM_HAPPY,
    EMOJI_ANIM_ERROR,
    EMOJI_ANIM_BLUETOOTH,
    EMOJI_ANIM_SPEAKING,
    EMOJI_ANIM_LISTENING,
    EMOJI_ANIM_PROCESSING,
    EMOJI_ANIM_STANDBY,
    EMOJI_ANIM_THINKING,
    EMOJI_ANIM_CUSTOM_1,
    EMOJI_ANIM_CUSTOM_2,
    EMOJI_ANIM_CUSTOM_3,
    EMOJI_ANIM_STANDBY_1,
    EMOJI_ANIM_STANDBY_2,
    EMOJI_ANIM_STANDBY_3,
    EMOJI_ANIM_STANDBY_4,
    EMOJI_ANIM_DISCONNECT,
    EMOJI_ANIM_SHOCK,
    EMOJI_ANIM_SUNGLASSES,
    EMOJI_ANIM_SAD,
    EMOJI_ANIM_GET,
    EMOJI_ANIM_SMILE,
    EMOJI_ANIM_RECHARGE,
    EMOJI_ANIM_SPEECHLESS,
    EMOJI_ANIM_CONCENTRATION,
    EMOJI_ANIM_FONDLE_LOVE,
    EMOJI_ANIM_FONDLE_ANGER,
    EMOJI_ANIM_BLINK,
    EMOJI_ANIM_UPGRADE,
    EMOJI_ANIM_COUNT,
    EMOJI_ANIM_NONE = -1
} emoji_anim_type_t;

typedef struct {
    bool available;
    emoji_anim_type_t type;
    char name[24];
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    bool loop;
    uint16_t frame_count;
    char pack_path[ANIM_MAX_PATH_LEN];
} anim_catalog_type_info_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint16_t delay_ms;
    uint16_t flags;
} anim_pack_frame_desc_t;

typedef struct {
    uint8_t *img_data;
    size_t data_size;
    int width;
    int height;
    lv_img_cf_t color_format;
    lv_img_dsc_t img_dsc;
} anim_frame_buffer_t;

typedef struct {
    FILE *file;
    anim_catalog_type_info_t info;
    anim_pack_frame_desc_t *frames;
    uint8_t *scratch_data;
    size_t scratch_size;
    uint16_t frame_count;
    uint32_t payload_offset;
    uint32_t frame_data_size;
    uint32_t file_pos;
    bool file_pos_valid;
} anim_stream_t;

typedef void (*emoji_progress_cb_t)(emoji_anim_type_t type, int types_done, int types_total);

int anim_catalog_init(void);
const anim_catalog_type_info_t *anim_catalog_get_type_info(emoji_anim_type_t type);
bool anim_catalog_has_type(emoji_anim_type_t type);
int emoji_load_type(emoji_anim_type_t type);
int emoji_get_frame_count(emoji_anim_type_t type);
int emoji_get_loop_duration_ms(emoji_anim_type_t type);
const char *emoji_type_name(emoji_anim_type_t type);

int anim_frame_buffer_init(anim_frame_buffer_t *buffer, uint16_t width, uint16_t height);
void anim_frame_buffer_free(anim_frame_buffer_t *buffer);

int anim_stream_open(emoji_anim_type_t type, anim_stream_t *out_stream);
void anim_stream_close(anim_stream_t *stream);
int anim_stream_read_frame(anim_stream_t *stream, int frame_index, anim_frame_buffer_t *buffer);
int anim_stream_get_frame_delay_ms(const anim_stream_t *stream, int frame_index);

int anim_load_static_frame(emoji_anim_type_t type, int frame_index, anim_frame_buffer_t *buffer);
lv_img_dsc_t *emoji_get_image(emoji_anim_type_t type, int frame);

int emoji_load_all_images(void);
int emoji_load_all_images_with_cb(emoji_progress_cb_t cb);
void emoji_free_all(void);
bool emoji_images_loaded(void);
int emoji_spiffs_init(void);

#endif /* ANIM_STORAGE_H */
