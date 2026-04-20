/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "unity.h"

static const char *TAG = "emote_gen_player_test";

void app_main(void)
{
    ESP_LOGI(TAG, "Unity test app (emote_gen_player)");
    unity_run_menu();
}
