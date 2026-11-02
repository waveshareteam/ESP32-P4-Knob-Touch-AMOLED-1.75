/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Prepare the DRV8837 vibration motor from the Board Manager GPIOs
 */
esp_err_t haptic_service_init(void);

/**
 * @brief  Vibrate in a pattern without blocking
 *
 *         `pattern_ms` alternates on and off times, starting with on. Each on time is
 *         limited to HAPTIC_MAX_ON_MS to protect the motor. A new pattern replaces one
 *         that is still running.
 */
esp_err_t haptic_play(const uint16_t *pattern_ms, size_t count);

void haptic_stop(void);

/**
 * @brief  Whether each knob detent vibrates; saved across resets, on by default
 */
bool haptic_knob_enabled(void);

void haptic_set_knob_enabled(bool enabled);

#define HAPTIC_KNOB_STRENGTHS   3

/**
 * @brief  How strong knob clicks are, from 1 (light) to HAPTIC_KNOB_STRENGTHS (strong);
 *         saved across resets, medium by default
 */
int haptic_knob_strength(void);

void haptic_set_knob_strength(int strength);

/**
 * @brief  One click for a knob detent, when knob vibration is on
 *
 *         It is skipped while a click is still playing, and never cuts into a pattern from
 *         haptic_play().
 */
void haptic_knob_tick(void);

/**
 * @brief  One click at the current strength right away, replacing a click still playing
 */
void haptic_knob_preview(void);

#define HAPTIC_MAX_ON_MS    500
#define HAPTIC_MAX_STEPS    16

#ifdef __cplusplus
}
#endif
