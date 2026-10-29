/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "board_app.h"

#define LCD_CMD_BRIGHTNESS    (0x51)

static const char *TAG = "board_app";

static dev_display_lcd_handles_t *s_lcd;
static dev_lcd_touch_handles_t *s_touch;
static lv_indev_t *s_indev;
static uint8_t s_brightness;

esp_err_t board_app_init(void)
{
    ESP_RETURN_ON_ERROR(esp_board_manager_init(), TAG, "Board Manager init failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&s_lcd),
                        TAG, "display_lcd handle missing");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, (void **)&s_touch),
                        TAG, "lcd_touch handle missing");
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_get_handle(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_board_manager_get_periph_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER, (void **)&bus);
    return bus;
}

lv_display_t *board_display_start(const esp_lv_adapter_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && s_lcd && s_touch, NULL, TAG, "board_app_init() must run first");

    dev_display_lcd_config_t *lcd_cfg = NULL;
    ESP_RETURN_ON_FALSE(esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg) == ESP_OK,
                        NULL, TAG, "display_lcd config missing");
    ESP_RETURN_ON_FALSE(lcd_cfg->lcd_width == BOARD_LCD_H_RES && lcd_cfg->lcd_height == BOARD_LCD_V_RES &&
                        lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs == BOARD_LCD_DPI_BUFFER_NUMS,
                        NULL, TAG, "board_app.h geometry does not match display_lcd (%ux%u, %u fbs)",
                        lcd_cfg->lcd_width, lcd_cfg->lcd_height, (unsigned)lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs);

    ESP_RETURN_ON_FALSE(esp_lv_adapter_init(cfg) == ESP_OK, NULL, TAG, "LVGL adapter init failed");

    /* The panel's native orientation is upright. The lcd_touch mirror flags in the
     * board definition map touch to this orientation, and the video player
     * writes frame buffers unrotated, so LVGL must not rotate either. */
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        s_lcd->panel_handle, s_lcd->io_handle, BOARD_LCD_H_RES, BOARD_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    disp_cfg.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, NULL, TAG, "Register display failed");

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, s_touch->touch_handle);
    s_indev = esp_lv_adapter_register_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(s_indev, NULL, TAG, "Register touch failed");

    ESP_RETURN_ON_FALSE(esp_lv_adapter_start() == ESP_OK, NULL, TAG, "Start LVGL adapter failed");
    return disp;
}

bool board_display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

void board_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

esp_err_t board_display_brightness_set(int brightness_percent)
{
    ESP_RETURN_ON_FALSE(s_lcd, ESP_ERR_INVALID_STATE, TAG, "Display not initialized");
    ESP_RETURN_ON_FALSE(brightness_percent >= 0 && brightness_percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "Brightness percent invalid (0~100)");

    uint8_t param = (uint8_t)(brightness_percent * 255 / 100);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_lcd->io_handle, LCD_CMD_BRIGHTNESS, &param, 1), TAG,
                        "Send brightness command failed");
    s_brightness = param;
    return ESP_OK;
}

int board_display_brightness_get(void)
{
    return s_brightness * 100 / 255;
}

esp_err_t board_display_backlight_on(void)
{
    return board_display_brightness_set(100);
}

esp_err_t board_display_backlight_off(void)
{
    return board_display_brightness_set(0);
}

esp_lcd_panel_handle_t board_display_get_panel_handle(void)
{
    return s_lcd ? s_lcd->panel_handle : NULL;
}

esp_lcd_touch_handle_t board_display_get_touch_handle(void)
{
    return s_touch ? s_touch->touch_handle : NULL;
}

lv_indev_t *board_display_get_input_dev(void)
{
    return s_indev;
}
