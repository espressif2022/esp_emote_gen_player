/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/gfx_disp.h"
#include "core/gfx_obj.h"
#include "esp_err.h"
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Macro constants                                                            */
/* -------------------------------------------------------------------------- */

/** @brief Max string length for `name` / `file` fields in index entries. */
#define EMOTE_GEN_PLAYER_STR_MAX 96
/** @brief Default manifest file name inside an emote_gen asset pack. */
#define EMOTE_GEN_PLAYER_INDEX_JSON_NAME "index.json"

/* -------------------------------------------------------------------------- */
/* Enumerations                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Where the asset pack is loaded from.
 */
typedef enum {
    EMOTE_GEN_PLAYER_SOURCE_PATH = 0,     /**< Filesystem path in `source.path`. */
    EMOTE_GEN_PLAYER_SOURCE_PARTITION,    /**< Partition label in `source.partition_label`. */
} emote_gen_player_source_type_t;

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/** @brief Opaque runtime handle from @ref emote_gen_player_init. */
typedef struct emote_gen_player *emote_gen_player_handle_t;

/**
 * @brief Playback layout and segment settings for one pack index item.
 */
typedef struct {
    int16_t x;              /**< X position on display. */
    int16_t y;              /**< Y position on display. */
    uint16_t width;         /**< Optional width hint; `0` to use anim default. */
    uint16_t height;        /**< Optional height hint; `0` to use anim default. */
    int32_t stop_frame;     /**< Single-segment stop frame when `has_stop_frame`. */
    int32_t loop_start;     /**< Loop segment start frame when `has_loop_range`. */
    int32_t loop_end;       /**< Loop segment end frame when `has_loop_range`. */
    bool has_stop_frame;    /**< If true, use `stop_frame` segment plan. */
    bool has_loop_range;    /**< If true, use intro + loop + tail segment plan. */
} emote_gen_player_play_spec_t;

/**
 * @brief One parsed pack index entry and its mmap `asset_id`.
 */
typedef struct {
    char name[EMOTE_GEN_PLAYER_STR_MAX];  /**< Logical animation name. */
    char file[EMOTE_GEN_PLAYER_STR_MAX]; /**< Member file path inside the pack. */
    emote_gen_player_play_spec_t play;    /**< Layout and segment options. */
    int asset_id;                          /**< Mmap asset id, or negative if unresolved. */
} emote_gen_player_index_entry_t;

/**
 * @brief Called when the player needs the board to flush one display region.
 *
 * @param x_start   Flush area left.
 * @param y_start   Flush area top.
 * @param x_end     Flush area right (exclusive per panel convention).
 * @param y_end     Flush area bottom (exclusive per panel convention).
 * @param data      Pixel buffer for the region.
 * @param manager   Player handle passed as `user_data` from init.
 */
typedef void (*emote_gen_player_flush_ready_cb_t)(int x_start, int y_start, int x_end, int y_end, const void *data,
                                                  emote_gen_player_handle_t manager);

/**
 * @brief Called when the display backend reports animation-related events.
 *
 * @param event     Event id (`gfx_disp_event_t`).
 * @param obj       Related gfx object pointer, if any.
 * @param manager   Player handle from init.
 */
typedef void (*emote_gen_player_update_cb_t)(gfx_disp_event_t event, const void *obj,
                                             emote_gen_player_handle_t manager);

/**
 * @brief Runtime configuration for @ref emote_gen_player_init.
 */
typedef struct {
    struct {
        bool swap;            /**< Color byte swap for panel. */
        bool double_buffer;   /**< Double framebuffer. */
        bool buff_dma;        /**< DMA-capable frame buffers. */
        bool buff_spiram;     /**< Allocate buffers in PSRAM when possible. */
    } flags;
    struct {
        int h_res;            /**< Horizontal resolution (px). */
        int v_res;            /**< Vertical resolution (px). */
        int fps;              /**< Gfx render tick rate. */
    } gfx_emote;
    struct {
        size_t buf_pixels;    /**< Pixels per internal buffer chunk. */
    } buffers;
    struct {
        int task_priority;    /**< Gfx render task priority. */
        int task_stack;       /**< Gfx render task stack size (bytes). */
        int task_affinity;    /**< Core affinity; `-1` for no pin. */
        bool task_stack_in_ext; /**< If true, prefer PSRAM for stack caps. */
    } task;
    emote_gen_player_flush_ready_cb_t flush_cb; /**< Panel flush hook. */
    emote_gen_player_update_cb_t update_cb;     /**< Optional disp event hook. */
} emote_gen_player_config_t;

/**
 * @brief Asset source for @ref emote_gen_player_mount_assets.
 */
typedef struct {
    emote_gen_player_source_type_t type; /**< Path vs partition. */
    union {
        const char *path;               /**< Directory or file path when type is PATH. */
        const char *partition_label;  /**< Partition name when type is PARTITION. */
    } source;
    struct {
        uint8_t mmap_enable: 1;         /**< Enable mmap of the pack image. */
    } flags;
} emote_gen_player_data_t;

