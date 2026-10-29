/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SERVICE_SSID_MAX       33      /*!< 32 bytes and the terminator */
#define WIFI_SERVICE_PASSWORD_MAX   64
#define WIFI_SERVICE_SCAN_MAX       16

typedef enum {
    WIFI_SERVICE_OFF,           /*!< Switched off */
    WIFI_SERVICE_IDLE,          /*!< On, but no network saved or the last attempt gave up */
    WIFI_SERVICE_CONNECTING,
    WIFI_SERVICE_CONNECTED,     /*!< Has an IP address */
} wifi_service_state_t;

typedef enum {
    WIFI_SERVICE_FAIL_NONE,
    WIFI_SERVICE_FAIL_PASSWORD, /*!< The network turned the password down */
    WIFI_SERVICE_FAIL_NOT_FOUND,/*!< The network is not in range */
    WIFI_SERVICE_FAIL_OTHER,
} wifi_service_fail_t;

typedef struct {
    wifi_service_state_t state;
    wifi_service_fail_t fail;   /*!< Why the last attempt failed, while IDLE */
    char ssid[WIFI_SERVICE_SSID_MAX];   /*!< Network in use, or the saved one; empty when none is saved */
    int8_t rssi;                /*!< Signal while connected, in dBm */
    char ip[16];                /*!< While connected */
} wifi_service_info_t;

typedef struct {
    char ssid[WIFI_SERVICE_SSID_MAX];
    int8_t rssi;
    bool secure;                /*!< Needs a password */
} wifi_service_ap_t;

/**
 * @brief  Bring up the Wi-Fi driver and switch it on again if it was on before
 *
 *         The co-processor link must be up. With Wi-Fi on, the saved network is joined and
 *         joined again whenever the connection drops; the clock is set from the network once
 *         there is an IP address.
 */
esp_err_t wifi_service_init(void);

/**
 * @brief  Switch Wi-Fi on or off; kept across reboots
 */
esp_err_t wifi_service_set_enabled(bool enabled);

void wifi_service_get_info(wifi_service_info_t *info);

/**
 * @brief  Look for networks in the background; wifi_service_get_scan() has them when done
 */
esp_err_t wifi_service_scan(void);

bool wifi_service_is_scanning(void);

/**
 * @brief  Networks from the last scan, strongest first, one entry per name
 *
 * @param  generation  Set to a number that changes with every finished scan
 * @return Number of networks copied
 */
int wifi_service_get_scan(wifi_service_ap_t *aps, int max, uint32_t *generation);

/**
 * @brief  Join a network and save it for later; password may be NULL for open networks
 */
esp_err_t wifi_service_connect(const char *ssid, const char *password);

/**
 * @brief  Leave the saved network and forget it
 */
esp_err_t wifi_service_forget(void);

#ifdef __cplusplus
}
#endif
