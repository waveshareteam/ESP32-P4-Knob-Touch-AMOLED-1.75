/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include "esp_board_manager.h"
#include "esp_extconn.h"
#include "dev_custom.h"

#include "knob_wireless.h"

#define KNOB_BOARD_DEVICE_WIRELESS "wireless"

static uint8_t s_wireless_handle;
static bool s_extconn_initialized;

static int wireless_init(void *config, int config_size, void **device_handle)
{
    (void)config;
    (void)config_size;
    if (device_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_extconn_initialized) {
        esp_extconn_config_t extconn_config = ESP_EXTCONN_CONFIG_DEFAULT();
        esp_err_t ret = esp_extconn_init(&extconn_config);
        if (ret != ESP_OK) {
            return ret;
        }
        s_extconn_initialized = true;
    }

    *device_handle = &s_wireless_handle;
    return ESP_OK;
}

static int wireless_deinit(void *device_handle)
{
    /* esp-extconn 0.3.3 has no public transport deinitialization API. The
     * device therefore remains active for this application process. Do not
     * report a successful shutdown that did not happen. */
    return device_handle == &s_wireless_handle ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
}

CUSTOM_DEVICE_IMPLEMENT(wireless, wireless_init, wireless_deinit);

esp_err_t knob_wireless_init(void)
{
    esp_err_t ret = esp_board_manager_prepare();
    if (ret != ESP_OK) {
        return ret;
    }
    return esp_board_manager_init_device_by_name(KNOB_BOARD_DEVICE_WIRELESS);
}

uint8_t *knob_wireless_get_mac(void)
{
    return esp_extconn_get_mac();
}
