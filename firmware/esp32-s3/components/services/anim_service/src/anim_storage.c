/**
 * @file anim_storage.c
 * @brief SD-backed animpack catalog and frame loading helpers.
 */

#include "anim_storage.h"
#include "anim_fault_injection.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ANIM_STORAGE"
#define ANIM_MANIFEST_VERSION 2U
#define ANIM_PACK_VERSION 2U
#define ANIM_MAX_LOOP_DURATION_MS (60 * 60 * 1000)
#define ANIM_FRAME_KNOWN_FLAGS ANIM_FRAME_FLAG_INDEXED8
#define ANIM_FRAME_OUTPUT_SCALE 1U

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t type_count;
} anim_manifest_header_t;

typedef struct __attribute__((packed)) {
    uint16_t type_id;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t frame_count;
    uint8_t loop;
    uint8_t reserved[3];
    char name[24];
    char pack_path[ANIM_MAX_PATH_LEN];
} anim_manifest_entry_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint8_t loop;
    uint8_t reserved;
    uint16_t default_delay_ms;
    uint32_t toc_offset;
    uint32_t payload_offset;
    uint32_t frame_data_size;
} anim_pack_header_t;

static const char *k_manifest_magic = "ANIM";
static const char *k_pack_magic = "ANPK";
static const int k_frame_read_attempts = 3;

static bool g_catalog_initialized = false;
static anim_catalog_type_info_t g_catalog[EMOJI_ANIM_COUNT];
static anim_frame_buffer_t g_legacy_frame = {0};

static uint16_t anim_storage_default_fps(void) {
#ifdef CONFIG_WATCHER_ANIM_FPS
    return (uint16_t)CONFIG_WATCHER_ANIM_FPS;
#else
    return (uint16_t)ANIMATION_REGISTRY_DEFAULT_FPS;
#endif
}

static void reset_catalog(void) {
    memset(g_catalog, 0, sizeof(g_catalog));
    for (int index = 0; index < EMOJI_ANIM_COUNT; ++index) {
        g_catalog[index].type = (emoji_anim_type_t)index;
        strncpy(g_catalog[index].name, animation_registry_name((emoji_anim_type_t)index),
                sizeof(g_catalog[index].name) - 1);
        g_catalog[index].fps = anim_storage_default_fps();
        g_catalog[index].loop = animation_registry_default_loop((emoji_anim_type_t)index);
    }
}

static void normalize_pack_path(const char *src, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == NULL || src[0] == '\0') {
        return;
    }

    if (src[0] == '/') {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    snprintf(dst, dst_size, "%s/%s", ANIM_STORAGE_ROOT, src);
}

static int anim_stream_reopen_file(anim_stream_t *stream) {
    if (stream == NULL || stream->info.pack_path[0] == '\0') {
        return -1;
    }

    if (anim_fault_injection_should_fail(ANIM_FAULT_SD_OPEN)) {
        return -1;
    }
    FILE *reopened = fopen(stream->info.pack_path, "rb");
    if (reopened == NULL) {
        ESP_LOGW(TAG, "Failed to reopen animpack %s: errno=%d (%s)", stream->info.pack_path, errno, strerror(errno));
        return -1;
    }

    if (stream->file != NULL) {
        fclose(stream->file);
    }

    stream->file = reopened;
    stream->file_pos_valid = false;
    return 0;
}