/* -------------------------------------------------------------------------- */
/* Functions                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Create gfx runtime, display, main animation object, and tip label.
 *
 * @param[in] config  Display size, buffers, task, and flush/update callbacks. Must not be NULL.
 *
 * @return
 *        - Valid @ref emote_gen_player_handle_t on success
 *        - `NULL` on failure (logs from implementation)
 */
emote_gen_player_handle_t emote_gen_player_init(const emote_gen_player_config_t *config);

/**
 * @brief Destroy the player: unmount assets, delete objects, deinit gfx.
 *
 * @param[in] handle  Player handle; safe to pass `NULL` (no-op).
 *
 * @return None
 */
void emote_gen_player_deinit(emote_gen_player_handle_t handle);

/**
 * @brief Tell gfx the asynchronous flush for this display has finished.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_INVALID_ARG if `handle` is NULL
 */
esp_err_t emote_gen_player_notify_flush_finished(emote_gen_player_handle_t handle);

/**
 * @brief Mount an emote_gen mmap pack and parse its index (replaces previous mount).
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] data    Source path or partition and mmap flags. Must not be NULL.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_* on argument, mmap, or parse errors
 */
esp_err_t emote_gen_player_mount_assets(emote_gen_player_handle_t handle, const emote_gen_player_data_t *data);

/**
 * @brief Unmount the current pack and stop playback.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 *
 * @return
 *        - ESP_OK on success (including nothing mounted)
 *        - ESP_ERR_INVALID_ARG if `handle` is NULL
 */
esp_err_t emote_gen_player_unmount_assets(emote_gen_player_handle_t handle);

/**
 * @brief Get the display created by init (for panel IO registration, etc.).
 *
 * @param[in] handle  Player handle. May be NULL.
 *
 * @return Display pointer, or `NULL` if `handle` is NULL or not initialized.
 */
gfx_disp_t *emote_gen_player_get_disp(emote_gen_player_handle_t handle);

/**
 * @brief Get the gfx core handle (for `gfx_touch_add`, timers, lock, etc.).
 *
 * @param[in] handle  Player handle. May be NULL.
 *
 * @return Gfx handle, or `NULL` if `handle` is NULL or gfx not inited.
 */
gfx_handle_t emote_gen_player_get_gfx_handle(emote_gen_player_handle_t handle);

/**
 * @brief Get the top tip `gfx_label` (PuHui, top-mid, ~2/3 width): prompts and current clip title.
 *
 * @param[in] handle  Player handle. May be NULL.
 *
 * @return Label object, or `NULL` if unavailable.
 */
gfx_obj_t *emote_gen_player_get_tip_label(emote_gen_player_handle_t handle);

/**
 * @brief Set tip strip UTF-8 text (`NULL` or `""` clears). Thread-safe (gfx lock).
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] text    C string or NULL for empty.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_INVALID_ARG if `handle` is NULL
 *        - ESP_ERR_INVALID_STATE if tip label was not created
 *        - Other ESP_ERR_* from `gfx_label_set_text`
 */
esp_err_t emote_gen_player_set_tip_text(emote_gen_player_handle_t handle, const char *text);

/**
 * @brief Number of animation entries in the currently mounted pack.
 *
 * @param[in] handle  Player handle. May be NULL.
 *
 * @return Entry count, or `0` if no pack or invalid handle.
 */
size_t emote_gen_player_get_index_count(emote_gen_player_handle_t handle);

/**
 * @brief Read one index entry by position.
 *
 * @param[in] handle  Player handle. May be NULL.
 * @param[in] index   Zero-based index.
 *
 * @return Pointer to entry, or `NULL` if out of range or not mounted.
 */
const emote_gen_player_index_entry_t *emote_gen_player_get_index_entry(emote_gen_player_handle_t handle,
                                                                       size_t index);

/**
 * @brief Switch animation immediately by index; tip shows the clip `name`.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] index   Index in `[0, entry_count)`.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_STATE / ESP_ERR_NOT_FOUND / etc. on failure
 */
esp_err_t emote_gen_player_anim_now(emote_gen_player_handle_t handle, size_t index);

/**
 * @brief Switch animation immediately by logical name.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] name    Entry `name` field; must not be NULL or empty.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_NOT_FOUND if name is unknown
 *        - Other codes from @ref emote_gen_player_anim_now
 */
esp_err_t emote_gen_player_anim_now_name(emote_gen_player_handle_t handle, const char *name);

/**
 * @brief After current segment plan drains, switch by index; tip shows new `name`.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] index   Target index in `[0, entry_count)`.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_* on invalid args, gfx drain failure, or apply failure
 */
esp_err_t emote_gen_player_anim_fade(emote_gen_player_handle_t handle, size_t index);

/**
 * @brief After current segment plan drains, switch by logical name.
 *
 * @param[in] handle  Player handle. Must not be NULL.
 * @param[in] name    Entry `name` field; must not be NULL or empty.
 *
 * @return
 *        - ESP_OK on success
 *        - ESP_ERR_NOT_FOUND if name is unknown
 *        - Other codes from @ref emote_gen_player_anim_fade
 */
esp_err_t emote_gen_player_anim_fade_name(emote_gen_player_handle_t handle, const char *name);

#ifdef __cplusplus
}
#endif
