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
 * @brief One page of Settings, opened from the list
 *
 * Settings draws the title and hands the page a full-screen root to build in, and deletes the
 * root when the page closes. The page owns its timers, knob use and dialogs.
 */
class SettingsPage {
public:
    virtual ~SettingsPage() = default;

    virtual const char *title() const = 0;

    /**
     * @brief  Image or LV_SYMBOL text for the list
     */
    virtual const void *icon() const = 0;

    /**
     * @brief  The current value, shown under the title in the list
     */
    virtual std::string summary() const = 0;

    virtual void open(lv_obj_t *root) = 0;

    /**
     * @brief  Stop timers and close dialogs; LVGL objects under the root go with it
     */
    virtual void close() = 0;

    /**
     * @brief  Back inside the page, such as closing a dialog
     *
     * @return false when the page itself should close
     */
    virtual bool back()
    {
        return false;
    }

    virtual void knob(int steps) = 0;

    /**
     * @brief  Settings went to the background with this page open
     */
    virtual void pause()
    {
    }
};

} // namespace esp_brookesia::apps