static int load_manifest_from_path(const char *manifest_path) {
    FILE *handle = fopen(manifest_path, "rb");
    if (handle == NULL) {
        ESP_LOGW(TAG, "Failed to open anim manifest %s: errno=%d (%s)", manifest_path, errno, strerror(errno));
        return -1;
    }

    anim_manifest_header_t header = {0};
    if (fread(&header, sizeof(header), 1, handle) != 1) {
        fclose(handle);
        return -1;
    }

    if (memcmp(header.magic, k_manifest_magic, sizeof(header.magic)) != 0 || header.version != ANIM_MANIFEST_VERSION) {
        ESP_LOGW(TAG, "Invalid animation manifest at %s", manifest_path);
        fclose(handle);
        return -1;
    }

    bool loaded_types[EMOJI_ANIM_COUNT] = {false};
    int loaded = 0;
    for (uint16_t index = 0; index < header.type_count; ++index) {
        anim_manifest_entry_t entry = {0};
        if (fread(&entry, sizeof(entry), 1, handle) != 1) {
            break;
        }

        if (entry.type_id >= EMOJI_ANIM_COUNT || entry.frame_count == 0 || entry.width == 0 || entry.height == 0) {
            ESP_LOGW(TAG, "Ignoring invalid animation manifest entry id=%u name=%.*s", (unsigned)entry.type_id,
                     (int)sizeof(entry.name), entry.name);
            continue;
        }

        emoji_anim_type_t type = (emoji_anim_type_t)entry.type_id;
        const char *canonical_name = animation_registry_name(type);
        size_t entry_name_len = strnlen(entry.name, sizeof(entry.name));
        size_t entry_path_len = strnlen(entry.pack_path, sizeof(entry.pack_path));
        if (entry_name_len == sizeof(entry.name) || strcmp(entry.name, canonical_name) != 0) {
            ESP_LOGW(TAG, "Ignoring animation manifest name mismatch id=%u expected=%s actual=%.*s",
                     (unsigned)entry.type_id, canonical_name, (int)sizeof(entry.name), entry.name);
            continue;
        }
        if (entry_path_len == 0 || entry_path_len == sizeof(entry.pack_path)) {
            ESP_LOGW(TAG, "Ignoring invalid animation manifest pack path id=%u name=%s", (unsigned)entry.type_id,
                     canonical_name);
            continue;
        }
        if (loaded_types[entry.type_id]) {
            ESP_LOGW(TAG, "Ignoring duplicate animation manifest entry id=%u name=%s", (unsigned)entry.type_id,
                     canonical_name);
            continue;
        }

        anim_catalog_type_info_t *info = &g_catalog[entry.type_id];
        info->available = true;
        info->width = entry.width;
        info->height = entry.height;
        info->fps = entry.fps > 0 ? entry.fps : anim_storage_default_fps();
        info->loop = entry.loop != 0;
        info->frame_count = entry.frame_count;
        strncpy(info->name, canonical_name, sizeof(info->name) - 1);
        normalize_pack_path(entry.pack_path, info->pack_path, sizeof(info->pack_path));
        if (info->pack_path[0] == '\0') {
            ESP_LOGW(TAG, "Ignoring animation manifest entry without pack path id=%u name=%s", (unsigned)entry.type_id,
                     canonical_name);
            memset(info, 0, sizeof(*info));
            info->type = type;
            strncpy(info->name, canonical_name, sizeof(info->name) - 1);
            continue;
        }
        loaded_types[entry.type_id] = true;
        ++loaded;
    }

    fclose(handle);
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded animpack manifest from %s (%d types)", manifest_path, loaded);
        return 0;
    }
    return -1;
}

static int ensure_manifest_loaded(void) {
    if (g_catalog_initialized) {
        return 0;
    }

    reset_catalog();
    if (load_manifest_from_path(ANIM_MANIFEST_PATH) != 0 && load_manifest_from_path(ANIM_MANIFEST_FALLBACK_PATH) != 0) {
        ESP_LOGW(TAG, "No SD animation manifest available under %s", ANIM_STORAGE_ROOT);
        return -1;
    }

    g_catalog_initialized = true;
    return 0;
}

static int anim_frame_buffer_ensure(anim_frame_buffer_t *buffer, uint16_t width, uint16_t height) {
    size_t expected_size = (size_t)width * (size_t)height * 2U;
    if (buffer == NULL || expected_size == 0) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }

    if (buffer->img_data != NULL && buffer->data_size == expected_size && buffer->width == width &&
        buffer->height == height) {
        return ANIM_STORAGE_OK;
    }

    anim_frame_buffer_free(buffer);
    return anim_frame_buffer_init(buffer, width, height);
}

static uint16_t anim_stream_output_width(const anim_stream_t *stream) {
    return stream != NULL ? (uint16_t)(stream->info.width * ANIM_FRAME_OUTPUT_SCALE) : 0;
}

static uint16_t anim_stream_output_height(const anim_stream_t *stream) {
    return stream != NULL ? (uint16_t)(stream->info.height * ANIM_FRAME_OUTPUT_SCALE) : 0;
}

