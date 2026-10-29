/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include "haptic_service.h"
#include "round_ui.hpp"
#include "VibrationPage.hpp"

LV_IMG_DECLARE(vibration);

namespace esp_brookesia::apps {

static const char *const STRENGTHS[HAPTIC_KNOB_STRENGTHS] = {"Light", "Medium", "Strong"};

const void *VibrationPage::icon() const
{
    return &vibration;
}

std::string VibrationPage::summary() const
{
    return haptic_knob_enabled() ? STRENGTHS[haptic_knob_strength() - 1] : "Off";
}

void VibrationPage::open(lv_obj_t *root)
{
    _column = round_ui::create_column(root);
    _switch = round_ui::create_switch_row(_column, &vibration, "Knob clicks", switch_cb, this).control;
    round_ui::create_section(_column, "Strength");
    _strength = round_ui::create_choice(_column, STRENGTHS, HAPTIC_KNOB_STRENGTHS, strength_cb, this);
    _note = round_ui::create_note(_column);
    round_ui::set_switch(_switch, haptic_knob_enabled());
    round_ui::set_choice(_strength, haptic_knob_strength() - 1);
    refresh();
}

void VibrationPage::close()
{
    _column = _switch = _strength = _note = nullptr;
}

void VibrationPage::knob(int steps)
{
    // The knob picks the strength, and each step already clicks, so it is felt right away
    if (haptic_knob_enabled()) {
        setStrength(std::clamp(haptic_knob_strength() + (steps > 0 ? 1 : -1), 1, HAPTIC_KNOB_STRENGTHS));
    }
}

void VibrationPage::setStrength(int strength)
{
    if (strength == haptic_knob_strength()) {
        return;
    }
    haptic_set_knob_strength(strength);
    round_ui::set_choice(_strength, strength - 1);
    haptic_knob_preview();
}

void VibrationPage::refresh()
{
    const bool on = haptic_knob_enabled();
    round_ui::set_enabled(_strength, on);
    lv_label_set_text(_note, on ? "Every step of the knob clicks, in every app. Turn the knob here to try the strengths."
                      : "The knob turns without clicks. Pomodoro alerts still vibrate.");
}

void VibrationPage::switch_cb(lv_event_t *e)
{
    VibrationPage *page = static_cast<VibrationPage *>(lv_event_get_user_data(e));
    const bool on = lv_obj_has_state(page->_switch, LV_STATE_CHECKED);
    haptic_set_knob_enabled(on);
    if (on) {
        haptic_knob_preview();
    }
    page->refresh();
}

void VibrationPage::strength_cb(lv_event_t *e)
{
    VibrationPage *page = static_cast<VibrationPage *>(lv_event_get_user_data(e));
    const int strength = round_ui::get_choice(page->_strength) + 1;
    haptic_set_knob_strength(strength);
    haptic_knob_preview();
}

} // namespace esp_brookesia::apps
