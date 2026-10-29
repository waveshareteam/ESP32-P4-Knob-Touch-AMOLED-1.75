/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/widgets/status_bar/esp_brookesia_status_bar.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"

namespace esp_brookesia::systems::phone {

constexpr StatusBar::AreaData STYLESHEET_466_466_DARK_STATUS_BAR_AREA_DATA(int w_percent, StatusBar::AreaAlign align,
        int start_offset)
{
    return {
        .size = gui::StyleSize::RECT_PERCENT(w_percent, 100),
        .layout_column_align = align,
        .layout_column_start_offset = start_offset,
        .layout_column_pad = 4,
    };
}

/* Only about 160 px of the bar's width lie inside the round screen at the icons' height, so the
 * Wi-Fi and battery icons sit right next to the clock in the middle instead of at the ends */
constexpr int STYLESHEET_466_466_DARK_STATUS_BAR_SIDE_OFFSET = 170;

constexpr StatusBar::Data STYLESHEET_466_466_DARK_STATUS_BAR_DATA = {
    .main = {
        .size = gui::StyleSize::RECT_W_PERCENT(100, 52),
        .background_color = gui::StyleColor::COLOR_WITH_OPACITY(0x000000, 0),  // Wallpaper or app shows through
        .text_font = gui::StyleFont::SIZE(22),
        .text_color = gui::StyleColor::COLOR(0xFFFFFF),
    },
    .area = {
        .num = 3,
        .data = {
            STYLESHEET_466_466_DARK_STATUS_BAR_AREA_DATA(50, StatusBar::AreaAlign::START,
                    STYLESHEET_466_466_DARK_STATUS_BAR_SIDE_OFFSET),
            STYLESHEET_466_466_DARK_STATUS_BAR_AREA_DATA(30, StatusBar::AreaAlign::CENTER, 0),
            STYLESHEET_466_466_DARK_STATUS_BAR_AREA_DATA(50, StatusBar::AreaAlign::END,
                    STYLESHEET_466_466_DARK_STATUS_BAR_SIDE_OFFSET),
        },
    },
    .icon_common_size = gui::StyleSize::SQUARE(24),
    .battery = {
        .area_index = 2,
        .icon_data = {
            .icon = {
                .image_num = 5,
                .images = {
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_battery_level1_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_battery_level2_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_battery_level3_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_battery_level4_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_battery_charge_24_24),
                },
            },
        },
    },
    .wifi = {
        .area_index = 0,
        .icon_data = {
            .icon = {
                .image_num = 4,
                .images = {
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_wifi_close_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_wifi_level1_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_wifi_level2_24_24),
                    gui::StyleImage::IMAGE_RECOLOR_WHITE(&esp_brookesia_image_middle_status_bar_wifi_level3_24_24),
                },
            },
        },
    },
    .clock = {
        .area_index = 1,
    },
    .flags = {
        .enable_battery_icon = 1,
        .enable_battery_icon_common_size = 1,
        .enable_battery_label = 0,    // Its percentage would push the icon into the clock
        .enable_wifi_icon = 1,
        .enable_wifi_icon_common_size = 1,
        .enable_clock = 1,
        .hide_app_icons = 1,
    },
};

} // namespace esp_brookesia::systems::phone