static int anim_stream_ensure_scratch(anim_stream_t *stream, size_t data_size) {
    if (stream == NULL || data_size == 0) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }

    if (stream->scratch_data != NULL && stream->scratch_size >= data_size) {
        return ANIM_STORAGE_OK;
    }

    if (stream->scratch_data != NULL) {
        heap_caps_free(stream->scratch_data);
        stream->scratch_data = NULL;
        stream->scratch_size = 0;
    }

    if (anim_fault_injection_should_fail(ANIM_FAULT_PSRAM_STREAM_SCRATCH_ALLOC)) {
        return ANIM_STORAGE_NO_MEMORY;
    }
    stream->scratch_data = (uint8_t *)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stream->scratch_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for indexed animation scratch", (unsigned)data_size);
        return ANIM_STORAGE_NO_MEMORY;
    }

    stream->scratch_size = data_size;
    return ANIM_STORAGE_OK;
}

static bool anim_frame_payload_limits(uint16_t width, uint16_t height, size_t *rgb565_out, size_t *indexed8_out) {
    if (width == 0 || height == 0) {
        return false;
    }

    uint64_t pixels = (uint64_t)width * (uint64_t)height;
    uint64_t rgb565_size = pixels * 2ULL;
    uint64_t indexed8_size = sizeof(uint16_t) + (256ULL * 2ULL) + pixels;
    if (rgb565_size > SIZE_MAX || indexed8_size > SIZE_MAX) {
        return false;
    }

    if (rgb565_out != NULL) {
        *rgb565_out = (size_t)rgb565_size;
    }
    if (indexed8_out != NULL) {
        *indexed8_out = (size_t)indexed8_size;
    }
    return true;
}

static bool anim_frame_desc_is_valid(const char *name, uint16_t width, uint16_t height, uint32_t payload_size,
                                     int frame_index, const anim_pack_frame_desc_t *frame) {
    size_t rgb565_size = 0;
    size_t indexed8_max_size = 0;
    const char *safe_name = name != NULL ? name : "<unknown>";

    if (frame == NULL || !anim_frame_payload_limits(width, height, &rgb565_size, &indexed8_max_size)) {
        ESP_LOGW(TAG, "Invalid animpack geometry for %s: %ux%u", safe_name, (unsigned)width, (unsigned)height);
        return false;
    }

    bool indexed8 = (frame->flags & ANIM_FRAME_FLAG_INDEXED8) != 0;
    if ((frame->flags & ~ANIM_FRAME_KNOWN_FLAGS) != 0) {
        ESP_LOGW(TAG, "Unsupported frame flags for %s frame %d: 0x%04x", safe_name, frame_index, frame->flags);
        return false;
    }
    if (frame->size == 0) {
        ESP_LOGW(TAG, "Invalid empty frame for %s frame %d", safe_name, frame_index);
        return false;
    }
    if (frame->offset > payload_size || frame->size > payload_size - frame->offset) {
        ESP_LOGW(TAG, "Frame range outside payload for %s frame %d: offset=%u size=%u payload=%u", safe_name,
                 frame_index, (unsigned)frame->offset, (unsigned)frame->size, (unsigned)payload_size);
        return false;
    }
    if (!indexed8 && frame->size != rgb565_size) {
        ESP_LOGW(TAG, "Invalid RGB565 frame size for %s frame %d: size=%u expected=%u", safe_name, frame_index,
                 (unsigned)frame->size, (unsigned)rgb565_size);
        return false;
    }
    if (indexed8 && (frame->size < sizeof(uint16_t) || frame->size > indexed8_max_size)) {
        ESP_LOGW(TAG, "Invalid indexed frame size for %s frame %d: size=%u max=%u", safe_name, frame_index,
                 (unsigned)frame->size, (unsigned)indexed8_max_size);
        return false;
    }

    return true;
}

static bool anim_frame_table_is_valid(const char *name, const anim_pack_header_t *header,
                                      const anim_pack_frame_desc_t *frames, uint32_t payload_size) {
    if (header == NULL || frames == NULL) {
        return false;
    }

    for (uint16_t index = 0; index < header->frame_count; ++index) {
        if (!anim_frame_desc_is_valid(name, header->width, header->height, payload_size, index, &frames[index])) {
            return false;
        }
    }
    return true;
}

