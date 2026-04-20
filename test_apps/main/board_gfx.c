/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"

#include "board_gfx.h"

static const char *TAG = "board_gfx";
static emote_gen_player_handle_t s_current_player;
static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static esp_lcd_touch_handle_t s_touch_handle;
static test_app_touch_press_cb_t s_touch_press_cb;
static void *s_touch_press_user_data;

void test_app_set_touch_press_cb(test_app_touch_press_cb_t cb, void *user_data)
{
    s_touch_press_cb = cb;
    s_touch_press_user_data = user_data;
}

static const char *test_app_get_partition_label(test_app_partition_type_t partition_type)
{
    switch (partition_type) {
    case TEST_APP_PARTITION_TYPE_EMOTE_GEN:
        return "emote_gen";
    default:
        return NULL;
    }
}

static void test_player_flush_ready_callback(int x1, int y1, int x2, int y2, const void *data,
                                             emote_gen_player_handle_t manager)
{
    (void)manager;
    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, data);
}

static void test_player_update_callback(gfx_disp_event_t event, const void *obj, emote_gen_player_handle_t manager)
{
    (void)manager;
    (void)obj;
    if (event == GFX_DISP_EVENT_PART_DONE) {
        ESP_LOGD("", "part done(%p): event:%d", obj, event);
    } else if (event == GFX_DISP_EVENT_ALL_DONE) {
        ESP_LOGD("", "all done(%p): event:%d", obj, event);
    }
}

static void test_player_touch_event_cb(gfx_touch_t *touch, const gfx_touch_event_t *event, void *user_data)
{
    (void)touch;
    (void)user_data;
    switch (event->type) {
    case GFX_TOUCH_EVENT_PRESS:
        ESP_LOGD(TAG, "touch press: (%u, %u)", event->x, event->y);
        if (s_touch_press_cb != NULL) {
            s_touch_press_cb(s_touch_press_user_data);
        }
        break;
    case GFX_TOUCH_EVENT_RELEASE:
        ESP_LOGD(TAG, "touch release: (%u, %u)", event->x, event->y);
        break;
    default:
        break;
    }
}

#if CONFIG_IDF_TARGET_ESP32S3
static bool test_flush_io_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata,
                                         void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    emote_gen_player_handle_t handle = (emote_gen_player_handle_t)user_ctx;
    if (handle) {
        emote_gen_player_notify_flush_finished(handle);
    }
    return true;
}
#elif CONFIG_IDF_TARGET_ESP32P4
static bool test_flush_dpi_panel_ready_callback(esp_lcd_panel_handle_t panel_io,
                                                esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    emote_gen_player_handle_t handle = (emote_gen_player_handle_t)user_ctx;
    if (handle) {
        emote_gen_player_notify_flush_finished(handle);
    }
    return true;
}
#endif

static void test_display_init(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = (BSP_LCD_H_RES * 100) * sizeof(uint16_t),
    };
    bsp_display_new(&bsp_disp_cfg, &s_panel_handle, &s_io_handle);
    esp_lcd_panel_disp_on_off(s_panel_handle, true);
#elif CONFIG_IDF_TARGET_ESP32P4
    const bsp_display_config_t bsp_disp_cfg = {
        .hdmi_resolution = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .dsi_bus = {
            .phy_clk_src = 0,
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        },
    };
    bsp_display_new(&bsp_disp_cfg, &s_panel_handle, &s_io_handle);
#endif
    bsp_display_backlight_on();
}

