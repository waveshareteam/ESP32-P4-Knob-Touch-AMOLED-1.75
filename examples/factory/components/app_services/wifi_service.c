/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "time_service.h"
#include "wifi_service.h"

#define NVS_NAMESPACE       "storage"   // Where the Settings page kept the switch before this service
#define NVS_KEY_ENABLED     "wifi_en"
#define QUICK_RETRIES       3
#define RETRY_PERIOD_US     (30 * 1000 * 1000)  // After the quick retries, a dropped network is tried this often
#define RSSI_REFRESH_US     (10 * 1000 * 1000)
#define SCAN_RECORDS_MAX    32

static const char *TAG = "wifi";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static bool s_enabled;
static bool s_started;          // esp_wifi_start() done
static bool s_scanning;
static wifi_service_state_t s_state = WIFI_SERVICE_OFF;
static wifi_service_fail_t s_fail;
static char s_ssid[WIFI_SERVICE_SSID_MAX];
static char s_ip[16];
static int8_t s_rssi;
static int64_t s_rssi_at;
static int s_retries;
static wifi_service_ap_t s_aps[WIFI_SERVICE_SCAN_MAX];
static int s_ap_count;
static uint32_t s_scan_generation;
static esp_timer_handle_t s_retry_timer;

static bool load_enabled(void)
{
    nvs_handle_t nvs;
    int32_t value = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i32(nvs, NVS_KEY_ENABLED, &value);
        nvs_close(nvs);
    }
    return value != 0;
}

static void save_enabled(bool enabled)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, NVS_KEY_ENABLED, enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void set_state(wifi_service_state_t state, wifi_service_fail_t fail)
{
    taskENTER_CRITICAL(&s_lock);
    s_state = state;
    s_fail = fail;
    if (state != WIFI_SERVICE_CONNECTED) {
        s_ip[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_lock);
}

/* The driver keeps the network last joined in its own flash storage */
static bool get_saved_ssid(char *ssid)
{
    wifi_config_t config = {};
    ssid[0] = '\0';
    if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK) {
        strlcpy(ssid, (const char *)config.sta.ssid, WIFI_SERVICE_SSID_MAX);
    }
    return ssid[0] != '\0';
}

/* What every join uses, also for networks saved by earlier firmware */
static void apply_join_options(wifi_config_t *config)
{
    // Any security the network offers is accepted; WPA3 networks may use either way of deriving the key
    config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    config->sta.pmf_cfg.capable = true;
    config->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
}

static void connect_saved(void)
{
    char ssid[WIFI_SERVICE_SSID_MAX];
    if (!get_saved_ssid(ssid)) {
        set_state(WIFI_SERVICE_IDLE, WIFI_SERVICE_FAIL_NONE);
        return;
    }
    wifi_config_t config = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK && (config.sta.threshold.authmode != WIFI_AUTH_OPEN ||
            config.sta.sae_pwe_h2e != WPA3_SAE_PWE_BOTH || !config.sta.pmf_cfg.capable)) {
        apply_join_options(&config);
        esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    taskENTER_CRITICAL(&s_lock);
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    taskEXIT_CRITICAL(&s_lock);
    set_state(WIFI_SERVICE_CONNECTING, WIFI_SERVICE_FAIL_NONE);
    ESP_LOGI(TAG, "Joining %s", ssid);
    const esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Join %s failed: %s", ssid, esp_err_to_name(ret));
        set_state(WIFI_SERVICE_IDLE, WIFI_SERVICE_FAIL_OTHER);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, RETRY_PERIOD_US);
    }
}

static void retry_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    const bool retry = s_enabled && s_state == WIFI_SERVICE_IDLE && s_fail != WIFI_SERVICE_FAIL_PASSWORD;
    taskEXIT_CRITICAL(&s_lock);
    if (retry) {
        s_retries = 0;
        connect_saved();
    }
}

static wifi_service_fail_t fail_from_reason(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return WIFI_SERVICE_FAIL_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
        return WIFI_SERVICE_FAIL_NOT_FOUND;
    default:
        return WIFI_SERVICE_FAIL_OTHER;
    }
}

