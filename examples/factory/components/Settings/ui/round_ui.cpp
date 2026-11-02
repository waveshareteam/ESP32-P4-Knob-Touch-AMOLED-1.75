/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdint>
#include "app_fonts.h"
#include "round_ui.hpp"

namespace esp_brookesia::apps::round_ui {

static constexpr int ROW_PAD_HOR = 20;
static constexpr int ROW_PAD_VER = 14;
static constexpr int ICON_GAP = 14;
static constexpr int DIALOG_WIDTH = 330;

lv_obj_t *create_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return obj;
}

lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static void style_pressable(lv_obj_t *obj, uint32_t color, uint32_t pressed)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
}

lv_obj_t *create_title(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb, void *user_data)
{
    lv_obj_t *pill = create_plain(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, TITLE_HEIGHT);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 22, LV_PART_MAIN);
    style_pressable(pill, COLOR_CARD, COLOR_CARD_PRESSED);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, TITLE_Y);
    lv_obj_add_event_cb(pill, back_cb, LV_EVENT_CLICKED, user_data);
    // A finger that just misses the pill still goes back
    lv_obj_set_ext_click_area(pill, 12);
    lv_obj_t *label = create_label(pill, &lv_font_montserrat_22, COLOR_TEXT);
    lv_label_set_text_fmt(label, LV_SYMBOL_LEFT "  %s", title);
    lv_obj_center(label);
    return pill;
}

