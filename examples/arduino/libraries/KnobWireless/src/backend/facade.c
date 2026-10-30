#include "esp_extconn.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "KnobWireless.h"

static const char *TAG = "KnobWireless";
static EventGroupHandle_t s_scan_events;
static wifi_event_sta_scan_done_t s_scan_done;
static const EventBits_t kScanDoneBit = BIT0;
static bool s_wireless_started;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *done = (wifi_event_sta_scan_done_t *)event_data;
        if (done) {
            s_scan_done = *done;
        }
        if (s_scan_events) {
            xEventGroupSetBits(s_scan_events, kScanDoneBit);
        }
    }
}

int ws_wireless_begin(void)
{
    if (s_wireless_started) {
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        return (int)err;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return (int)err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return (int)err;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return (int)err;
    }
    s_scan_events = xEventGroupCreate();
    if (!s_scan_events) {
        return (int)ESP_ERR_NO_MEM;
    }
    esp_extconn_config_t config = ESP_EXTCONN_CONFIG_DEFAULT();
    err = esp_extconn_init(&config);
    if (err != ESP_OK) {
        return (int)err;
    }
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK) {
        return (int)err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return (int)err;
    }
    err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wireless_started = true;
    }
    return (int)err;
}

int ws_wifi_scan(ws_wifi_record_t *records, size_t capacity, size_t *count)
{
    if (!records || !count || capacity == 0) {
        return (int)ESP_ERR_INVALID_ARG;
    }
    *count = 0;
    const int64_t start_us = esp_timer_get_time();
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
    };
    xEventGroupClearBits(s_scan_events, kScanDoneBit);
    memset(&s_scan_done, 0, sizeof(s_scan_done));
    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan start failed err=%d elapsed_ms=%lld", (int)err,
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return (int)err;
    }
    const EventBits_t bits = xEventGroupWaitBits(s_scan_events, kScanDoneBit, pdTRUE, pdTRUE,
                                                 pdMS_TO_TICKS(15000));
    if ((bits & kScanDoneBit) == 0) {
        ESP_LOGE(TAG, "scan done timeout elapsed_ms=%lld", (long long)((esp_timer_get_time() - start_us) / 1000));
        return (int)ESP_ERR_WIFI_TIMEOUT;
    }
    if (s_scan_done.status != ESP_OK) {
        ESP_LOGE(TAG, "scan done status=%d elapsed_ms=%lld", s_scan_done.status,
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return (int)s_scan_done.status;
    }
    uint16_t number = 0;
    err = esp_wifi_scan_get_ap_num(&number);
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list();
        ESP_LOGE(TAG, "scan count failed err=%d elapsed_ms=%lld", (int)err,
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return (int)err;
    }
    if (number == 0) {
        esp_wifi_clear_ap_list();
        return ESP_OK;
    }
    const size_t take = number < capacity ? (number < 10 ? number : 10) : (capacity < 10 ? capacity : 10);
    wifi_ap_record_t found[10] = {0};
    uint16_t request = (uint16_t)take;
    err = esp_wifi_scan_get_ap_records(&request, found);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan records failed err=%d elapsed_ms=%lld", (int)err,
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return (int)err;
    }
    for (size_t i = 0; i < request; ++i) {
        memcpy(records[i].ssid, found[i].ssid, sizeof(records[i].ssid));
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        records[i].rssi = found[i].rssi;
        records[i].channel = found[i].primary;
        records[i].authmode = (uint8_t)found[i].authmode;
    }
    *count = request;
    return ESP_OK;
}
