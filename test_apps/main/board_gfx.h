/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"
#include "emote_gen_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    emote_gen_player_handle_t player;
} test_app_runtime_t;

typedef enum {
    TEST_APP_PARTITION_TYPE_EMOTE_GEN = 0,
} test_app_partition_type_t;

/** Called from the board touch path when a press event is reported (gfx touch). */
typedef void (*test_app_touch_press_cb_t)(void *user_data);

void test_app_set_touch_press_cb(test_app_touch_press_cb_t cb, void *user_data);

esp_err_t test_app_runtime_open(test_app_runtime_t *runtime, test_app_partition_type_t partition_type);
void test_app_runtime_close(test_app_runtime_t *runtime);
void test_app_log_case(const char *tag, const char *case_name);

#ifdef __cplusplus
}
#endif