static int decode_indexed8_frame(const anim_stream_t *stream, const anim_pack_frame_desc_t *frame,
                                 const uint8_t *payload, anim_frame_buffer_t *buffer) {
    if (stream == NULL || frame == NULL || payload == NULL || buffer == NULL || frame->size < sizeof(uint16_t)) {
        return -1;
    }

    uint16_t palette_count = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    size_t palette_bytes = (size_t)palette_count * 2U;
    size_t src_width = stream->info.width;
    size_t src_height = stream->info.height;
    size_t pixel_count = src_width * src_height;
    size_t dst_width = src_width * ANIM_FRAME_OUTPUT_SCALE;
    size_t dst_height = src_height * ANIM_FRAME_OUTPUT_SCALE;
    size_t expected_size = sizeof(uint16_t) + palette_bytes + pixel_count;
    if (palette_count == 0 || palette_count > 256 || frame->size != expected_size ||
        buffer->data_size < dst_width * dst_height * 2U) {
        ESP_LOGW(TAG, "Invalid indexed frame %s: colors=%u size=%u expected=%u", stream->info.name,
                 (unsigned)palette_count, (unsigned)frame->size, (unsigned)expected_size);
        return -1;
    }

    const uint8_t *palette = payload + sizeof(uint16_t);
    const uint8_t *indices = palette + palette_bytes;
    uint16_t palette_words[256];
    for (uint16_t index = 0; index < palette_count; ++index) {
        const uint8_t *color = palette + ((size_t)index * 2U);
        palette_words[index] = (uint16_t)color[0] | ((uint16_t)color[1] << 8);
    }

    uint16_t *dst = (uint16_t *)buffer->img_data;
    for (size_t y = 0; y < src_height; ++y) {
        const uint8_t *src_row = indices + (y * src_width);
        for (size_t x = 0; x < src_width; ++x) {
            uint8_t index = src_row[x];
            if (index >= palette_count) {
                ESP_LOGW(TAG, "Invalid indexed frame %s: pixel=%u index=%u colors=%u", stream->info.name,
                         (unsigned)(y * src_width + x), (unsigned)index, (unsigned)palette_count);
                return -1;
            }
            uint16_t color = palette_words[index];
            for (size_t scale_y = 0; scale_y < ANIM_FRAME_OUTPUT_SCALE; ++scale_y) {
                uint16_t *dst_row = dst + ((y * ANIM_FRAME_OUTPUT_SCALE + scale_y) * dst_width);
                size_t dst_x = x * ANIM_FRAME_OUTPUT_SCALE;
                for (size_t scale_x = 0; scale_x < ANIM_FRAME_OUTPUT_SCALE; ++scale_x) {
                    dst_row[dst_x + scale_x] = color;
                }
            }
        }
    }

    return 0;
}

static int decode_rgb565_scaled_frame(const anim_stream_t *stream, const anim_pack_frame_desc_t *frame,
                                      const uint8_t *payload, anim_frame_buffer_t *buffer) {
    if (stream == NULL || frame == NULL || payload == NULL || buffer == NULL) {
        return -1;
    }

    size_t src_width = stream->info.width;
    size_t src_height = stream->info.height;
    size_t src_size = src_width * src_height * 2U;
    size_t dst_width = src_width * ANIM_FRAME_OUTPUT_SCALE;
    size_t dst_height = src_height * ANIM_FRAME_OUTPUT_SCALE;
    if (frame->size != src_size || buffer->data_size < dst_width * dst_height * 2U) {
        ESP_LOGW(TAG, "Invalid RGB565 frame %s: size=%u expected=%u", stream->info.name, (unsigned)frame->size,
                 (unsigned)src_size);
        return -1;
    }

    const uint16_t *src = (const uint16_t *)payload;
    uint16_t *dst = (uint16_t *)buffer->img_data;
    for (size_t y = 0; y < src_height; ++y) {
        const uint16_t *src_row = src + (y * src_width);
        for (size_t x = 0; x < src_width; ++x) {
            uint16_t color = src_row[x];
            for (size_t scale_y = 0; scale_y < ANIM_FRAME_OUTPUT_SCALE; ++scale_y) {
                uint16_t *dst_row = dst + ((y * ANIM_FRAME_OUTPUT_SCALE + scale_y) * dst_width);
                size_t dst_x = x * ANIM_FRAME_OUTPUT_SCALE;
                for (size_t scale_x = 0; scale_x < ANIM_FRAME_OUTPUT_SCALE; ++scale_x) {
                    dst_row[dst_x + scale_x] = color;
                }
            }
        }
    }

    return 0;
}

