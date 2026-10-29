/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool present;       /*!< A battery is connected to the AXP2101 */
    bool charging;
    int percent;        /*!< Fuel gauge estimate, 0 to 100; valid while present */
} power_battery_t;

/**
 * @brief  Start the AXP2101 battery detection and fuel gauge
 *
 *         The board runs from USB without a battery as well; the battery state then reads as
 *         not present.
 */
esp_err_t power_service_init(void);

esp_err_t power_service_get_battery(power_battery_t *battery);

#ifdef __cplusplus
}
#endif