static esp_err_t test_touch_bsp_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "bsp_i2c_init failed");
    esp_err_t ret = bsp_touch_new(NULL, &s_touch_handle);
    if (ret != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG, "bsp_touch_new failed");
        s_touch_handle = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void test_graphics_runtime_cleanup(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (s_panel_handle != NULL) {
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = NULL;
    }
    if (s_io_handle != NULL) {
        esp_lcd_panel_io_del(s_io_handle);
        s_io_handle = NULL;
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
#elif CONFIG_IDF_TARGET_ESP32P4
    bsp_display_delete();
    bsp_touch_delete();
    s_panel_handle = NULL;
    s_io_handle = NULL;
#endif
    if (s_touch_handle != NULL) {
        esp_lcd_touch_del(s_touch_handle);
        s_touch_handle = NULL;
    }
    bsp_i2c_deinit();
    vTaskDelay(pdMS_TO_TICKS(200));
}

esp_err_t test_app_runtime_open(test_app_runtime_t *runtime, test_app_partition_type_t partition_type)
{
    const char *partition_label = test_app_get_partition_label(partition_type);
    esp_err_t ret;
    emote_gen_player_config_t player_cfg = {
        .flags = {
#if CONFIG_IDF_TARGET_ESP32S3
            .swap = true,
#elif CONFIG_IDF_TARGET_ESP32P4
            .swap = false,
#endif
            .double_buffer = true,
            .buff_dma = true,
            .buff_spiram = false,
        },
        .gfx_emote = {
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = BSP_LCD_H_RES * 16,
        },
        .task = {
            .task_priority = 7,
            .task_stack = 5 * 1024,
            .task_affinity = 0,
            .task_stack_in_ext = false,
        },
        .flush_cb = test_player_flush_ready_callback,
        .update_cb = test_player_update_callback,
    };
    emote_gen_player_data_t assets = {
        .type = EMOTE_GEN_PLAYER_SOURCE_PARTITION,
        .source = {
            .partition_label = partition_label,
        },
        .flags = {
            .mmap_enable = 1,
        },
    };

    ESP_RETURN_ON_FALSE(runtime != NULL, ESP_ERR_INVALID_ARG, TAG, "runtime is NULL");
    ESP_RETURN_ON_FALSE(partition_label != NULL && partition_label[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "partition_type is invalid");

    runtime->player = NULL;
    s_current_player = NULL;
    test_display_init();
    ret = test_touch_bsp_init();
    if (ret != ESP_OK) {
        test_graphics_runtime_cleanup();
        return ret;
    }
    runtime->player = emote_gen_player_init(&player_cfg);
    if (runtime->player == NULL) {
        test_graphics_runtime_cleanup();
        return ESP_FAIL;
    }
    s_current_player = runtime->player;
    gfx_disp_t *disp = emote_gen_player_get_disp(runtime->player);
    if (disp == NULL) {
        ESP_LOGE(TAG, "player disp is NULL");
        test_app_runtime_close(runtime);
        return ESP_FAIL;
    }

    gfx_handle_t gfx_handle = emote_gen_player_get_gfx_handle(runtime->player);
    if (gfx_handle == NULL) {
        ESP_LOGE(TAG, "player gfx_handle is NULL");
        test_app_runtime_close(runtime);
        return ESP_FAIL;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = test_flush_io_ready_callback,
    };
    esp_lcd_panel_io_register_event_callbacks(s_io_handle, &cbs, runtime->player);
#elif CONFIG_IDF_TARGET_ESP32P4
    esp_lcd_dpi_panel_event_callbacks_t cbs = {0};
    cbs.on_color_trans_done = test_flush_dpi_panel_ready_callback;
    esp_lcd_dpi_panel_register_event_callbacks(s_panel_handle, &cbs, runtime->player);
#endif

    gfx_touch_config_t touch_cfg = {
        .handle = s_touch_handle,
        .event_cb = test_player_touch_event_cb,
        .disp = disp,
        .poll_ms = 50,
        .user_data = gfx_handle,
    };
    if (gfx_touch_add(gfx_handle, &touch_cfg) == NULL) {
        ESP_LOGE(TAG, "Failed to add touch");
        test_app_runtime_close(runtime);
        return ESP_FAIL;
    }

    ret = emote_gen_player_mount_assets(runtime->player, &assets);
    if (ret != ESP_OK) {
        test_app_runtime_close(runtime);
        return ret;
    }

    ESP_LOGI("", "open success: player:%p, disp:%p", runtime->player, disp);
    return ESP_OK;
}

void test_app_runtime_close(test_app_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    test_app_set_touch_press_cb(NULL, NULL);
    emote_gen_player_deinit(runtime->player);
    runtime->player = NULL;
    s_current_player = NULL;
    test_graphics_runtime_cleanup();
}

void test_app_log_case(const char *tag, const char *case_name)
{
    ESP_LOGI(tag, "=== %s ===", case_name ? case_name : "case");
}
