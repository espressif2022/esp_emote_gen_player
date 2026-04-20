/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mmap_assets.h"

#include "emote_gen_player_priv.h"

static const char *TAG = "emote_gen_player";
static const size_t EMOTE_GEN_PLAYER_INDEX_MAX_DEFAULT = 64;

static esp_err_t emote_gen_player_parse_index_json_array(const char *json, size_t json_len,
                                                         emote_gen_player_index_entry_t *entries, size_t max_entries,
                                                         size_t *out_count)
{
    ESP_RETURN_ON_FALSE(json != NULL && json_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid json buffer");
    ESP_RETURN_ON_FALSE(entries != NULL && max_entries > 0, ESP_ERR_INVALID_ARG, TAG, "invalid entries");
    ESP_RETURN_ON_FALSE(out_count != NULL, ESP_ERR_INVALID_ARG, TAG, "out_count is NULL");

    *out_count = 0;

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_ERR_INVALID_RESPONSE, TAG, "cJSON parse failed");

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (*out_count >= max_entries) {
            break;
        }

        emote_gen_player_index_entry_t *entry = &entries[*out_count];
        memset(entry, 0, sizeof(*entry));
        entry->asset_id = -1;
        entry->play.width = 200;
        entry->play.height = 150;

        cJSON *json_name = cJSON_GetObjectItem(item, "name");
        cJSON *json_file = cJSON_GetObjectItem(item, "file");
        cJSON *json_x = cJSON_GetObjectItem(item, "x");
        cJSON *json_y = cJSON_GetObjectItem(item, "y");
        cJSON *json_loop = cJSON_GetObjectItem(item, "loop");

        if (cJSON_IsString(json_name) && json_name->valuestring != NULL) {
            strncpy(entry->name, json_name->valuestring, sizeof(entry->name) - 1);
        }
        if (cJSON_IsString(json_file) && json_file->valuestring != NULL) {
            strncpy(entry->file, json_file->valuestring, sizeof(entry->file) - 1);
        }
        if (cJSON_IsNumber(json_x)) {
            entry->play.x = (int16_t)json_x->valueint;
        }
        if (cJSON_IsNumber(json_y)) {
            entry->play.y = (int16_t)json_y->valueint;
        }
        if (cJSON_IsArray(json_loop)) {
            int loop_size = cJSON_GetArraySize(json_loop);
            cJSON *loop_start = cJSON_GetArrayItem(json_loop, 0);
            cJSON *loop_end = cJSON_GetArrayItem(json_loop, 1);

            if (loop_size == 1 && cJSON_IsNumber(loop_start)) {
                entry->play.stop_frame = loop_start->valueint;
                entry->play.has_stop_frame = true;
            } else if (loop_size >= 2 && cJSON_IsNumber(loop_start) && cJSON_IsNumber(loop_end)) {
                entry->play.loop_start = loop_start->valueint;
                entry->play.loop_end = loop_end->valueint;
                entry->play.has_loop_range = true;
            }
        }

        (*out_count)++;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static int emote_gen_player_mmap_find_asset_id(mmap_assets_handle_t mmap, const char *filename)
{
    if (filename == NULL || filename[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < mmap_assets_get_stored_files(mmap); i++) {
        const char *name = mmap_assets_get_name(mmap, i);
        if (name != NULL && strcmp(name, filename) == 0) {
            return i;
        }
    }

    return -1;
}

static esp_err_t emote_gen_player_load_index_from_mmap(mmap_assets_handle_t mmap, const char *index_filename,
                                                       emote_gen_player_index_entry_t *entries, size_t max_entries,
                                                       size_t *out_count)
{
    ESP_RETURN_ON_FALSE(mmap != NULL, ESP_ERR_INVALID_ARG, TAG, "mmap is NULL");
    ESP_RETURN_ON_FALSE(index_filename != NULL && index_filename[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "index_filename is invalid");
    ESP_RETURN_ON_FALSE(entries != NULL && max_entries > 0, ESP_ERR_INVALID_ARG, TAG, "invalid entries buffer");
    ESP_RETURN_ON_FALSE(out_count != NULL, ESP_ERR_INVALID_ARG, TAG, "out_count is NULL");

    int index_id = emote_gen_player_mmap_find_asset_id(mmap, index_filename);
    if (index_id < 0) {
        ESP_LOGW(TAG, "%s not found in mmap", index_filename);
        *out_count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    const void *json_mem = mmap_assets_get_mem(mmap, index_id);
    size_t json_len = (size_t)mmap_assets_get_size(mmap, index_id);
    if (json_mem == NULL || json_len == 0) {
        *out_count = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(emote_gen_player_parse_index_json_array((const char *)json_mem, json_len, entries,
                                                                max_entries, out_count),
                        TAG, "parse index json failed");

    for (size_t i = 0; i < *out_count; i++) {
        entries[i].asset_id = emote_gen_player_mmap_find_asset_id(mmap, entries[i].file);
        if (entries[i].asset_id < 0) {
            ESP_LOGW(TAG, "mmap has no file \"%s\" (name=%s)", entries[i].file, entries[i].name);
        }
    }

    return ESP_OK;
}

esp_err_t emote_gen_player_mount_assets(emote_gen_player_handle_t handle, const emote_gen_player_data_t *data)
{
    esp_err_t ret;
    mmap_assets_config_t asset_config = {0};

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is NULL");

    ret = emote_gen_player_unmount_assets(handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "unmount existing assets failed");

    if (handle->entries == NULL) {
        handle->entry_capacity = EMOTE_GEN_PLAYER_INDEX_MAX_DEFAULT;
        handle->entries = calloc(handle->entry_capacity, sizeof(*handle->entries));
        ESP_RETURN_ON_FALSE(handle->entries != NULL, ESP_ERR_NO_MEM, TAG, "alloc entries failed");
    }

    asset_config.max_files = 0;
    asset_config.checksum = 0;

    if (data->type == EMOTE_GEN_PLAYER_SOURCE_PATH) {
        ESP_RETURN_ON_FALSE(data->source.path != NULL && data->source.path[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                            "path is invalid");
        asset_config.partition_label = data->source.path;
        asset_config.flags.use_fs = true;
        asset_config.flags.full_check = true;
    } else if (data->type == EMOTE_GEN_PLAYER_SOURCE_PARTITION) {
        ESP_RETURN_ON_FALSE(data->source.partition_label != NULL && data->source.partition_label[0] != '\0',
                            ESP_ERR_INVALID_ARG, TAG, "partition_label is invalid");
        asset_config.partition_label = data->source.partition_label;
        asset_config.flags.mmap_enable = data->flags.mmap_enable;
        asset_config.flags.full_check = true;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    ret = mmap_assets_new(&asset_config, &handle->mmap);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mmap_assets_new failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = emote_gen_player_load_index_from_mmap(handle->mmap, EMOTE_GEN_PLAYER_INDEX_JSON_NAME, handle->entries,
                                                handle->entry_capacity, &handle->entry_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "load index from mmap failed: %s", esp_err_to_name(ret));
        emote_gen_player_unmount_assets(handle);
        return ret;
    }

    return ESP_OK;
}

size_t emote_gen_player_get_index_count(emote_gen_player_handle_t handle)
{
    return (handle != NULL) ? handle->entry_count : 0;
}

const emote_gen_player_index_entry_t *emote_gen_player_get_index_entry(emote_gen_player_handle_t handle, size_t index)
{
    if (handle == NULL || index >= handle->entry_count) {
        return NULL;
    }

    return &handle->entries[index];
}