static int compare_rssi(const void *a, const void *b)
{
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

static void collect_scan(void)
{
    uint16_t count = SCAN_RECORDS_MAX;
    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    // Fetching the records also frees the driver's copy, so it is done even after a failed scan
    if (!records || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        count = 0;
        esp_wifi_clear_ap_list();
    }
    qsort(records, count, sizeof(*records), compare_rssi);
    wifi_service_ap_t aps[WIFI_SERVICE_SCAN_MAX];
    int n = 0;
    for (int i = 0; i < count && n < WIFI_SERVICE_SCAN_MAX; i++) {
        const char *ssid = (const char *)records[i].ssid;
        bool seen = ssid[0] == '\0';    // Hidden networks have no name to show
        for (int j = 0; j < n && !seen; j++) {
            seen = strcmp(aps[j].ssid, ssid) == 0;
        }
        if (!seen) {
            ESP_LOGI(TAG, "Found %s, channel %u, %d dBm, auth %d", ssid, records[i].primary, records[i].rssi,
                     records[i].authmode);
            strlcpy(aps[n].ssid, ssid, sizeof(aps[n].ssid));
            aps[n].rssi = records[i].rssi;
            aps[n].secure = records[i].authmode != WIFI_AUTH_OPEN;
            n++;
        }
    }
    free(records);
    taskENTER_CRITICAL(&s_lock);
    memcpy(s_aps, aps, sizeof(aps[0]) * n);
    s_ap_count = n;
    s_scan_generation++;
    s_scanning = false;
    taskEXIT_CRITICAL(&s_lock);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_enabled) {
                connect_saved();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *event = data;
            ESP_LOGI(TAG, "Disconnected from %.32s, reason %u, rssi %d", (const char *)event->ssid, event->reason,
                     event->rssi);
            // Our own disconnects, before joining another network or switching off
            if (!s_enabled || event->reason == WIFI_REASON_ASSOC_LEAVE) {
                break;
            }
            const wifi_service_fail_t fail = fail_from_reason(event->reason);
            if (fail != WIFI_SERVICE_FAIL_PASSWORD && s_retries < QUICK_RETRIES) {
                s_retries++;
                set_state(WIFI_SERVICE_CONNECTING, WIFI_SERVICE_FAIL_NONE);
                const esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Retry failed: %s", esp_err_to_name(ret));
                }
                break;
            }
            ESP_LOGI(TAG, "Connect gave up (reason %u)", event->reason);
            s_retries = 0;
            set_state(WIFI_SERVICE_IDLE, fail);
            if (fail != WIFI_SERVICE_FAIL_PASSWORD) {
                esp_timer_stop(s_retry_timer);
                esp_timer_start_once(s_retry_timer, RETRY_PERIOD_US);
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            collect_scan();
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        wifi_ap_record_t ap = {};
        const bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        taskENTER_CRITICAL(&s_lock);
        s_state = WIFI_SERVICE_CONNECTED;
        s_fail = WIFI_SERVICE_FAIL_NONE;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        if (have_ap) {
            s_rssi = ap.rssi;
            strlcpy(s_ssid, (const char *)ap.ssid, sizeof(s_ssid));
        }
        s_rssi_at = esp_timer_get_time();
        taskEXIT_CRITICAL(&s_lock);
        s_retries = 0;
        ESP_LOGI(TAG, "Connected to %s, " IPSTR, s_ssid, IP2STR(&event->ip_info.ip));
        time_service_start_sync();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        if (s_state == WIFI_SERVICE_CONNECTED) {
            set_state(WIFI_SERVICE_CONNECTING, WIFI_SERVICE_FAIL_NONE);
        }
    }
}

esp_err_t wifi_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = esp_netif_init();
    ESP_RETURN_ON_FALSE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, ret, TAG, "Netif init failed");
    ret = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, ret, TAG, "Event loop failed");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_FAIL, TAG, "Create station netif failed");
    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Set station mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG,
                        "Register Wi-Fi events failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG,
                        "Register IP events failed");
    const esp_timer_create_args_t retry_args = {
        .callback = retry_cb,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "Create retry timer failed");
    s_initialized = true;

    get_saved_ssid(s_ssid);
    s_enabled = load_enabled();
    ESP_LOGI(TAG, "Wi-Fi %s, saved network: %s", s_enabled ? "on" : "off", s_ssid[0] ? s_ssid : "none");
    if (s_enabled) {
        set_state(WIFI_SERVICE_IDLE, WIFI_SERVICE_FAIL_NONE);
        ret = esp_wifi_start();
        s_started = ret == ESP_OK;
        ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi start failed");
    }
    return ESP_OK;
}

