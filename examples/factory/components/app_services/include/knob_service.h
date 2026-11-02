/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Called with the knob movement since the last call
 *
 *         `steps` is positive for clockwise and negative for counter-clockwise
 *         detents. It runs in the knob task with the LVGL lock held, so it may
 *         update widgets directly but must not block.
 */
typedef void (*knob_step_cb_t)(int steps, void *user_ctx);

/**
 * @brief  Start counting knob detents from the Board Manager knob GPIOs
 */
esp_err_t knob_service_init(void);

/**
 * @brief  Send knob steps to this callback until it is detached
 *
 *         Only the most recently attached callback receives steps, so a page that
 *         opens on top of another takes the knob over and hands it back on detach.
 */
esp_err_t knob_service_attach(knob_step_cb_t cb, void *user_ctx);

/**
 * @brief  Stop sending knob steps to a callback attached earlier
 */
void knob_service_detach(knob_step_cb_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
