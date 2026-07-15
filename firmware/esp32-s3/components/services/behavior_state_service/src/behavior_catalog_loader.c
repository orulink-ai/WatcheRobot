#include "behavior_catalog_loader.h"

#include <stdio.h>
#include <stdlib.h>

static esp_err_t read_candidate(const char *path, size_t max_bytes, char **out_json, size_t *out_len) {
    FILE *file = NULL;
    char *json = NULL;
    long file_size;
    size_t read_size;

    if (path == NULL || path[0] == '\0' || max_bytes == 0U || out_json == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    file_size = ftell(file);
    if (file_size <= 0 || (size_t)file_size > max_bytes || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    json = (char *)calloc((size_t)file_size + 1U, 1U);
    if (json == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    read_size = fread(json, 1U, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(json);
        return ESP_FAIL;
    }

    *out_json = json;
    *out_len = read_size;
    return ESP_OK;
}

esp_err_t behavior_catalog_load_first_valid(const behavior_catalog_candidate_t *candidates, size_t candidate_count,
                                             size_t max_bytes, behavior_anim_validator_t anim_validator,
                                             void *anim_validator_ctx, behavior_catalog_t *out_catalog,
                                             size_t *out_selected_index) {
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    size_t index;

    if (candidates == NULL || candidate_count == 0U || max_bytes == 0U || out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_selected_index != NULL) {
        *out_selected_index = SIZE_MAX;
    }

    for (index = 0U; index < candidate_count; ++index) {
        char *json = NULL;
        size_t json_len = 0U;
        esp_err_t ret = read_candidate(candidates[index].path, max_bytes, &json, &json_len);

        if (ret == ESP_ERR_NO_MEM) {
            return ret;
        }
        if (ret != ESP_OK) {
            if (ret != ESP_ERR_NOT_FOUND || last_error == ESP_ERR_NOT_FOUND) {
                last_error = ret;
            }
            continue;
        }

        ret = behavior_catalog_parse_json(json, json_len, anim_validator, anim_validator_ctx, out_catalog);
        free(json);
        if (ret == ESP_OK) {
            if (out_selected_index != NULL) {
                *out_selected_index = index;
            }
            return ESP_OK;
        }
        if (ret == ESP_ERR_NO_MEM) {
            return ret;
        }
        last_error = ret;
    }

    return last_error;
}
