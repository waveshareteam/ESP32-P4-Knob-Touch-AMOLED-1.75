#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <sdkconfig.h>
#include <esp_arduino_version.h>
#include <esp_idf_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "KnobWireless supports ESP32-P4 only"
#endif

#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "KnobWireless requires Arduino-ESP32 3.3.12"
#endif

#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "KnobWireless requires ESP-IDF 5.5.5"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t authmode;
} ws_wifi_record_t;

int ws_wireless_begin(void);
int ws_wifi_scan(ws_wifi_record_t *records, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif
