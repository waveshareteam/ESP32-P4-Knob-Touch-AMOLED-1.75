/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "board_app.h"
#include "power_service.h"

#define AXP2101_ADDR            0x34
#define AXP2101_I2C_HZ          400000
#define AXP2101_I2C_TIMEOUT_MS  50

#define REG_STATUS1             0x00
#define REG_STATUS2             0x01
#define REG_GAUGE_CTRL          0x18
#define REG_BAT_DET_CTRL        0x68
#define REG_BAT_PERCENT         0xA4

#define STATUS1_BAT_PRESENT     (1 << 3)
#define STATUS2_DIRECTION(v)    (((v) >> 5) & 0x03)
#define DIRECTION_CHARGING      0x01
#define GAUGE_ENABLE            (1 << 3)
#define BAT_DET_ENABLE          (1 << 0)

static const char *TAG = "power";

static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, AXP2101_I2C_TIMEOUT_MS);
}

static esp_err_t set_bits(uint8_t reg, uint8_t bits)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(read_reg(reg, &value), TAG, "Read 0x%02x failed", reg);
    if ((value & bits) == bits) {
        return ESP_OK;
    }
    const uint8_t data[2] = {reg, (uint8_t)(value | bits)};
    return i2c_master_transmit(s_dev, data, sizeof(data), AXP2101_I2C_TIMEOUT_MS);
}

esp_err_t power_service_init(void)
{
    if (s_dev) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = board_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not ready");
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = AXP2101_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_dev), TAG, "Add AXP2101 failed");
    // Both are on after a power-up; make sure, since nothing else configures the PMIC
    esp_err_t ret = set_bits(REG_BAT_DET_CTRL, BAT_DET_ENABLE);
    if (ret == ESP_OK) {
        ret = set_bits(REG_GAUGE_CTRL, GAUGE_ENABLE);
    }
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }
    power_battery_t battery;
    power_service_get_battery(&battery);
    ESP_LOGI(TAG, "Battery %s", battery.present ? "present" : "not connected");
    return ESP_OK;
}

esp_err_t power_service_get_battery(power_battery_t *battery)
{
    *battery = (power_battery_t) {0};
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "Not started");
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t percent = 0;
    ESP_RETURN_ON_ERROR(read_reg(REG_STATUS1, &status1), TAG, "Read status failed");
    battery->present = (status1 & STATUS1_BAT_PRESENT) != 0;
    if (!battery->present) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(read_reg(REG_STATUS2, &status2), TAG, "Read status failed");
    ESP_RETURN_ON_ERROR(read_reg(REG_BAT_PERCENT, &percent), TAG, "Read gauge failed");
    battery->charging = STATUS2_DIRECTION(status2) == DIRECTION_CHARGING;
    battery->percent = percent > 100 ? 100 : percent;
    return ESP_OK;
}
