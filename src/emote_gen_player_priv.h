#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_mmap_assets.h"
#include "emote_gen_player.h"

struct emote_gen_player {
    mmap_assets_handle_t mmap;
    gfx_handle_t gfx_handle;
    gfx_disp_t *disp;
    gfx_obj_t *tip_label;
    gfx_obj_t *anim_obj;
    emote_gen_player_index_entry_t *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t current_index;
    bool has_active_anim;
    emote_gen_player_flush_ready_cb_t flush_cb;
    emote_gen_player_update_cb_t update_cb;
    EventGroupHandle_t anim_events;
};