int emoji_spiffs_init(void) {
    /* Legacy compatibility no-op. Animation assets now live on SD card. */
    return 0;
}

int anim_catalog_init(void) {
    return ensure_manifest_loaded();
}

const anim_catalog_type_info_t *anim_catalog_get_type_info(emoji_anim_type_t type) {
    if (ensure_manifest_loaded() != 0) {
        return NULL;
    }
    if (type < 0 || type >= EMOJI_ANIM_COUNT) {
        return NULL;
    }
    return &g_catalog[type];
}

bool anim_catalog_has_type(emoji_anim_type_t type) {
    const anim_catalog_type_info_t *info = anim_catalog_get_type_info(type);
    return info != NULL && info->available && info->pack_path[0] != '\0' && info->frame_count > 0;
}

int emoji_load_type(emoji_anim_type_t type) {
    const anim_catalog_type_info_t *info = anim_catalog_get_type_info(type);
    return info != NULL && info->available ? info->frame_count : -1;
}

int emoji_get_frame_count(emoji_anim_type_t type) {
    const anim_catalog_type_info_t *info = anim_catalog_get_type_info(type);
    if (info == NULL || !info->available) {
        return 0;
    }

    return info->frame_count;
}

int emoji_get_loop_duration_ms(emoji_anim_type_t type) {
    const anim_catalog_type_info_t *info = anim_catalog_get_type_info(type);
    if (info == NULL || !info->available || info->frame_count == 0) {
        return 0;
    }

    anim_stream_t stream = {0};
    int64_t duration_ms = 0;
    if (anim_stream_open(type, &stream) == 0) {
        int frame_count = emoji_get_frame_count(type);
        if (frame_count <= 0 || frame_count > stream.frame_count) {
            frame_count = stream.frame_count;
        }
        for (int frame = 0; frame < frame_count; ++frame) {
            int delay_ms = anim_stream_get_frame_delay_ms(&stream, frame);
            if (delay_ms > 0) {
                duration_ms += delay_ms;
                if (duration_ms > ANIM_MAX_LOOP_DURATION_MS) {
                    ESP_LOGW(TAG, "Loop duration for %s exceeds cap; clamping to %dms", info->name,
                             ANIM_MAX_LOOP_DURATION_MS);
                    duration_ms = ANIM_MAX_LOOP_DURATION_MS;
                    break;
                }
            }
        }
        anim_stream_close(&stream);
        if (duration_ms > 0) {
            return (int)duration_ms;
        }
    }

    int fps = info->fps > 0 ? info->fps : anim_storage_default_fps();
    if (fps <= 0) {
        return 0;
    }
    int64_t fallback_duration_ms = ((int64_t)emoji_get_frame_count(type) * 1000LL) / fps;
    if (fallback_duration_ms <= 0) {
        return 0;
    }
    if (fallback_duration_ms > ANIM_MAX_LOOP_DURATION_MS) {
        ESP_LOGW(TAG, "Fallback loop duration for %s exceeds cap; clamping to %dms", info->name,
                 ANIM_MAX_LOOP_DURATION_MS);
        return ANIM_MAX_LOOP_DURATION_MS;
    }
    if (fallback_duration_ms > INT_MAX) {
        return INT_MAX;
    }
    return (int)fallback_duration_ms;
}

const char *emoji_type_name(emoji_anim_type_t type) {
    return animation_registry_name(type);
}

int anim_frame_buffer_init(anim_frame_buffer_t *buffer, uint16_t width, uint16_t height) {
    if (buffer == NULL || width == 0 || height == 0) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }

    size_t data_size = (size_t)width * (size_t)height * 2U;
    if (anim_fault_injection_should_fail(ANIM_FAULT_PSRAM_FRAME_BUFFER_ALLOC)) {
        return ANIM_STORAGE_NO_MEMORY;
    }
    uint8_t *img_data = (uint8_t *)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (img_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for animation frame buffer", (unsigned)data_size);
        return ANIM_STORAGE_NO_MEMORY;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->img_data = img_data;
    buffer->data_size = data_size;
    buffer->width = width;
    buffer->height = height;
    buffer->color_format = LV_IMG_CF_TRUE_COLOR;
    buffer->img_dsc.header.always_zero = 0;
    buffer->img_dsc.header.w = width;
    buffer->img_dsc.header.h = height;
    buffer->img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    buffer->img_dsc.data_size = (uint32_t)data_size;
    buffer->img_dsc.data = img_data;
    return ANIM_STORAGE_OK;
}