lv_obj_t *create_column(lv_obj_t *parent)
{
    lv_obj_t *column = create_plain(parent);
    lv_obj_set_size(column, SCREEN, SCREEN - COLUMN_TOP);
    lv_obj_set_pos(column, 0, COLUMN_TOP);
    lv_obj_add_flag(column, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(column, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(column, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(column, 8, LV_PART_MAIN);
    // The last card can scroll up to where the circle is wide
    lv_obj_set_style_pad_bottom(column, SCREEN / 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(column, 10, LV_PART_MAIN);
    return column;
}

void scroll_column(lv_obj_t *column, int steps)
{
    if (column) {
        lv_obj_scroll_by_bounded(column, 0, -steps * KNOB_SCROLL, LV_ANIM_ON);
    }
}

static lv_obj_t *create_card(lv_obj_t *column)
{
    lv_obj_t *card = create_plain(column);
    lv_obj_set_size(card, CARD_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, ROW_PAD_HOR, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(card, ROW_PAD_VER, LV_PART_MAIN);
    lv_obj_set_style_opa(card, LV_OPA_40, LV_STATE_DISABLED);
    return card;
}

static void add_icon(lv_obj_t *row, const void *icon)
{
    if (!icon) {
        return;
    }
    lv_obj_t *obj;
    if (lv_image_src_get_type(icon) == LV_IMAGE_SRC_SYMBOL) {
        obj = create_label(row, &lv_font_montserrat_24, COLOR_ACCENT);
        lv_label_set_text(obj, static_cast<const char *>(icon));
    } else {
        obj = lv_image_create(row);
        lv_image_set_src(obj, icon);
    }
    lv_obj_set_width(obj, 32);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

Row create_row(lv_obj_t *column, const void *icon, const char *title, const char *value, bool chevron)
{
    Row row;
    row.obj = create_card(column);
    lv_obj_set_flex_flow(row.obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row.obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row.obj, ICON_GAP, LV_PART_MAIN);
    add_icon(row.obj, icon);

    lv_obj_t *texts = create_plain(row.obj);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 2, LV_PART_MAIN);
    row.title = create_label(texts, &lv_font_montserrat_22, COLOR_TEXT);
    lv_obj_set_width(row.title, lv_pct(100));
    lv_label_set_long_mode(row.title, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(row.title, title);
    // Network and phone names may be Chinese
    row.value = create_label(texts, app_font_text_20(), COLOR_HINT);
    lv_obj_set_width(row.value, lv_pct(100));
    lv_label_set_long_mode(row.value, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_height(row.value, lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
    lv_label_set_text(row.value, value ? value : "");
    if (!value || !value[0]) {
        lv_obj_add_flag(row.value, LV_OBJ_FLAG_HIDDEN);
    }
    if (chevron) {
        row.control = create_label(row.obj, &lv_font_montserrat_20, COLOR_HINT);
        lv_label_set_text(row.control, LV_SYMBOL_RIGHT);
        style_pressable(row.obj, COLOR_CARD, COLOR_CARD_PRESSED);
    }
    return row;
}

Row create_switch_row(lv_obj_t *column, const void *icon, const char *title, lv_event_cb_t cb, void *user_data)
{
    Row row = create_row(column, icon, title, nullptr, false);
    row.control = lv_switch_create(row.obj);
    lv_obj_set_size(row.control, 64, 34);
    lv_obj_set_style_bg_color(row.control, lv_color_hex(0x3A3B47), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.control, lv_color_hex(COLOR_ACCENT),
                              static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    lv_obj_add_event_cb(row.control, cb, LV_EVENT_VALUE_CHANGED, user_data);
    return row;
}

void set_switch(lv_obj_t *sw, bool on)
{
    if (sw && on != lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        if (on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
    }
}

lv_obj_t *create_section(lv_obj_t *column, const char *text)
{
    lv_obj_t *label = create_label(column, &lv_font_montserrat_18, COLOR_HINT);
    lv_obj_set_width(label, CARD_WIDTH - 2 * ROW_PAD_HOR);
    lv_obj_set_style_margin_top(label, 8, LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *create_note(lv_obj_t *column)
{
    lv_obj_t *label = create_label(column, app_font_text_20(), COLOR_HINT);
    lv_obj_set_width(label, CARD_WIDTH - 2 * ROW_PAD_HOR);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, "");
    return label;
}

static void choice_option_cb(lv_event_t *e)
{
    lv_obj_t *option = lv_event_get_current_target_obj(e);
    lv_obj_t *choice = lv_obj_get_parent(option);
    const int index = static_cast<int>(lv_obj_get_index(option));
    if (lv_obj_has_state(choice, LV_STATE_DISABLED) || index == get_choice(choice)) {
        return;
    }
    set_choice(choice, index);
    lv_obj_send_event(choice, LV_EVENT_VALUE_CHANGED, nullptr);
}

lv_obj_t *create_choice(lv_obj_t *column, const char *const *options, int count, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *choice = create_plain(column);
    lv_obj_set_size(choice, CARD_WIDTH, 56);
    lv_obj_set_style_radius(choice, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(choice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(choice, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_opa(choice, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_pad_all(choice, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(choice, LV_FLEX_FLOW_ROW);
    lv_obj_add_event_cb(choice, cb, LV_EVENT_VALUE_CHANGED, user_data);
    for (int i = 0; i < count; i++) {
        lv_obj_t *option = create_plain(choice);
        lv_obj_set_height(option, lv_pct(100));
        lv_obj_set_flex_grow(option, 1);
        lv_obj_set_style_radius(option, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(option, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
        lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(option, choice_option_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *label = create_label(option, &lv_font_montserrat_20, COLOR_HINT);
        lv_label_set_text(label, options[i]);
        lv_obj_center(label);
    }
    lv_obj_set_user_data(choice, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    set_choice(choice, 0);
    return choice;
}

void set_choice(lv_obj_t *choice, int index)
{
    lv_obj_set_user_data(choice, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    for (uint32_t i = 0; i < lv_obj_get_child_count(choice); i++) {
        lv_obj_t *option = lv_obj_get_child(choice, i);
        const bool picked = static_cast<int>(i) == index;
        lv_obj_set_style_bg_opa(option, picked ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(option, 0), lv_color_hex(picked ? COLOR_TEXT : COLOR_HINT),
                                    LV_PART_MAIN);
    }
}

int get_choice(lv_obj_t *choice)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(choice)));
}

lv_obj_t *create_button(lv_obj_t *column, const char *symbol, const char *text, bool danger, lv_event_cb_t cb,
                        void *user_data)
{
    lv_obj_t *button = create_plain(column);
    lv_obj_set_size(button, 240, 56);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    style_pressable(button, COLOR_CARD, COLOR_CARD_PRESSED);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = create_label(button, &lv_font_montserrat_20, danger ? COLOR_DANGER : COLOR_ACCENT);
    if (symbol) {
        lv_label_set_text_fmt(label, "%s  %s", symbol, text);
    } else {
        lv_label_set_text(label, text);
    }
    lv_obj_center(label);
    return button;
}

void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (!obj) {
        return;
    }
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

static constexpr uintptr_t DIALOG_CANCEL = 0;
static constexpr uintptr_t DIALOG_CONFIRM = 1;

lv_obj_t *dialog_open(const char *title, const char *body, const char *confirm_text, bool danger, lv_event_cb_t cb,
                      void *user_data)
{
    // On the top layer, so it stays over the page and the status bar alike
    lv_obj_t *backdrop = create_plain(lv_layer_top());
    lv_obj_set_size(backdrop, SCREEN, SCREEN);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);   // Nothing under it can be tapped

    lv_obj_t *panel = create_plain(backdrop);
    lv_obj_set_size(panel, DIALOG_WIDTH, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 40, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = create_label(panel, &lv_font_montserrat_24, COLOR_TEXT);
    lv_label_set_text(title_label, title);
    lv_obj_t *body_label = create_label(panel, app_font_text_20(), COLOR_HINT);
    lv_obj_set_width(body_label, DIALOG_WIDTH - 48);
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(body_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(body_label, body);

    lv_obj_t *buttons = create_plain(panel);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 12, LV_PART_MAIN);
    const char *texts[2] = {"Cancel", confirm_text};
    for (uintptr_t i = DIALOG_CANCEL; i <= DIALOG_CONFIRM; i++) {
        lv_obj_t *button = create_plain(buttons);
        lv_obj_set_size(button, 130, 52);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        const uint32_t color = (i == DIALOG_CONFIRM) ? (danger ? COLOR_DANGER : COLOR_ACCENT) : 0x2E2F3A;
        style_pressable(button, color, i == DIALOG_CONFIRM ? color : COLOR_CARD_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_set_user_data(button, reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
        lv_obj_t *label = create_label(button, &lv_font_montserrat_20, COLOR_TEXT);
        lv_label_set_text(label, texts[i]);
        lv_obj_center(label);
    }
    return backdrop;
}

bool dialog_confirmed(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_current_target_obj(e);
    return reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)) == DIALOG_CONFIRM;
}

void dialog_close(lv_obj_t *dialog)
{
    if (dialog) {
        lv_obj_delete_async(dialog);
    }
}

} // namespace esp_brookesia::apps::round_ui
