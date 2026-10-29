/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

/**
 * @brief Bluetooth audio from a phone: on or off, the connection, and forgetting paired phones
 */
class BluetoothPage: public SettingsPage {
public:
    const char *title() const override
    {
        return "Bluetooth";
    }
    const void *icon() const override;
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    bool back() override;
    void knob(int steps) override;

private:
    lv_obj_t *_column = nullptr;
    lv_obj_t *_switch = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_disconnect = nullptr;
    lv_obj_t *_forget = nullptr;
    lv_obj_t *_dialog = nullptr;
    lv_timer_t *_timer = nullptr;

    void refresh();

    static void timer_cb(lv_timer_t *timer);
    static void switch_cb(lv_event_t *e);
    static void disconnect_cb(lv_event_t *e);
    static void forget_cb(lv_event_t *e);
    static void dialog_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