void anim_frame_buffer_free(anim_frame_buffer_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    if (buffer->img_data != NULL) {
        heap_caps_free(buffer->img_data);
    }
    memset(buffer, 0, sizeof(*buffer));
}

int anim_stream_open(emoji_anim_type_t type, anim_stream_t *out_stream) {
    const anim_catalog_type_info_t *catalog_info = anim_catalog_get_type_info(type);
    if (catalog_info == NULL || !catalog_info->available || catalog_info->pack_path[0] == '\0' || out_stream == NULL) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }

    if (anim_fault_injection_should_fail(ANIM_FAULT_SD_OPEN)) {
        return ANIM_STORAGE_SD_OPEN_FAILED;
    }
    FILE *handle = fopen(catalog_info->pack_path, "rb");
    if (handle == NULL) {
        ESP_LOGW(TAG, "Failed to open animpack: %s", catalog_info->pack_path);
        return ANIM_STORAGE_SD_OPEN_FAILED;
    }
    anim_pack_header_t header = {0};
    if (anim_fault_injection_should_fail(ANIM_FAULT_SD_HEADER_READ) || fread(&header, sizeof(header), 1, handle) != 1) {
        ESP_LOGW(TAG, "Failed to read animpack header: %s", catalog_info->pack_path);
        fclose(handle);
        return ANIM_STORAGE_SD_READ_FAILED;
    }

    if (memcmp(header.magic, k_pack_magic, sizeof(header.magic)) != 0 || header.version != ANIM_PACK_VERSION) {
        ESP_LOGW(TAG, "Invalid animpack header: %s", catalog_info->pack_path);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }
    if (header.width == 0 || header.height == 0 || header.frame_count == 0 || header.frame_data_size == 0) {
        ESP_LOGW(TAG, "Invalid animpack metadata: %s width=%u height=%u frames=%u payload=%u", catalog_info->pack_path,
                 (unsigned)header.width, (unsigned)header.height, (unsigned)header.frame_count,
                 (unsigned)header.frame_data_size);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }
    if (header.width != catalog_info->width || header.height != catalog_info->height ||
        header.frame_count != catalog_info->frame_count || (header.loop != 0) != catalog_info->loop) {
        ESP_LOGW(TAG, "Animpack metadata does not match manifest: %s manifest=%ux%u/%u/loop=%d pack=%ux%u/%u/loop=%d",
                 catalog_info->pack_path, (unsigned)catalog_info->width, (unsigned)catalog_info->height,
                 (unsigned)catalog_info->frame_count, catalog_info->loop ? 1 : 0, (unsigned)header.width,
                 (unsigned)header.height, (unsigned)header.frame_count, header.loop != 0 ? 1 : 0);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }
    size_t table_size = (size_t)header.frame_count * sizeof(anim_pack_frame_desc_t);
    long file_size = 0;
    if (fseek(handle, 0, SEEK_END) != 0 || (file_size = ftell(handle)) < 0) {
        ESP_LOGW(TAG, "Failed to stat animpack: %s errno=%d (%s)", catalog_info->pack_path, errno, strerror(errno));
        fclose(handle);
        return ANIM_STORAGE_SD_READ_FAILED;
    }
    uint64_t toc_end = (uint64_t)header.toc_offset + (uint64_t)table_size;
    if (header.toc_offset < sizeof(header) || toc_end > (uint64_t)header.payload_offset ||
        header.payload_offset > (uint64_t)file_size) {
        ESP_LOGW(TAG, "Invalid animpack bounds: %s toc=%u toc_end=%llu payload=%u file=%ld", catalog_info->pack_path,
                 (unsigned)header.toc_offset, (unsigned long long)toc_end, (unsigned)header.payload_offset, file_size);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }
    uint64_t payload_size64 = (uint64_t)file_size - (uint64_t)header.payload_offset;
    if (payload_size64 > UINT32_MAX) {
        ESP_LOGW(TAG, "Animpack payload too large: %s payload=%llu", catalog_info->pack_path,
                 (unsigned long long)payload_size64);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }
    uint32_t payload_size = (uint32_t)payload_size64;
    anim_pack_frame_desc_t *frames = (anim_pack_frame_desc_t *)malloc(table_size);
    if (frames == NULL) {
        ESP_LOGW(TAG, "Failed to allocate animpack TOC: %s bytes=%u", catalog_info->pack_path, (unsigned)table_size);
        fclose(handle);
        return ANIM_STORAGE_NO_MEMORY;
    }

    if (anim_fault_injection_should_fail(ANIM_FAULT_SD_TOC_READ) ||
        fseek(handle, (long)header.toc_offset, SEEK_SET) != 0 ||
        fread(frames, sizeof(anim_pack_frame_desc_t), header.frame_count, handle) != header.frame_count) {
        ESP_LOGW(TAG, "Failed to read animpack TOC: %s", catalog_info->pack_path);
        free(frames);
        fclose(handle);
        return ANIM_STORAGE_SD_READ_FAILED;
    }

    if (!anim_frame_table_is_valid(catalog_info->name, &header, frames, payload_size)) {
        ESP_LOGW(TAG, "Invalid animpack frame table: %s", catalog_info->pack_path);
        free(frames);
        fclose(handle);
        return ANIM_STORAGE_PACK_CORRUPT;
    }

    memset(out_stream, 0, sizeof(*out_stream));
    out_stream->file = handle;
    out_stream->info = *catalog_info;
    out_stream->info.width = header.width;
    out_stream->info.height = header.height;
    out_stream->info.frame_count = header.frame_count;
    out_stream->info.loop = header.loop != 0;
    out_stream->frames = frames;
    out_stream->frame_count = header.frame_count;
    out_stream->payload_offset = header.payload_offset;
    out_stream->frame_data_size = header.frame_data_size;
    out_stream->payload_size = payload_size;
    out_stream->file_pos = header.toc_offset + (uint32_t)table_size;
    out_stream->file_pos_valid = true;
    return ANIM_STORAGE_OK;
}

