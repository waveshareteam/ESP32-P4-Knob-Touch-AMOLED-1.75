/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

/**
 * @brief A percentage set on an arc around the screen, by touch or by the knob
 */
class LevelPage: public SettingsPage {
public:
    struct Config {
        const char *title;
        const void *list_icon;
        const char *symbol;         // Shown large in the middle
        const char *caption;        // Under the value
        int min;
        int (*get)(void);
        void (*set)(int percent);
        bool follow;                // The value also changes elsewhere; show it as it does
    };

    explicit LevelPage(const Config &config);

    const char *title() const override
    {
        return _config.title;
    }
    const void *icon() const override
    {
        return _config.list_icon;
    }
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    void knob(int steps) override;

private:
    Config _config;
    lv_obj_t *_arc = nullptr;
    lv_obj_t *_value = nullptr;
    lv_timer_t *_follow_timer = nullptr;

    void apply(int percent, bool move_arc);
    void showValue(int percent);

    static void arc_event_cb(lv_event_t *e);
    static void follow_timer_cb(lv_timer_t *timer);
};

} // namespace esp_brookesia::apps
