/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

/**
 * @brief What the device is and what it runs
 */
class AboutPage: public SettingsPage {
public:
    const char *title() const override
    {
        return "About";
    }
    const void *icon() const override;
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    void knob(int steps) override;

private:
    lv_obj_t *_column = nullptr;
};

} // namespace esp_brookesia::apps