void anim_stream_close(anim_stream_t *stream) {
    if (stream == NULL) {
        return;
    }

    if (stream->file != NULL) {
        fclose(stream->file);
    }
    if (stream->frames != NULL) {
        free(stream->frames);
    }
    if (stream->scratch_data != NULL) {
        heap_caps_free(stream->scratch_data);
    }
    memset(stream, 0, sizeof(*stream));
}

int anim_stream_get_frame_delay_ms(const anim_stream_t *stream, int frame_index) {
    if (stream == NULL || stream->frames == NULL || frame_index < 0 || frame_index >= stream->frame_count) {
        return 0;
    }

    int delay_ms = stream->frames[frame_index].delay_ms;
    if (delay_ms > 0) {
        return delay_ms;
    }

    if (stream->info.fps > 0) {
        return 1000 / stream->info.fps;
    }

    return 1000 / anim_storage_default_fps();
}

int anim_stream_read_frame(anim_stream_t *stream, int frame_index, anim_frame_buffer_t *buffer) {
    if (stream == NULL || stream->file == NULL || stream->frames == NULL || buffer == NULL) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }
    if (frame_index < 0 || frame_index >= stream->frame_count) {
        return ANIM_STORAGE_INVALID_RESOURCE;
    }
    if (anim_fault_injection_should_fail(ANIM_FAULT_SD_FRAME_READ)) {
        return ANIM_STORAGE_SD_READ_FAILED;
    }

    anim_pack_frame_desc_t *frame = &stream->frames[frame_index];
    bool indexed8 = (frame->flags & ANIM_FRAME_FLAG_INDEXED8) != 0;
    if (!anim_frame_desc_is_valid(stream->info.name, stream->info.width, stream->info.height, stream->payload_size,
                                  frame_index, frame)) {
        return ANIM_STORAGE_PACK_CORRUPT;
    }

    uint16_t output_width = anim_stream_output_width(stream);
    uint16_t output_height = anim_stream_output_height(stream);
    int buffer_result = anim_frame_buffer_ensure(buffer, output_width, output_height);
    if (buffer_result != ANIM_STORAGE_OK) {
        return buffer_result;
    }

    uint8_t *read_buffer = stream->scratch_data;
    int scratch_result = anim_stream_ensure_scratch(stream, frame->size);
    if (scratch_result != ANIM_STORAGE_OK) {
        return scratch_result;
    }
    read_buffer = stream->scratch_data;

    if (indexed8) {
        scratch_result = anim_stream_ensure_scratch(stream, frame->size);
        if (scratch_result != ANIM_STORAGE_OK) {
            return scratch_result;
        }
        read_buffer = stream->scratch_data;
    }

    for (int attempt = 1; attempt <= k_frame_read_attempts; ++attempt) {
        errno = 0;
        clearerr(stream->file);

        uint32_t target_pos = stream->payload_offset + frame->offset;
        bool positioned = stream->file_pos_valid && stream->file_pos == target_pos;
        if (positioned || fseek(stream->file, (long)target_pos, SEEK_SET) == 0) {
            stream->file_pos = target_pos;
            stream->file_pos_valid = true;
            size_t bytes_read = anim_fault_injection_should_fail(ANIM_FAULT_SD_SHORT_READ)
                                    ? (frame->size > 0U ? frame->size - 1U : 0U)
                                    : fread(read_buffer, 1, frame->size, stream->file);
            stream->file_pos += (uint32_t)bytes_read;
            if (bytes_read == frame->size) {
                if (indexed8 && decode_indexed8_frame(stream, frame, read_buffer, buffer) != 0) {
                    return ANIM_STORAGE_PACK_CORRUPT;
                }
                if (!indexed8 && decode_rgb565_scaled_frame(stream, frame, read_buffer, buffer) != 0) {
                    return ANIM_STORAGE_PACK_CORRUPT;
                }
                buffer->img_dsc.data = buffer->img_data;
                buffer->img_dsc.data_size = (uint32_t)buffer->data_size;
                return ANIM_STORAGE_OK;
            }

            ESP_LOGW(TAG, "Short read for %s frame %d: got=%u expected=%u errno=%d ferror=%d feof=%d (attempt %d/%d)",
                     stream->info.name, frame_index, (unsigned)bytes_read, (unsigned)frame->size, errno,
                     ferror(stream->file), feof(stream->file), attempt, k_frame_read_attempts);
            stream->file_pos_valid = false;
        } else {
            ESP_LOGW(TAG, "Failed to seek %s frame %d: errno=%d (%s) (attempt %d/%d)", stream->info.name, frame_index,
                     errno, strerror(errno), attempt, k_frame_read_attempts);
            stream->file_pos_valid = false;
        }

        if (attempt < k_frame_read_attempts && anim_stream_reopen_file(stream) == 0) {
            continue;
        }
        break;
    }

    return ANIM_STORAGE_SD_READ_FAILED;
}

