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

/**
 * @brief  Set the time zone and take the time from the PCF85063 RTC
 *
 *         The RTC keeps counting through resets while the board has power, so the clock is right
 *         before Wi-Fi connects. An RTC that lost its time leaves the system clock unset.
 */
esp_err_t time_service_init(void);

/**
 * @brief  Whether the system clock holds a real time, from the RTC or from the network
 */
bool time_service_is_valid(void);

/**
 * @brief  Store the system time in the RTC
 */
esp_err_t time_service_save(void);

/**
 * @brief  Set the clock from the network now that there is a connection, and keep it in the RTC
 *
 *         Calling it again does nothing; SNTP keeps the clock right from then on.
 */
void time_service_start_sync(void);

#ifdef __cplusplus
}
#endif
