/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "driver/i2c_master.h"
#include "board_app.h"
#include "time_service.h"

#define PCF85063_ADDR           0x51
#define PCF85063_I2C_HZ         400000
#define PCF85063_I2C_TIMEOUT_MS 50
#define REG_SECONDS             0x04    // Seconds to years follow in 7 registers
#define SECONDS_OS              0x80    // Oscillator stopped: the time is not valid

#define TIME_ZONE               "CST-8"
#define NTP_SERVER              "ntp.aliyun.com"
#define FIRST_VALID_YEAR        2025    // Anything earlier is a clock that was never set

static const char *TAG = "time";

static i2c_master_dev_handle_t s_dev;
static bool s_valid;

static uint8_t from_bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

esp_err_t time_service_init(void)
{
    setenv("TZ", TIME_ZONE, 1);
    tzset();
    if (!s_dev) {
        i2c_master_bus_handle_t bus = board_i2c_get_handle();
        ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not ready");
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF85063_ADDR,
            .scl_speed_hz = PCF85063_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_dev), TAG, "Add PCF85063 failed");
    }

    uint8_t reg = REG_SECONDS;
    uint8_t t[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, t, sizeof(t), PCF85063_I2C_TIMEOUT_MS), TAG,
                        "Read RTC failed");
    struct tm utc = {
        .tm_sec = from_bcd(t[0] & 0x7F),
        .tm_min = from_bcd(t[1] & 0x7F),
        .tm_hour = from_bcd(t[2] & 0x3F),
        .tm_mday = from_bcd(t[3] & 0x3F),
        .tm_mon = from_bcd(t[5] & 0x1F) - 1,
        .tm_year = from_bcd(t[6]) + 100,
    };
    if ((t[0] & SECONDS_OS) || utc.tm_year + 1900 < FIRST_VALID_YEAR || utc.tm_mon < 0 || utc.tm_mon > 11) {
        ESP_LOGI(TAG, "RTC holds no time yet; waiting for the network");
        return ESP_OK;
    }
    // mktime() works in local time; the RTC counts UTC
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t now = mktime(&utc);
    setenv("TZ", TIME_ZONE, 1);
    tzset();
    const struct timeval tv = {.tv_sec = now};
    settimeofday(&tv, NULL);
    s_valid = true;
    struct tm local;
    localtime_r(&now, &local);
    ESP_LOGI(TAG, "Time from RTC: %04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min);
    return ESP_OK;
}

bool time_service_is_valid(void)
{
    return s_valid;
}

esp_err_t time_service_save(void)
{
    s_valid = true;
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "Not started");
    const time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    const uint8_t data[8] = {
        REG_SECONDS,
        to_bcd(utc.tm_sec),     // Writing the seconds also clears the oscillator-stop flag
        to_bcd(utc.tm_min),
        to_bcd(utc.tm_hour),
        to_bcd(utc.tm_mday),
        (uint8_t)utc.tm_wday,
        to_bcd(utc.tm_mon + 1),
        to_bcd(utc.tm_year % 100),
    };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, data, sizeof(data), PCF85063_I2C_TIMEOUT_MS), TAG,
                        "Write RTC failed");
    ESP_LOGI(TAG, "Time stored in the RTC");
    return ESP_OK;
}

static void sync_cb(struct timeval *tv)
{
    (void)tv;
    // The RTC keeps it for the next reset, before Wi-Fi is back
    time_service_save();
}

void time_service_start_sync(void)
{
    if (esp_sntp_enabled()) {
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "Time sync with %s started", NTP_SERVER);
}