int anim_load_static_frame(emoji_anim_type_t type, int frame_index, anim_frame_buffer_t *buffer) {
    anim_stream_t stream = {0};
    int result = -1;

    if (anim_stream_open(type, &stream) != 0) {
        return -1;
    }

    result = anim_stream_read_frame(&stream, frame_index, buffer);
    anim_stream_close(&stream);
    return result;
}

lv_img_dsc_t *emoji_get_image(emoji_anim_type_t type, int frame) {
    if (anim_load_static_frame(type, frame, &g_legacy_frame) != 0) {
        return NULL;
    }
    return &g_legacy_frame.img_dsc;
}

int emoji_load_all_images(void) {
    return emoji_load_all_images_with_cb(NULL);
}

int emoji_load_all_images_with_cb(emoji_progress_cb_t cb) {
    if (ensure_manifest_loaded() != 0) {
        return -1;
    }

    int total = 0;
    for (int type = 0; type < EMOJI_ANIM_COUNT; ++type) {
        if (!anim_catalog_has_type((emoji_anim_type_t)type)) {
            continue;
        }
        ++total;
        if (cb != NULL) {
            cb((emoji_anim_type_t)type, total, EMOJI_ANIM_COUNT);
        }
    }
    return total > 0 ? 0 : -1;
}

void emoji_free_all(void) {
    anim_frame_buffer_free(&g_legacy_frame);
}

bool emoji_images_loaded(void) {
    return false;
}
