/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include "lvgl.h"

namespace esp_brookesia::apps {

/**
 * @brief Types a Wi-Fi password with the knob
 *
 * A keyboard would leave keys a few millimetres wide on this screen, so characters sit on a
 * wheel instead: the knob turns it, a tap on the big character types it, a tap on a smaller
 * one moves to it. Four buttons switch between letters, capitals, digits and symbols.
 */
class PasswordView {
public:
    /* password is nullptr when the view was cancelled */
    using DoneCb = void (*)(void *ctx, const char *password);

    PasswordView(lv_obj_t *parent, const char *ssid, DoneCb done_cb, void *ctx);
    ~PasswordView();

    void knob(int steps);

private:
    static constexpr int SLOTS = 7;
    static constexpr int SETS = 4;

    lv_obj_t *_root = nullptr;
    lv_obj_t *_field = nullptr;
    lv_obj_t *_error = nullptr;
    lv_obj_t *_slots[SLOTS] = {};
    lv_obj_t *_set_buttons[SETS] = {};
    DoneCb _done_cb;
    void *_ctx;
    std::string _text;
    int _set = 0;
    int _index[SETS] = {};      // Each set keeps its own position

    void render();
    void move(int steps);
    void type();
    void finish(bool join);

    static void slot_cb(lv_event_t *e);
    static void set_cb(lv_event_t *e);
    static void button_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
