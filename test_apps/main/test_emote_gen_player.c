/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "unity.h"

#include "board_gfx.h"
#include "emote_gen_player.h"

static const char *TAG = "test_emote_gen";

/** Observation window: tap screen to cycle animations (now / fade alternating). */
#define TEST_ANIM_TOUCH_WINDOW_MS (60 * 1000)

static size_t s_touch_cycle_index;
static bool s_touch_cycle_use_fade;

/**
 * Touch `event_cb` runs on the gfx render task inside `gfx_timer_handler`, while
 * `gfx_emote_lock` (render_mutex) is held. `emote_gen_player_anim_fade` ->
 * `gfx_anim_play_left_to_tail` blocks on an anim event group until the render loop
 * advances playback; that never happens until the timer callback returns → deadlock.
 * Queue the switch and run it from a dedicated task instead.
 */
typedef struct {
    emote_gen_player_handle_t player;
    size_t index;
    bool fade;
} test_anim_switch_msg_t;

static QueueHandle_t s_anim_switch_q;
static TaskHandle_t s_anim_switch_task_handle;

static void test_anim_switch_worker(void *arg)
{
    (void)arg;
    test_anim_switch_msg_t msg;

    for (;;) {
        if (xQueueReceive(s_anim_switch_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg.player == NULL) {
            continue;
        }
        ESP_LOGI(TAG, "switch task -> anim %zu (%s)", msg.index, msg.fade ? "fade" : "now");
        if (msg.fade) {
            emote_gen_player_anim_fade(msg.player, msg.index);
        } else {
            emote_gen_player_anim_now(msg.player, msg.index);
        }
    }
}

static void test_anim_switch_worker_ensure_started(void)
{
    if (s_anim_switch_q == NULL) {
        s_anim_switch_q = xQueueCreate(8, sizeof(test_anim_switch_msg_t));
    }
    if (s_anim_switch_task_handle == NULL && s_anim_switch_q != NULL) {
        xTaskCreate(test_anim_switch_worker, "anim_sw", 4096, NULL, 5, &s_anim_switch_task_handle);
    }
}

static void test_anim_on_touch_press(void *user_data)
{
    emote_gen_player_handle_t player = (emote_gen_player_handle_t)user_data;
    size_t n = emote_gen_player_get_index_count(player);

    if (n == 0 || player == NULL || s_anim_switch_q == NULL) {
        return;
    }
    size_t i = s_touch_cycle_index % n;
    bool fade = s_touch_cycle_use_fade;
    test_anim_switch_msg_t msg = {
        .player = player,
        .index = i,
        .fade = fade,
    };

    s_touch_cycle_index++;
    s_touch_cycle_use_fade = !s_touch_cycle_use_fade;

    ESP_LOGI(TAG, "touch queued -> anim %zu (%s)", i, fade ? "fade" : "now");
    if (xQueueSend(s_anim_switch_q, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "anim switch queue full, drop");
    }
}

static void test_anim_index_log(emote_gen_player_handle_t player)
{
    size_t index_count = emote_gen_player_get_index_count(player);

    for (size_t i = 0; i < index_count; i++) {
        const emote_gen_player_index_entry_t *e = emote_gen_player_get_index_entry(player, i);
        TEST_ASSERT_NOT_NULL(e);
        ESP_LOGI("", "index[%2zu] name:%-20s pos:(%2d,%2d) stop:%3d has_loop:%d loop:[%3d,%3d]",
                 i, e->name, e->play.x, e->play.y, e->play.stop_frame, (int)e->play.has_loop_range, e->play.loop_start,
                 e->play.loop_end);
    }
    ESP_LOGI(TAG, "index.json: loaded %zu entries", index_count);
}

static void test_anim_run_case_emote_gen(emote_gen_player_handle_t player)
{
    size_t index_count = emote_gen_player_get_index_count(player);

    test_anim_index_log(player);
    TEST_ASSERT(index_count > 0);

    test_app_log_case(TAG, "Animation decoder validation");

    test_anim_switch_worker_ensure_started();
    TEST_ASSERT_NOT_NULL_MESSAGE(s_anim_switch_q, "anim switch queue");
    TEST_ASSERT_NOT_NULL_MESSAGE(s_anim_switch_task_handle, "anim switch task");

    TEST_ASSERT_NOT_NULL_MESSAGE(emote_gen_player_get_tip_label(player), "tip label");
    emote_gen_player_anim_now(player, 0);
    /* Tip strip shows current clip name (synced inside emote_gen_player after each switch). */
    /* Next tap: same pattern as the old auto loop (index 1 with fade, then 2 with now, ...). */
    s_touch_cycle_index = 1;
    s_touch_cycle_use_fade = true;
    test_app_set_touch_press_cb(test_anim_on_touch_press, player);
    ESP_LOGI(TAG, "Tap screen to cycle animations (%d s window)", TEST_ANIM_TOUCH_WINDOW_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(TEST_ANIM_TOUCH_WINDOW_MS));
    test_app_set_touch_press_cb(NULL, NULL);
}

TEST_CASE("emote_gen_player mmap index segment playback", "[emote_gen]")
{
    test_app_runtime_t runtime;

    TEST_ASSERT_EQUAL(ESP_OK, test_app_runtime_open(&runtime, TEST_APP_PARTITION_TYPE_EMOTE_GEN));

    test_anim_run_case_emote_gen(runtime.player);

    test_app_runtime_close(&runtime);
}
