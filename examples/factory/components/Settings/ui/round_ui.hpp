/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include "lvgl.h"
#include "board_app.h"

/* Building blocks for Settings pages on the round 466 x 466 screen. Everything stays inside the
 * circle: content scrolls in a column of cards narrow enough for the lower half of the screen,
 * below a title pill that sits where the circle is still wide enough for it. */
namespace esp_brookesia::apps::round_ui {

constexpr int SCREEN = BOARD_LCD_H_RES;
constexpr int STATUS_BAR_HEIGHT = 52;
constexpr int TITLE_Y = 58;                 // Title pill, just below the status bar
constexpr int TITLE_HEIGHT = 40;
constexpr int COLUMN_TOP = TITLE_Y + TITLE_HEIGHT + 6;
constexpr int CARD_WIDTH = 320;
constexpr int KNOB_SCROLL = 70;             // Pixels a page scrolls per knob detent

constexpr uint32_t COLOR_CARD = 0x1C1D24;
constexpr uint32_t COLOR_CARD_PRESSED = 0x2C2D38;
constexpr uint32_t COLOR_ACCENT = 0x3D9BFF;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;
constexpr uint32_t COLOR_HINT = 0x8E929E;
constexpr uint32_t COLOR_DANGER = 0xFF5A5F;

struct Row {
    lv_obj_t *obj = nullptr;
    lv_obj_t *title = nullptr;
    lv_obj_t *value = nullptr;      // Second line, may be empty
    lv_obj_t *control = nullptr;    // Switch or chevron at the end
};

lv_obj_t *create_plain(lv_obj_t *parent);
lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);

/**
 * @brief  "‹ title" at the top of a page; a tap on it goes back
 */
lv_obj_t *create_title(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb, void *user_data);

/**
 * @brief  Scrolling column of cards below the title, clipped above at the title
 */
lv_obj_t *create_column(lv_obj_t *parent);

/**
 * @brief  Scroll a column by knob detents
 */
void scroll_column(lv_obj_t *column, int steps);

/**
 * @brief  Card with an icon (image or LV_SYMBOL text, may be null), a title and an optional value line
 *
 * @param  chevron  Show "›" at the end, for rows that open something
 */
Row create_row(lv_obj_t *column, const void *icon, const char *title, const char *value, bool chevron);

/**
 * @brief  Row with a switch at the end; its LV_EVENT_VALUE_CHANGED reaches cb
 */
Row create_switch_row(lv_obj_t *column, const void *icon, const char *title, lv_event_cb_t cb, void *user_data);

void set_switch(lv_obj_t *sw, bool on);

/**
 * @brief  Small grey heading between cards
 */
lv_obj_t *create_section(lv_obj_t *column, const char *text);

/**
 * @brief  Wrapped grey text, for what a setting is doing
 */
lv_obj_t *create_note(lv_obj_t *column);

/**
 * @brief  Options side by side in one pill, one of them picked
 *
 *         A tap picks an option and sends LV_EVENT_VALUE_CHANGED to cb; a disabled choice dims
 *         and ignores taps.
 */
lv_obj_t *create_choice(lv_obj_t *column, const char *const *options, int count, lv_event_cb_t cb, void *user_data);
void set_choice(lv_obj_t *choice, int index);
int get_choice(lv_obj_t *choice);

/**
 * @brief  Pill button; disabled buttons dim
 */
lv_obj_t *create_button(lv_obj_t *column, const char *symbol, const char *text, bool danger, lv_event_cb_t cb,
                        void *user_data);

void set_enabled(lv_obj_t *obj, bool enabled);

/**
 * @brief  Confirmation over the whole screen
 *
 *         cb gets LV_EVENT_CLICKED from either button; dialog_confirmed() tells which one. The
 *         dialog stays until dialog_close().
 */
lv_obj_t *dialog_open(const char *title, const char *body, const char *confirm_text, bool danger, lv_event_cb_t cb,
                      void *user_data);
bool dialog_confirmed(lv_event_t *e);
void dialog_close(lv_obj_t *dialog);

} // namespace esp_brookesia::apps::round_ui
