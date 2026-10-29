/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

/**
 * @brief Whether every knob detent clicks the vibration motor, and how strongly
 */
class VibrationPage: public SettingsPage {
public:
    const char *title() const override
    {
        return "Vibration";
    }
    const void *icon() const override;
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    void knob(int steps) override;

private:
    lv_obj_t *_column = nullptr;
    lv_obj_t *_switch = nullptr;
    lv_obj_t *_strength = nullptr;
    lv_obj_t *_note = nullptr;

    void setStrength(int strength);
    void refresh();

    static void switch_cb(lv_event_t *e);
    static void strength_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
