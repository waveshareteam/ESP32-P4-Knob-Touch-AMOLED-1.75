/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_psram.h"
#include "round_ui.hpp"
#include "AboutPage.hpp"

LV_IMG_DECLARE(info);
LV_IMG_DECLARE(logo);

namespace esp_brookesia::apps {

const void *AboutPage::icon() const
{
    return &info;
}

std::string AboutPage::summary() const
{
    return std::string("Version ") + esp_app_get_description()->version;
}

/* A grey heading over a white line, centred, so nothing reaches the curved edge */
static void add_item(lv_obj_t *column, const char *heading, const char *text)
{
    lv_obj_t *label = round_ui::create_label(column, &lv_font_montserrat_16, round_ui::COLOR_HINT);
    lv_label_set_text(label, heading);
    lv_obj_set_style_margin_top(label, 6, LV_PART_MAIN);
    label = round_ui::create_label(column, &lv_font_montserrat_20, round_ui::COLOR_TEXT);
    lv_obj_set_width(label, round_ui::CARD_WIDTH - 40);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(label, text);
}

void AboutPage::open(lv_obj_t *root)
{
    _column = round_ui::create_column(root);
    lv_obj_set_style_pad_row(_column, 2, LV_PART_MAIN);
    char text[64];

    lv_obj_t *image = lv_image_create(_column);
    lv_image_set_src(image, &logo);
    lv_obj_set_style_margin_top(image, 4, LV_PART_MAIN);
    lv_obj_t *model = round_ui::create_label(_column, &lv_font_montserrat_22, round_ui::COLOR_TEXT);
    lv_obj_set_width(model, round_ui::CARD_WIDTH - 20);
    lv_label_set_long_mode(model, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(model, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(model, "ESP32-P4 Knob Touch AMOLED 1.75");
    lv_obj_t *maker = round_ui::create_label(_column, &lv_font_montserrat_18, round_ui::COLOR_HINT);
    lv_label_set_text(maker, "Waveshare");
    lv_obj_set_style_margin_bottom(maker, 6, LV_PART_MAIN);

    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(text, sizeof(text), "%s, %s", app->version, app->date);
    add_item(_column, "Firmware", text);
    snprintf(text, sizeof(text), "ESP-IDF %s, ESP-Brookesia", esp_get_idf_version());
    add_item(_column, "Software", text);
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    snprintf(text, sizeof(text), "ESP32-P4 v%d.%d, flash %u MB, PSRAM %u MB", chip.revision / 100, chip.revision % 100,
             (unsigned)(flash_size >> 20), (unsigned)(esp_psram_get_size() >> 20));
    add_item(_column, "Hardware", text);
}

void AboutPage::close()
{
    _column = nullptr;
}

void AboutPage::knob(int steps)
{
    round_ui::scroll_column(_column, steps);
}

} // namespace esp_brookesia::apps
