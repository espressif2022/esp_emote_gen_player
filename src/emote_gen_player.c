/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "widget/gfx_anim.h"
#include "widget/gfx_label.h"
#include "emote_gen_player_priv.h"

LV_FONT_DECLARE(font_puhui_basic_20_4);

static const char *TAG = "emote_gen_player";
static const uint32_t EMOTE_GEN_PLAYER_DEFAULT_FPS = 25;
static const uint32_t EMOTE_GEN_PLAYER_DEFAULT_LOOP_MID_PLAYS = 0;
/** Tip strip height (px), same order of magnitude as expression emote_init. */
static const int32_t EMOTE_GEN_PLAYER_DEF_LABEL_H = 40;
static const uint32_t EMOTE_GEN_PLAYER_DEF_LABEL_SCROLL_SPEED = 50;
static const uint32_t EMOTE_GEN_PLAYER_DEF_LABEL_COLOR = 0xFFFFFF;

static esp_err_t emote_gen_player_configure_anim(gfx_obj_t *obj, const emote_gen_player_play_spec_t *spec,
                                                 const void *anim_data, size_t anim_len);

static const char *emote_gen_player_segment_action_str(gfx_anim_segment_action_t action)
{
    switch (action) {
    case GFX_ANIM_SEGMENT_ACTION_CONTINUE:
        return "CONTINUE";
    case GFX_ANIM_SEGMENT_ACTION_PAUSE:
        return "PAUSE";
    default:
        return "UNKNOWN";
    }
}

esp_err_t emote_gen_player_notify_flush_finished(emote_gen_player_handle_t handle)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    gfx_disp_flush_ready(handle->disp, true);
    return ret;
}

static void emote_gen_player_flush_cb_wrapper(gfx_disp_t *disp, int x1, int y1, int x2, int y2, const void *data)
{
    emote_gen_player_handle_t handle = (emote_gen_player_handle_t)gfx_disp_get_user_data(disp);

    if (handle != NULL && handle->flush_cb != NULL) {
        handle->flush_cb(x1, y1, x2, y2, data, handle);
    }
}

static void emote_gen_player_update_cb_wrapper(gfx_disp_t *disp, gfx_disp_event_t event, const void *obj)
{
    emote_gen_player_handle_t handle = (emote_gen_player_handle_t)gfx_disp_get_user_data(disp);

    if (handle != NULL && handle->update_cb != NULL) {
        handle->update_cb(event, obj, handle);
    }
}

static esp_err_t emote_gen_player_apply_segments(gfx_obj_t *obj, const emote_gen_player_play_spec_t *spec, uint32_t fps,
                                                 uint32_t loop_mid_plays)
{
    gfx_anim_segment_t segments[3];

    if (spec->has_loop_range) {
        segments[0].start = 0;
        segments[0].end = (uint32_t)spec->loop_start - 1;
        segments[0].fps = fps;
        segments[0].play_count = 1;
        segments[0].end_action = GFX_ANIM_SEGMENT_ACTION_CONTINUE;

        segments[1].start = (uint32_t)spec->loop_start;
        segments[1].end = (uint32_t)spec->loop_end - 1;
        segments[1].fps = fps;
        segments[1].play_count = loop_mid_plays;
        segments[1].end_action = GFX_ANIM_SEGMENT_ACTION_CONTINUE;

        segments[2].start = (uint32_t)spec->loop_end;
        segments[2].end = 0xFFFFFFFF;
        segments[2].fps = fps;
        segments[2].play_count = 1;
        segments[2].end_action = GFX_ANIM_SEGMENT_ACTION_CONTINUE;

        ESP_LOGI("", "[0] segments: [%d, %d], (fps:%d, play_count:%d, action:%s)",
                 segments[0].start, segments[0].end, segments[0].fps, segments[0].play_count,
                 emote_gen_player_segment_action_str(segments[0].end_action));
        ESP_LOGI("", "[1] segments: [%d, %d], (fps:%d, play_count:%d, action:%s)",
                 segments[1].start, segments[1].end, segments[1].fps, segments[1].play_count,
                 emote_gen_player_segment_action_str(segments[1].end_action));
        ESP_LOGI("", "[2] segments: [%d, %d], (fps:%d, play_count:%d, action:%s)",
                 segments[2].start, segments[2].end, segments[2].fps, segments[2].play_count,
                 emote_gen_player_segment_action_str(segments[2].end_action));

        return gfx_anim_set_segments(obj, segments, 3);
    } else if (spec->has_stop_frame) {
        segments[0].start = 0;
        segments[0].end = (uint32_t)spec->stop_frame;
        segments[0].fps = fps;
        segments[0].play_count = 1;
        segments[0].end_action = GFX_ANIM_SEGMENT_ACTION_CONTINUE;

        segments[1].start = (uint32_t)spec->stop_frame;
        segments[1].end = 0xFFFFFFFF;
        segments[1].fps = fps;
        segments[1].play_count = 1;
        segments[1].end_action = GFX_ANIM_SEGMENT_ACTION_CONTINUE;

        ESP_LOGI("", "[0] segments: [%d, %d], (fps:%d, play_count:%d, action:%s)",
                 segments[0].start, segments[0].end, segments[0].fps, segments[0].play_count,
                 emote_gen_player_segment_action_str(segments[0].end_action));
        ESP_LOGI("", "[1] segments: [%d, %d], (fps:%d, play_count:%d, action:%s)",
                 segments[1].start, segments[1].end, segments[1].fps, segments[1].play_count,
                 emote_gen_player_segment_action_str(segments[1].end_action));

        return gfx_anim_set_segments(obj, segments, 2);
    } else {
        ESP_LOGI("", "[0] segments: [%d, %d], (fps:%d, play_count:%d)", 0, 0xFFFFFFFF, 50, 1);
    }

    return gfx_anim_set_segment(obj, 0, 0xFFFFFFFF, fps, false);
}

