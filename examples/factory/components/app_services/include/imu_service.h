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

/**
 * Readings of the QMI8658 motion sensor in the screen's frame: x to the right of the screen,
 * y to its top and z out of it, so a board lying face up reads accel (0, 0, 1).
 */
typedef struct {
    float accel[3];         /*!< Acceleration in g, what an accelerometer feels: 1 g up at rest */
    float gyro[3];          /*!< Turn rate in degrees per second, the gyro offset removed */
    float up[3];            /*!< Unit vector pointing up, from accel and gyro together: steady while
                                 the board moves, and quick when it turns */
    float quat[4];          /*!< Orientation as a quaternion w, x, y, z; turns about up drift */
    float temperature;      /*!< Sensor temperature in degrees Celsius */
    uint32_t count;         /*!< Samples so far; changes with every new one */
} imu_sample_t;

/**
 * @brief  Start sampling, for as long as at least one user has started it
 *
 *         The first second also measures the gyro offset, so the board should rest
 *         meanwhile; the offset keeps being tracked whenever it lies still.
 */
esp_err_t imu_service_start(void);

/**
 * @brief  Stop sampling once every user that started it has stopped it; the sensor sleeps
 */
void imu_service_stop(void);

/**
 * @return ESP_ERR_INVALID_STATE before the first sample
 */
esp_err_t imu_service_get(imu_sample_t *sample);

#ifdef __cplusplus
}
#endif
