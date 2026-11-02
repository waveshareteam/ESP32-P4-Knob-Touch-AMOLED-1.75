/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstring>
#include "app_fonts.h"
#include "round_ui.hpp"
#include "PasswordView.hpp"

namespace esp_brookesia::apps {

using namespace round_ui;

static constexpr const char *SET_NAMES[] = {"abc", "ABC", "123", "#+="};
static constexpr const char *SET_CHARS[] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    "!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~ ",
};
static constexpr size_t MIN_LENGTH = 8;         // WPA2 passwords are 8 to 63 characters
static constexpr size_t MAX_LENGTH = 63;
static constexpr size_t SHOWN_TAIL = 16;        // The field shows the end of a long password
static constexpr int SLOT_SPACING = 52;
static constexpr int WHEEL_Y = 248;
static constexpr int BIG_SLOT = 76;

enum Button : uintptr_t {
    BUTTON_CANCEL,
    BUTTON_DELETE,
    BUTTON_JOIN,
};

static int set_length(int set)
{
    return static_cast<int>(strlen(SET_CHARS[set]));
}

PasswordView::PasswordView(lv_obj_t *parent, const char *ssid, DoneCb done_cb, void *ctx) :
    _done_cb(done_cb), _ctx(ctx)
{
    _root = create_plain(parent);
    lv_obj_set_size(_root, SCREEN, SCREEN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);      // Covers the page under it

    lv_obj_t *heading = create_label(_root, &lv_font_montserrat_18, COLOR_HINT);
    lv_label_set_text(heading, "Password for");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_t *name = create_label(_root, app_font_text_20(), COLOR_TEXT);
    lv_obj_set_width(name, 280);
    lv_obj_set_style_max_height(name, lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(name, ssid);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *field = create_plain(_root);
    lv_obj_set_size(field, 320, 52);
    lv_obj_align(field, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_radius(field, 26, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(field, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    _field = create_label(field, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_center(_field);
    _error = create_label(_root, &lv_font_montserrat_18, COLOR_DANGER);
    lv_label_set_text(_error, "At least 8 characters");
    lv_obj_align(_error, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_add_flag(_error, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < SLOTS; i++) {
        lv_obj_t *slot = create_plain(_root);
        const int offset = i - SLOTS / 2;
        const int size = offset == 0 ? BIG_SLOT : 46;
        lv_obj_set_size(slot, size, size);
        lv_obj_align(slot, LV_ALIGN_TOP_MID, offset * SLOT_SPACING + (offset > 0 ? 12 : offset < 0 ? -12 : 0),
                     WHEEL_Y - size / 2);
        lv_obj_set_style_radius(slot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(slot, reinterpret_cast<void *>(static_cast<intptr_t>(offset)));
        lv_obj_add_event_cb(slot, slot_cb, LV_EVENT_CLICKED, this);
        if (offset == 0) {
            lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(slot, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
            lv_obj_set_style_bg_color(slot, lv_color_hex(0x2C7BD6), LV_STATE_PRESSED);
        }
        const int distance = offset < 0 ? -offset : offset;
        lv_obj_t *label = create_label(slot, offset == 0 ? &lv_font_montserrat_40 : &lv_font_montserrat_26,
                                       COLOR_TEXT);
        lv_obj_set_style_text_opa(label, distance == 0 ? LV_OPA_COVER : distance == 1 ? LV_OPA_70 :
                                  distance == 2 ? LV_OPA_40 : LV_OPA_20, LV_PART_MAIN);
        lv_obj_center(label);
        _slots[i] = slot;
    }

    lv_obj_t *sets = create_plain(_root);
    lv_obj_set_size(sets, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sets, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sets, 8, LV_PART_MAIN);
    lv_obj_align(sets, LV_ALIGN_TOP_MID, 0, 302);
    for (int i = 0; i < SETS; i++) {
        lv_obj_t *button = create_plain(sets);
        lv_obj_set_size(button, 66, 36);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(button, set_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = create_label(button, &lv_font_montserrat_18, COLOR_TEXT);
        lv_label_set_text(label, SET_NAMES[i]);
        lv_obj_center(label);
        _set_buttons[i] = button;
    }

    lv_obj_t *buttons = create_plain(_root);
    lv_obj_set_size(buttons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 12, LV_PART_MAIN);
    lv_obj_align(buttons, LV_ALIGN_TOP_MID, 0, 354);
    static constexpr const char *BUTTON_TEXT[] = {LV_SYMBOL_CLOSE, LV_SYMBOL_BACKSPACE, "Join"};
    for (uintptr_t i = BUTTON_CANCEL; i <= BUTTON_JOIN; i++) {
        lv_obj_t *button = create_plain(buttons);
        lv_obj_set_size(button, i == BUTTON_JOIN ? 120 : 56, 56);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(i == BUTTON_JOIN ? COLOR_ACCENT : COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(i == BUTTON_JOIN ? 0x2C7BD6 : COLOR_CARD_PRESSED),
                                  LV_STATE_PRESSED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(button, reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(button, button_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = create_label(button, &lv_font_montserrat_22, COLOR_TEXT);
        lv_label_set_text(label, BUTTON_TEXT[i]);
        lv_obj_center(label);
    }
    render();
}

PasswordView::~PasswordView()
{
    lv_obj_delete(_root);
}

void PasswordView::knob(int steps)
{
    move(steps);
}

void PasswordView::render()
{
    if (_text.empty()) {
        lv_obj_set_style_text_color(_field, lv_color_hex(COLOR_HINT), LV_PART_MAIN);
        lv_obj_set_style_text_font(_field, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_label_set_text(_field, "Turn the knob, tap to type");
    } else {
        lv_obj_set_style_text_color(_field, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(_field, &lv_font_montserrat_24, LV_PART_MAIN);
        std::string shown = _text.size() > SHOWN_TAIL ? "..." + _text.substr(_text.size() - SHOWN_TAIL) : _text;
        lv_label_set_text(_field, (shown + "|").c_str());
    }
    const int length = set_length(_set);
    for (int i = 0; i < SLOTS; i++) {
        const int offset = i - SLOTS / 2;
        const char c = SET_CHARS[_set][((_index[_set] + offset) % length + length) % length];
        lv_obj_t *label = lv_obj_get_child(_slots[i], 0);
        if (c == ' ') {
            lv_label_set_text(label, offset == 0 ? "space" : "_");
            lv_obj_set_style_text_font(label, offset == 0 ? &lv_font_montserrat_20 : &lv_font_montserrat_26,
                                       LV_PART_MAIN);
        } else {
            const char text[2] = {c, '\0'};
            lv_label_set_text(label, text);
            lv_obj_set_style_text_font(label, offset == 0 ? &lv_font_montserrat_40 : &lv_font_montserrat_26,
                                       LV_PART_MAIN);
        }
    }
    for (int i = 0; i < SETS; i++) {
        lv_obj_set_style_bg_color(_set_buttons[i], lv_color_hex(i == _set ? COLOR_ACCENT : COLOR_CARD), LV_PART_MAIN);
    }
}

void PasswordView::move(int steps)
{
    const int length = set_length(_set);
    _index[_set] = ((_index[_set] + steps) % length + length) % length;
    render();
}

void PasswordView::type()
{
    if (_text.size() < MAX_LENGTH) {
        _text.push_back(SET_CHARS[_set][_index[_set]]);
        lv_obj_add_flag(_error, LV_OBJ_FLAG_HIDDEN);
        render();
    }
}

void PasswordView::finish(bool join)
{
    if (join && _text.size() < MIN_LENGTH) {
        lv_obj_remove_flag(_error, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // The callback usually deletes this view
    _done_cb(_ctx, join ? _text.c_str() : nullptr);
}

void PasswordView::slot_cb(lv_event_t *e)
{
    PasswordView *view = static_cast<PasswordView *>(lv_event_get_user_data(e));
    const int offset = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e))));
    if (offset == 0) {
        view->type();
    } else {
        view->move(offset);
    }
}

void PasswordView::set_cb(lv_event_t *e)
{
    PasswordView *view = static_cast<PasswordView *>(lv_event_get_user_data(e));
    view->_set = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e))));
    view->render();
}

void PasswordView::button_cb(lv_event_t *e)
{
    PasswordView *view = static_cast<PasswordView *>(lv_event_get_user_data(e));
    switch (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)))) {
    case BUTTON_CANCEL:
        view->finish(false);
        break;
    case BUTTON_DELETE:
        if (!view->_text.empty()) {
            view->_text.pop_back();
            view->render();
        }
        break;
    case BUTTON_JOIN:
        view->finish(true);
        break;
    default:
        break;
    }
}

} // namespace esp_brookesia::apps