esp_err_t wifi_service_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    if (enabled == s_enabled) {
        return ESP_OK;
    }
    s_enabled = enabled;
    save_enabled(enabled);
    esp_timer_stop(s_retry_timer);
    s_retries = 0;
    if (!enabled) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_started = false;
        taskENTER_CRITICAL(&s_lock);
        s_scanning = false;
        taskEXIT_CRITICAL(&s_lock);
        set_state(WIFI_SERVICE_OFF, WIFI_SERVICE_FAIL_NONE);
        return ESP_OK;
    }
    set_state(WIFI_SERVICE_IDLE, WIFI_SERVICE_FAIL_NONE);
    if (s_started) {
        connect_saved();
        return ESP_OK;
    }
    // The start event joins the saved network
    const esp_err_t ret = esp_wifi_start();
    s_started = ret == ESP_OK;
    return ret;
}

void wifi_service_get_info(wifi_service_info_t *info)
{
    // The signal is fetched from the co-processor now and then, not on every call
    const int64_t now = esp_timer_get_time();
    if (s_state == WIFI_SERVICE_CONNECTED && now - s_rssi_at > RSSI_REFRESH_US) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_rssi = ap.rssi;
        }
        s_rssi_at = now;
    }
    taskENTER_CRITICAL(&s_lock);
    info->state = s_state;
    info->fail = s_fail;
    info->rssi = s_rssi;
    strlcpy(info->ssid, s_ssid, sizeof(info->ssid));
    strlcpy(info->ip, s_ip, sizeof(info->ip));
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t wifi_service_scan(void)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi is off");
    if (s_scanning) {
        return ESP_OK;
    }
    // A scan would get in the way of joining a network
    if (s_state == WIFI_SERVICE_CONNECTING) {
        return ESP_ERR_INVALID_STATE;
    }
    // Fails while a connection is being set up; the caller asks again later
    const esp_err_t ret = esp_wifi_scan_start(NULL, false);
    if (ret == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_scanning = true;
        taskEXIT_CRITICAL(&s_lock);
    }
    return ret;
}

bool wifi_service_is_scanning(void)
{
    return s_scanning;
}

int wifi_service_get_scan(wifi_service_ap_t *aps, int max, uint32_t *generation)
{
    taskENTER_CRITICAL(&s_lock);
    const int n = s_ap_count < max ? s_ap_count : max;
    memcpy(aps, s_aps, sizeof(aps[0]) * n);
    if (generation) {
        *generation = s_scan_generation;
    }
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

esp_err_t wifi_service_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    ESP_RETURN_ON_FALSE(ssid && ssid[0] && strlen(ssid) < WIFI_SERVICE_SSID_MAX, ESP_ERR_INVALID_ARG, TAG, "Bad SSID");
    ESP_RETURN_ON_FALSE(!password || strlen(password) < WIFI_SERVICE_PASSWORD_MAX, ESP_ERR_INVALID_ARG, TAG,
                        "Password too long");
    wifi_config_t config = {};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    if (password) {
        strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    }
    apply_join_options(&config);

    esp_timer_stop(s_retry_timer);
    s_retries = 0;
    taskENTER_CRITICAL(&s_lock);
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    taskEXIT_CRITICAL(&s_lock);
    set_state(WIFI_SERVICE_CONNECTING, WIFI_SERVICE_FAIL_NONE);
    ESP_LOGI(TAG, "Joining %s with a %u-character password", ssid, password ? (unsigned)strlen(password) : 0);
    if (s_started) {
        if (s_scanning) {
            esp_wifi_scan_stop();
        }
        esp_wifi_disconnect();
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "Set config failed");
    if (!s_enabled) {
        s_enabled = true;
        save_enabled(true);
    }
    if (!s_started) {
        // The start event joins the network just saved
        const esp_err_t ret = esp_wifi_start();
        s_started = ret == ESP_OK;
        return ret;
    }
    const esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Join %s failed: %s", ssid, esp_err_to_name(ret));
        set_state(WIFI_SERVICE_IDLE, WIFI_SERVICE_FAIL_OTHER);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, RETRY_PERIOD_US);
    }
    return ret;
}

esp_err_t wifi_service_forget(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    esp_timer_stop(s_retry_timer);
    if (s_started) {
        esp_wifi_disconnect();
    }
    wifi_config_t config = {};
    const esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &config);
    taskENTER_CRITICAL(&s_lock);
    s_ssid[0] = '\0';
    taskEXIT_CRITICAL(&s_lock);
    set_state(s_enabled ? WIFI_SERVICE_IDLE : WIFI_SERVICE_OFF, WIFI_SERVICE_FAIL_NONE);
    return ret;
}
