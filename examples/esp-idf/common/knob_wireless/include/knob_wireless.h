/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the Board Manager wireless custom device. */
esp_err_t knob_wireless_init(void);

/** Return the wireless coprocessor MAC after successful initialization. */
uint8_t *knob_wireless_get_mac(void);

#ifdef __cplusplus
}
#endif
