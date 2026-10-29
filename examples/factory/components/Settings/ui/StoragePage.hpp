/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

/**
 * @brief The microSD card: its state, a rescan, and opening it on a computer over USB
 */
class StoragePage: public SettingsPage {
public:
    const char *title() const override
    {
        return "Storage";
    }
    const void *icon() const override;
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    bool back() override;
    void knob(int steps) override;
    void pause() override;

private:
    lv_obj_t *_column = nullptr;
    lv_obj_t *_card = nullptr;
    lv_obj_t *_rescan = nullptr;
    lv_obj_t *_usb_switch = nullptr;
    lv_obj_t *_usb = nullptr;
    lv_obj_t *_dialog = nullptr;
    lv_timer_t *_timer = nullptr;

    void refresh();
    void stopUsbDrive();

    static void timer_cb(lv_timer_t *timer);
    static void rescan_cb(lv_event_t *e);
    static void usb_switch_cb(lv_event_t *e);
    static void dialog_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
