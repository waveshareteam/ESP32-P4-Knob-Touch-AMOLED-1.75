/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch_cst9217.h"
#include "dev_display_lcd.h"

static const char *TAG = "SETUP_DEVICE";

/* Board Manager creates the DSI bus and the DBI panel IO, then resets and
 * initializes the panel after this factory returns. */
esp_err_t lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle, dev_display_lcd_config_t *lcd_cfg,
                                        dev_display_lcd_handles_t *lcd_handles)
{
    ESP_RETURN_ON_FALSE(dsi_handle && lcd_cfg && lcd_handles, ESP_ERR_INVALID_ARG, TAG, "invalid DSI factory arguments");

    co5300_vendor_config_t vendor_config = {
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = dsi_handle,
            .dpi_config = &lcd_cfg->sub_cfg.dsi.dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = lcd_cfg->bits_per_pixel,
        .rgb_ele_order = lcd_cfg->rgb_ele_order,
        .reset_gpio_num = lcd_cfg->sub_cfg.dsi.reset_gpio_num,
        .flags = {
            .reset_active_high = lcd_cfg->sub_cfg.dsi.reset_active_high,
        },
        .vendor_config = &vendor_config,
    };
    return esp_lcd_new_panel_co5300(lcd_handles->io_handle, &panel_config, &lcd_handles->panel_handle);
}

esp_err_t lcd_touch_factory_entry_t(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
{
    ESP_RETURN_ON_FALSE(io && touch_dev_config && ret_touch, ESP_ERR_INVALID_ARG, TAG, "invalid touch factory arguments");
    return esp_lcd_touch_new_i2c_cst9217(io, touch_dev_config, ret_touch);
}
