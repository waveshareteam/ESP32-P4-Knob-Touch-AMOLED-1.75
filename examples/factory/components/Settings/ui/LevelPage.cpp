/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdio>
#include "round_ui.hpp"
#include "LevelPage.hpp"

namespace esp_brookesia::apps {

static constexpr int ARC_SIZE = 384;
static constexpr int ARC_WIDTH = 18;
/* Clockwise from 3 o'clock; the gap at the top leaves room for the title */
static constexpr int ARC_START = 310;
static constexpr int ARC_END = 230;
static constexpr int KNOB_STEP = 5;
static constexpr uint32_t FOLLOW_MS = 300;

LevelPage::LevelPage(const Config &config) :
    _config(config)
{
}

std::string LevelPage::summary() const
{
    char text[8];
    snprintf(text, sizeof(text), "%d%%", _config.get());
    return text;
}

void LevelPage::open(lv_obj_t *root)
{
    _arc = lv_arc_create(root);
    lv_obj_set_size(_arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(_arc);
    lv_arc_set_bg_angles(_arc, ARC_START, ARC_END);
    lv_arc_set_range(_arc, _config.min, 100);
    lv_obj_set_style_arc_width(_arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(0x2A2B35), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(round_ui::COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_arc, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(_arc, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, this);
    // Only the ring takes touches, not the square around it
    lv_obj_add_flag(_arc, LV_OBJ_FLAG_ADV_HITTEST);

    // Straight on the root: a scaled label draws past its own size, which a container would clip
    lv_obj_t *symbol = round_ui::create_label(root, &lv_font_montserrat_36, round_ui::COLOR_ACCENT);
    lv_label_set_text(symbol, _config.symbol);
    lv_obj_align(symbol, LV_ALIGN_CENTER, 0, -72);
    _value = round_ui::create_label(root, &lv_font_montserrat_44, round_ui::COLOR_TEXT);
    // Scaled up to read from a distance, like the other big numbers on this screen
    lv_obj_set_style_transform_scale(_value, 360, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(_value, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(_value, lv_pct(50), LV_PART_MAIN);
    lv_obj_align(_value, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t *caption = round_ui::create_label(root, &lv_font_montserrat_20, round_ui::COLOR_HINT);
    lv_label_set_text(caption, _config.caption);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, 74);

    const int value = std::clamp(_config.get(), _config.min, 100);
    lv_arc_set_value(_arc, value);
    showValue(value);
    if (_config.follow) {
        _follow_timer = lv_timer_create(follow_timer_cb, FOLLOW_MS, this);
    }
}

void LevelPage::close()
{
    if (_follow_timer) {
        lv_timer_delete(_follow_timer);
        _follow_timer = nullptr;
    }
    _arc = _value = nullptr;
}

void LevelPage::knob(int steps)
{
    if (_arc) {
        apply(lv_arc_get_value(_arc) + steps * KNOB_STEP, true);
    }
}

void LevelPage::apply(int percent, bool move_arc)
{
    percent = std::clamp(percent, _config.min, 100);
    if (move_arc) {
        lv_arc_set_value(_arc, percent);
    }
    _config.set(percent);
    showValue(percent);
}

void LevelPage::showValue(int percent)
{
    lv_label_set_text_fmt(_value, "%d%%", percent);
}

void LevelPage::arc_event_cb(lv_event_t *e)
{
    LevelPage *page = static_cast<LevelPage *>(lv_event_get_user_data(e));
    page->apply(lv_arc_get_value(page->_arc), false);
}

void LevelPage::follow_timer_cb(lv_timer_t *timer)
{
    LevelPage *page = static_cast<LevelPage *>(lv_timer_get_user_data(timer));
    if (!page->_arc || lv_obj_has_state(page->_arc, LV_STATE_PRESSED)) {
        return;
    }
    const int value = std::clamp(page->_config.get(), page->_config.min, 100);
    if (value != lv_arc_get_value(page->_arc)) {
        lv_arc_set_value(page->_arc, value);
        page->showValue(value);
    }
}

} // namespace esp_brookesia::apps