static esp_err_t emote_gen_player_lock(emote_gen_player_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(handle->gfx_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "gfx_handle is NULL");
    return gfx_emote_lock(handle->gfx_handle);
}

static void emote_gen_player_unlock(emote_gen_player_handle_t handle)
{
    if (handle != NULL && handle->gfx_handle != NULL) {
        gfx_emote_unlock(handle->gfx_handle);
    }
}

static int emote_gen_player_find_index_by_name(emote_gen_player_handle_t handle, const char *name)
{
    size_t i;

    if (handle == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }

    for (i = 0; i < handle->entry_count; i++) {
        if (strcmp(handle->entries[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static esp_err_t emote_gen_player_apply_anim_by_index(emote_gen_player_handle_t handle, size_t index)
{
    const emote_gen_player_index_entry_t *entry;
    const void *anim_data;
    size_t anim_size;

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(handle->anim_obj != NULL, ESP_ERR_INVALID_STATE, TAG, "anim_obj is NULL");
    ESP_RETURN_ON_FALSE(index < handle->entry_count, ESP_ERR_INVALID_ARG, TAG, "index out of range");

    entry = &handle->entries[index];
    ESP_RETURN_ON_FALSE(entry->asset_id >= 0, ESP_ERR_NOT_FOUND, TAG, "anim asset not found");

    anim_data = mmap_assets_get_mem(handle->mmap, entry->asset_id);
    anim_size = (size_t)mmap_assets_get_size(handle->mmap, entry->asset_id);
    ESP_RETURN_ON_FALSE(anim_data != NULL && anim_size > 0, ESP_ERR_INVALID_SIZE, TAG, "anim data is invalid");

    gfx_anim_stop(handle->anim_obj);
    ESP_RETURN_ON_ERROR(emote_gen_player_configure_anim(handle->anim_obj, &entry->play, anim_data, anim_size),
                        TAG, "configure anim failed");
    ESP_RETURN_ON_ERROR(gfx_anim_start(handle->anim_obj), TAG, "gfx_anim_start failed");

    handle->current_index = index;
    handle->has_active_anim = true;

    if (handle->tip_label != NULL) {
        esp_err_t lr = gfx_label_set_text(handle->tip_label, entry->name);
        if (lr != ESP_OK) {
            ESP_LOGW(TAG, "tip strip set_text failed: %s", esp_err_to_name(lr));
        }
    }

    return ESP_OK;
}

emote_gen_player_handle_t emote_gen_player_init(const emote_gen_player_config_t *config)
{
    esp_err_t ret = ESP_OK;
    emote_gen_player_handle_t handle = NULL;

    ESP_GOTO_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, error, TAG, "config is NULL");

    handle = calloc(1, sizeof(*handle));
    ESP_GOTO_ON_FALSE(handle != NULL, ESP_ERR_NO_MEM, error, TAG, "alloc handle failed");

    handle->flush_cb = config->flush_cb;
    handle->update_cb = config->update_cb;
    handle->current_index = SIZE_MAX;

    gfx_core_config_t gfx_cfg = {
        .fps = config->gfx_emote.fps,
        .task = {
            .task_priority = config->task.task_priority,
            .task_stack = config->task.task_stack,
            .task_affinity = config->task.task_affinity,
            .task_stack_caps = config->task.task_stack_in_ext ?
            (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) :
            (MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT),
        }
    };

    handle->gfx_handle = gfx_emote_init(&gfx_cfg);
    ESP_GOTO_ON_FALSE(handle->gfx_handle != NULL, ESP_ERR_INVALID_STATE, error, TAG, "gfx_emote_init failed");

    gfx_disp_config_t disp_cfg = {
        .h_res = config->gfx_emote.h_res,
        .v_res = config->gfx_emote.v_res,
        .flush_cb = emote_gen_player_flush_cb_wrapper,
        .update_cb = emote_gen_player_update_cb_wrapper,
        .user_data = handle,
        .flags = {
            .swap = config->flags.swap,
            .buff_dma = config->flags.buff_dma,
            .buff_spiram = config->flags.buff_spiram,
            .double_buffer = config->flags.double_buffer,
        },
        .buffers = {
            .buf1 = NULL,
            .buf2 = NULL,
            .buf_pixels = config->buffers.buf_pixels,
        },
    };
    ESP_GOTO_ON_ERROR(emote_gen_player_lock(handle), error, TAG, "gfx lock failed");

    handle->disp = gfx_disp_add(handle->gfx_handle, &disp_cfg);
    ESP_GOTO_ON_FALSE(handle->disp != NULL, ESP_ERR_INVALID_STATE, error, TAG, "gfx_disp_add failed");

    handle->anim_obj = gfx_anim_create(handle->disp);
    ESP_GOTO_ON_FALSE(handle->anim_obj != NULL, ESP_ERR_INVALID_STATE, error, TAG, "gfx_anim_create failed");

    handle->tip_label = gfx_label_create(handle->disp);
    ESP_GOTO_ON_FALSE(handle->tip_label != NULL, ESP_ERR_INVALID_STATE, error_unlock, TAG, "gfx_label_create failed");

    int32_t label_w = (int32_t)config->gfx_emote.h_res * 2 / 3;
    if (label_w < 1) {
        label_w = 1;
    }
    gfx_obj_set_size(handle->tip_label, label_w, EMOTE_GEN_PLAYER_DEF_LABEL_H);
    gfx_obj_align(handle->tip_label, GFX_ALIGN_TOP_MID, 0, 0);
    gfx_label_set_color(handle->tip_label, GFX_COLOR_HEX(EMOTE_GEN_PLAYER_DEF_LABEL_COLOR));
    gfx_label_set_text_align(handle->tip_label, GFX_TEXT_ALIGN_CENTER);
    gfx_label_set_long_mode(handle->tip_label, GFX_LABEL_LONG_SCROLL);
    gfx_label_set_scroll_speed(handle->tip_label, EMOTE_GEN_PLAYER_DEF_LABEL_SCROLL_SPEED);
    gfx_label_set_scroll_loop(handle->tip_label, true);
    gfx_label_set_font(handle->tip_label, (void *)&font_puhui_basic_20_4);
    gfx_label_set_text(handle->tip_label, "");
    gfx_obj_set_visible(handle->tip_label, true);

    emote_gen_player_unlock(handle);

    return handle;

error_unlock:
    emote_gen_player_unlock(handle);

error:
    emote_gen_player_deinit(handle);
    return NULL;
}

void emote_gen_player_deinit(emote_gen_player_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    emote_gen_player_unmount_assets(handle);

    if (handle->tip_label != NULL) {
        gfx_obj_delete(handle->tip_label);
        handle->tip_label = NULL;
    }

    if (handle->anim_obj != NULL) {
        gfx_obj_delete(handle->anim_obj);
        handle->anim_obj = NULL;
    }

    if (handle->gfx_handle != NULL) {
        gfx_emote_deinit(handle->gfx_handle);
        handle->gfx_handle = NULL;
        handle->disp = NULL;
    }
    free(handle);
}

esp_err_t emote_gen_player_unmount_assets(emote_gen_player_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->mmap != NULL) {
        mmap_assets_del(handle->mmap);
        handle->mmap = NULL;
    }

    handle->entry_count = 0;
    handle->current_index = SIZE_MAX;
    handle->has_active_anim = false;

    if (handle->entries != NULL) {
        memset(handle->entries, 0, handle->entry_capacity * sizeof(*handle->entries));
    }
    if (handle->anim_obj != NULL) {
        gfx_anim_stop(handle->anim_obj);
    }

    return ESP_OK;
}

static esp_err_t emote_gen_player_configure_anim(gfx_obj_t *obj, const emote_gen_player_play_spec_t *spec,
                                                 const void *anim_data, size_t anim_len)
{
    gfx_anim_src_t anim_src;
    uint32_t fps = EMOTE_GEN_PLAYER_DEFAULT_FPS;
    uint32_t loop_mid_plays = EMOTE_GEN_PLAYER_DEFAULT_LOOP_MID_PLAYS;

    ESP_RETURN_ON_FALSE(obj != NULL, ESP_ERR_INVALID_ARG, TAG, "obj is NULL");
    ESP_RETURN_ON_FALSE(spec != NULL, ESP_ERR_INVALID_ARG, TAG, "spec is NULL");
    ESP_RETURN_ON_FALSE(anim_data != NULL && anim_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid anim buffer");
    ESP_RETURN_ON_FALSE(!(spec->has_stop_frame && spec->has_loop_range), ESP_ERR_INVALID_ARG, TAG,
                        "stop_frame and loop range are mutually exclusive");

    anim_src.type = GFX_ANIM_SRC_TYPE_MEMORY;
    anim_src.data = anim_data;
    anim_src.data_len = anim_len;
    ESP_RETURN_ON_ERROR(gfx_anim_set_src_desc(obj, &anim_src), TAG, "gfx_anim_set_src_desc failed");

    if (spec->width > 0 && spec->height > 0) {
        ESP_RETURN_ON_ERROR(gfx_obj_set_size(obj, spec->width, spec->height), TAG, "gfx_obj_set_size failed");
    }

    ESP_RETURN_ON_ERROR(gfx_anim_set_auto_mirror(obj, false), TAG, "gfx_anim_set_auto_mirror failed");

    if (spec->x == 0 && spec->y == 0) {
        ESP_RETURN_ON_ERROR(gfx_obj_align(obj, GFX_ALIGN_CENTER, 0, 0), TAG, "gfx_obj_align failed");
    } else {
        ESP_RETURN_ON_ERROR(gfx_obj_set_pos(obj, spec->x, spec->y), TAG, "gfx_obj_set_pos failed");
    }

    return emote_gen_player_apply_segments(obj, spec, fps, loop_mid_plays);
}

gfx_disp_t *emote_gen_player_get_disp(emote_gen_player_handle_t handle)
{
    return (handle != NULL) ? handle->disp : NULL;
}

gfx_handle_t emote_gen_player_get_gfx_handle(emote_gen_player_handle_t handle)
{
    return (handle != NULL) ? handle->gfx_handle : NULL;
}

gfx_obj_t *emote_gen_player_get_tip_label(emote_gen_player_handle_t handle)
{
    return (handle != NULL) ? handle->tip_label : NULL;
}

esp_err_t emote_gen_player_set_tip_text(emote_gen_player_handle_t handle, const char *text)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(handle->tip_label != NULL, ESP_ERR_INVALID_STATE, TAG, "tip strip not available");

    ESP_RETURN_ON_ERROR(emote_gen_player_lock(handle), TAG, "player lock failed");
    esp_err_t ret = gfx_label_set_text(handle->tip_label, text != NULL ? text : "");
    emote_gen_player_unlock(handle);
    return ret;
}

esp_err_t emote_gen_player_anim_now(emote_gen_player_handle_t handle, size_t index)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_ERROR(emote_gen_player_lock(handle), TAG, "player lock failed");
    esp_err_t ret = emote_gen_player_apply_anim_by_index(handle, index);
    emote_gen_player_unlock(handle);
    return ret;
}

esp_err_t emote_gen_player_anim_now_name(emote_gen_player_handle_t handle, const char *name)
{
    int index = emote_gen_player_find_index_by_name(handle, name);

    ESP_RETURN_ON_FALSE(index >= 0, ESP_ERR_NOT_FOUND, TAG, "anim name not found");
    return emote_gen_player_anim_now(handle, (size_t)index);
}

esp_err_t emote_gen_player_anim_fade(emote_gen_player_handle_t handle, size_t index)
{
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(index < handle->entry_count, ESP_ERR_INVALID_ARG, TAG, "index out of range");

    if (!handle->has_active_anim || handle->current_index == SIZE_MAX) {
        return emote_gen_player_anim_now(handle, index);
    }

    if (handle->current_index == index) {
        return ESP_OK;
    }

    /* gfx_anim_play_left_to_tail must not run under gfx_emote_lock */
    ret = gfx_anim_play_left_to_tail(handle->anim_obj);
    if (ret == ESP_ERR_NOT_FOUND) {
        return emote_gen_player_anim_now(handle, index);
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "gfx_anim_play_left_to_tail failed");

    return emote_gen_player_anim_now(handle, index);
}

esp_err_t emote_gen_player_anim_fade_name(emote_gen_player_handle_t handle, const char *name)
{
    int index = emote_gen_player_find_index_by_name(handle, name);

    ESP_RETURN_ON_FALSE(index >= 0, ESP_ERR_NOT_FOUND, TAG, "anim name not found");
    return emote_gen_player_anim_fade(handle, (size_t)index);
}