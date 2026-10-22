#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_extconn.h"

static const char *TAG = "03_wifi";

#define WIFI_SCAN_DONE_BIT BIT0
#define WIFI_SCAN_TIMEOUT_MS 15000
#define WIFI_SCAN_TOP_N 10
#define WIFI_SCAN_MAX_RECORDS 64

typedef struct {
    EventGroupHandle_t events;
    uint32_t status;
    uint16_t event_count;
} wifi_scan_state_t;

static wifi_scan_state_t s_scan;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t *done = (const wifi_event_sta_scan_done_t *)event_data;
        s_scan.status = done->status;
        s_scan.event_count = done->number;
        xEventGroupSetBits(s_scan.events, WIFI_SCAN_DONE_BIT);
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    return ret;
}

static const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA_WPA2_PSK";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2_ENT";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3_PSK";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2_WPA3_PSK";
#endif
#ifdef WIFI_AUTH_WAPI_PSK
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI_PSK";
#endif
#ifdef WIFI_AUTH_OWE
    case WIFI_AUTH_OWE:
        return "OWE";
#endif
#ifdef WIFI_AUTH_WPA3_ENT_192
    case WIFI_AUTH_WPA3_ENT_192:
        return "WPA3_ENT_192";
#endif
#ifdef WIFI_AUTH_WPA3_EXT_PSK
    case WIFI_AUTH_WPA3_EXT_PSK:
        return "WPA3_EXT_PSK";
#endif
#ifdef WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
        return "WPA3_EXT_MIX";
#endif
#ifdef WIFI_AUTH_DPP
    case WIFI_AUTH_DPP:
        return "DPP";
#endif
    default:
        return "UNKNOWN";
    }
}

static void ssid_to_printable(const uint8_t ssid[32], char out[33])
{
    size_t len = 0;
    while (len < 32 && ssid[len] != 0) {
        unsigned char c = ssid[len];
        out[len] = isprint(c) ? (char)c : '?';
        len++;
    }
    out[len] = '\0';
}

static int compare_ap_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ap_a = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *ap_b = (const wifi_ap_record_t *)b;
    if (ap_a->rssi != ap_b->rssi) {
        return (int)ap_b->rssi - (int)ap_a->rssi;
    }
    return (int)ap_a->primary - (int)ap_b->primary;
}

static void print_scan_results(uint16_t total, uint16_t stored, const wifi_ap_record_t *records)
{
    uint16_t hidden = 0;
    for (uint16_t i = 0; i < stored; ++i) {
        if (records[i].ssid[0] == 0) {
            hidden++;
        }
    }

    uint16_t top = stored < WIFI_SCAN_TOP_N ? stored : WIFI_SCAN_TOP_N;
    ESP_LOGI(TAG, "summary: status=%s total=%u stored=%u top=%u hidden=%u",
             s_scan.status == 0 ? "OK" : "FAIL", total, stored, top, hidden);
    if (top == 0) {
        ESP_LOGI(TAG, "summary: 0 APs visible; no-network result is valid");
        return;
    }

    for (uint16_t i = 0; i < top; ++i) {
        char ssid[33];
        ssid_to_printable(records[i].ssid, ssid);
        ESP_LOGI(TAG, "top%02u: SSID=\"%s\" RSSI=%d channel=%u auth=%s",
                 (unsigned)(i + 1), ssid[0] ? ssid : "<hidden>", records[i].rssi,
                 records[i].primary, authmode_to_str(records[i].authmode));
    }
}

static esp_err_t run_wifi_scan(void)
{
    s_scan.events = xEventGroupCreate();
    if (!s_scan.events) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        goto cleanup_events;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        goto cleanup_nvs;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
        goto cleanup_nvs;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        goto cleanup_event_loop;
    }

    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "scan event handler register failed: %s", esp_err_to_name(ret));
        goto cleanup_wifi;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set STA mode failed: %s", esp_err_to_name(ret));
        goto cleanup_handler;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(ret));
        goto cleanup_handler;
    }

    wifi_scan_config_t scan = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    s_scan.status = UINT32_MAX;
    s_scan.event_count = 0;
    xEventGroupClearBits(s_scan.events, WIFI_SCAN_DONE_BIT);

    ret = esp_wifi_scan_start(&scan, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "scan start failed: %s", esp_err_to_name(ret));
        goto cleanup_started_wifi;
    }

    EventBits_t bits = xEventGroupWaitBits(s_scan.events, WIFI_SCAN_DONE_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS));
    if ((bits & WIFI_SCAN_DONE_BIT) == 0) {
        ESP_LOGE(TAG, "summary: status=TIMEOUT timeout_ms=%u", WIFI_SCAN_TIMEOUT_MS);
        (void)esp_wifi_scan_stop();
        ret = ESP_ERR_TIMEOUT;
        goto cleanup_started_wifi;
    }

    if (s_scan.status != 0) {
        ESP_LOGE(TAG, "summary: status=FAIL scan_status=%lu event_count=%u",
                 (unsigned long)s_scan.status, (unsigned)s_scan.event_count);
        ret = ESP_FAIL;
        goto cleanup_started_wifi;
    }

    uint16_t total = 0;
    ret = esp_wifi_scan_get_ap_num(&total);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get AP count failed: %s", esp_err_to_name(ret));
        goto cleanup_started_wifi;
    }

    uint16_t stored = total > WIFI_SCAN_MAX_RECORDS ? WIFI_SCAN_MAX_RECORDS : total;
    wifi_ap_record_t *records = NULL;
    if (stored > 0) {
        records = calloc(stored, sizeof(*records));
        if (!records) {
            ret = ESP_ERR_NO_MEM;
            goto cleanup_started_wifi;
        }
        uint16_t requested = stored;
        ret = esp_wifi_scan_get_ap_records(&requested, records);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "get AP records failed: %s", esp_err_to_name(ret));
            free(records);
            goto cleanup_started_wifi;
        }
        stored = requested;
        qsort(records, stored, sizeof(*records), compare_ap_rssi_desc);
    }

    print_scan_results(total, stored, records);
    free(records);
    ret = ESP_OK;

cleanup_started_wifi:
    (void)esp_wifi_stop();
cleanup_handler:
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event_handler);
cleanup_wifi:
    (void)esp_wifi_deinit();
cleanup_event_loop:
    (void)esp_event_loop_delete_default();
cleanup_nvs:
    (void)nvs_flash_deinit();
cleanup_events:
    if (s_scan.events) {
        vEventGroupDelete(s_scan.events);
        s_scan.events = NULL;
    }
    return ret;
}

void app_main(void)
{
    esp_extconn_config_t cfg = ESP_EXTCONN_CONFIG_DEFAULT();
    esp_err_t ret = esp_extconn_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_extconn_init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "summary: status=EXTCONN_FAIL err=%s", esp_err_to_name(ret));
        return;
    }

    uint8_t *mac = esp_extconn_get_mac();
    if (mac) {
        ESP_LOGI(TAG, "extconn MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "extconn MAC not available yet");
    }

    ret = run_wifi_scan();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "summary: status=FAIL err=%s", esp_err_to_name(ret));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
