#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_extconn.h"

static const char *TAG = "04_bluetooth";

#define BLE_SCAN_WINDOW_SECONDS 10
#define BLE_SCAN_TIMEOUT_US 15000000LL
#define BLE_PARAM_ACK_TIMEOUT_US 5000000LL
#define BLE_START_ACK_TIMEOUT_US 5000000LL
#define BLE_STOP_ACK_TIMEOUT_US 5000000LL
#define BLE_RESTART_DELAY_MS 250
#define BLE_START_RETRY_DELAY_MS 3000
#define BLE_TABLE_MAX 64
#define BLE_TOP_N 10
#define BLE_EXPIRE_AFTER_WINDOWS 3
#define BLE_NAME_MAX 31

typedef struct {
    bool in_use;
    bool name_complete;
    bool seen_this_window;
    esp_bd_addr_t addr;
    esp_ble_addr_type_t addr_type;
    char name[BLE_NAME_MAX + 1];
    uint32_t seen_count;
    int8_t last_rssi;
    int8_t best_rssi;
    int64_t last_seen_us;
    uint32_t last_window;
} ble_device_t;

typedef enum {
    BLE_SCAN_STATE_IDLE = 0,
    BLE_SCAN_STATE_PARAMS_PENDING,
    BLE_SCAN_STATE_STARTING,
    BLE_SCAN_STATE_RUNNING,
    BLE_SCAN_STATE_STOPPING,
} ble_scan_state_t;

static ble_device_t s_devices[BLE_TABLE_MAX];
static ble_scan_state_t s_scan_state = BLE_SCAN_STATE_IDLE;
static bool s_scan_params_ready;
static bool s_restart_scheduled;
static int64_t s_restart_due_us;
static int64_t s_state_started_us;
static int64_t s_scan_started_us;
static int64_t s_scan_stop_requested_us;
static uint32_t s_window;
static uint32_t s_window_packets;
static uint32_t s_window_unique;
static uint32_t s_window_new;

static esp_ble_scan_params_t s_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x60,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static void start_scan_window(void);

static const char *scan_state_to_str(ble_scan_state_t state)
{
    switch (state) {
    case BLE_SCAN_STATE_IDLE:
        return "idle";
    case BLE_SCAN_STATE_PARAMS_PENDING:
        return "params_pending";
    case BLE_SCAN_STATE_STARTING:
        return "starting";
    case BLE_SCAN_STATE_RUNNING:
        return "running";
    case BLE_SCAN_STATE_STOPPING:
        return "stopping";
    default:
        return "unknown";
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            return ret;
        }
        ret = nvs_flash_init();
    }
    return ret;
}

static bool addr_equal(const esp_bd_addr_t a, const esp_bd_addr_t b)
{
    return memcmp(a, b, sizeof(esp_bd_addr_t)) == 0;
}

static const char *addr_type_to_str(esp_ble_addr_type_t type)
{
    switch (type) {
    case BLE_ADDR_TYPE_PUBLIC:
        return "public";
    case BLE_ADDR_TYPE_RANDOM:
        return "random";
    case BLE_ADDR_TYPE_RPA_PUBLIC:
        return "rpa_public";
    case BLE_ADDR_TYPE_RPA_RANDOM:
        return "rpa_random";
    default:
        return "unknown";
    }
}

static void format_addr(const esp_bd_addr_t addr, char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void copy_printable_name(char *dst, size_t dst_len, const uint8_t *src, uint8_t src_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    size_t n = src_len < dst_len - 1 ? src_len : dst_len - 1;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = src[i];
        dst[i] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    dst[n] = '\0';
}

static void parse_name_tlvs(const uint8_t *data, uint8_t len, char *name, size_t name_len, bool *complete)
{
    if (!data || !name || !complete) {
        return;
    }
    for (uint8_t idx = 0; idx < len;) {
        uint8_t field_len = data[idx];
        if (field_len == 0) {
            break;
        }
        if ((uint16_t)idx + field_len >= len) {
            break;
        }
        uint8_t type = data[idx + 1];
        uint8_t value_len = field_len - 1;
        const uint8_t *value = &data[idx + 2];
        if (value_len > 0 && type == ESP_BLE_AD_TYPE_NAME_CMPL) {
            copy_printable_name(name, name_len, value, value_len);
            *complete = true;
            return;
        }
        if (value_len > 0 && type == ESP_BLE_AD_TYPE_NAME_SHORT && !*complete && name[0] == '\0') {
            copy_printable_name(name, name_len, value, value_len);
        }
        idx = (uint8_t)(idx + field_len + 1);
    }
}

static void resolve_adv_name(const esp_ble_gap_cb_param_t *param, char *name, size_t name_len, bool *complete)
{
    name[0] = '\0';
    *complete = false;
    parse_name_tlvs(param->scan_rst.ble_adv, param->scan_rst.adv_data_len, name, name_len, complete);
    parse_name_tlvs(param->scan_rst.ble_adv + param->scan_rst.adv_data_len,
                    param->scan_rst.scan_rsp_len, name, name_len, complete);
}

static int find_device(const esp_bd_addr_t addr, esp_ble_addr_type_t addr_type)
{
    for (int i = 0; i < BLE_TABLE_MAX; ++i) {
        if (s_devices[i].in_use && s_devices[i].addr_type == addr_type && addr_equal(s_devices[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

static int choose_slot(void)
{
    int oldest = 0;
    for (int i = 0; i < BLE_TABLE_MAX; ++i) {
        if (!s_devices[i].in_use) {
            return i;
        }
        if (s_devices[i].last_seen_us < s_devices[oldest].last_seen_us) {
            oldest = i;
        }
    }
    return oldest;
}

static void record_device(const esp_ble_gap_cb_param_t *param)
{
    s_window_packets++;
    int idx = find_device(param->scan_rst.bda, param->scan_rst.ble_addr_type);
    if (idx < 0) {
        idx = choose_slot();
        memset(&s_devices[idx], 0, sizeof(s_devices[idx]));
        s_devices[idx].in_use = true;
        memcpy(s_devices[idx].addr, param->scan_rst.bda, sizeof(esp_bd_addr_t));
        s_devices[idx].addr_type = param->scan_rst.ble_addr_type;
        s_devices[idx].best_rssi = -128;
        s_window_new++;
    }

    ble_device_t *dev = &s_devices[idx];
    if (!dev->seen_this_window) {
        dev->seen_this_window = true;
        s_window_unique++;
    }
    dev->seen_count++;
    dev->last_rssi = param->scan_rst.rssi;
    if (param->scan_rst.rssi > dev->best_rssi) {
        dev->best_rssi = param->scan_rst.rssi;
    }
    dev->last_seen_us = esp_timer_get_time();
    dev->last_window = s_window;

    char adv_name[BLE_NAME_MAX + 1];
    bool complete = false;
    resolve_adv_name(param, adv_name, sizeof(adv_name), &complete);
    if (adv_name[0] != '\0' && (complete || !dev->name[0] || !dev->name_complete)) {
        strlcpy(dev->name, adv_name, sizeof(dev->name));
        dev->name_complete = complete;
    }
}

static uint32_t expire_devices(void)
{
    uint32_t expired = 0;
    for (int i = 0; i < BLE_TABLE_MAX; ++i) {
        if (s_devices[i].in_use && s_window > s_devices[i].last_window &&
            s_window - s_devices[i].last_window >= BLE_EXPIRE_AFTER_WINDOWS) {
            memset(&s_devices[i], 0, sizeof(s_devices[i]));
            expired++;
        }
    }
    return expired;
}

static int compare_device_index_by_rssi(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (s_devices[ia].best_rssi != s_devices[ib].best_rssi) {
        return (int)s_devices[ib].best_rssi - (int)s_devices[ia].best_rssi;
    }
    return (int)s_devices[ib].last_rssi - (int)s_devices[ia].last_rssi;
}

static void print_window_summary(uint32_t expired)
{
    int indexes[BLE_TABLE_MAX];
    int count = 0;
    int named = 0;
    int strongest = -1;
    for (int i = 0; i < BLE_TABLE_MAX; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        indexes[count++] = i;
        if (s_devices[i].name[0] != '\0') {
            named++;
        }
        if (strongest < 0 || s_devices[i].best_rssi > s_devices[strongest].best_rssi) {
            strongest = i;
        }
    }
    qsort(indexes, count, sizeof(indexes[0]), compare_device_index_by_rssi);

    char strong_addr[18] = "none";
    const char *strong_name = "none";
    int strong_rssi = 0;
    if (strongest >= 0) {
        format_addr(s_devices[strongest].addr, strong_addr);
        strong_name = s_devices[strongest].name[0] ? s_devices[strongest].name : "Unnamed";
        strong_rssi = s_devices[strongest].best_rssi;
    }
    ESP_LOGI(TAG, "window %lu summary: unique=%d named=%d new=%lu expired=%lu packets=%lu strongest=\"%s\" addr=%s RSSI=%d",
             (unsigned long)s_window, count, named, (unsigned long)s_window_new,
             (unsigned long)expired, (unsigned long)s_window_packets, strong_name, strong_addr, strong_rssi);

    int top = count < BLE_TOP_N ? count : BLE_TOP_N;
    if (top == 0) {
        ESP_LOGI(TAG, "window %lu top: none", (unsigned long)s_window);
        return;
    }
    int64_t now = esp_timer_get_time();
    for (int rank = 0; rank < top; ++rank) {
        ble_device_t *dev = &s_devices[indexes[rank]];
        char addr[18];
        format_addr(dev->addr, addr);
        ESP_LOGI(TAG, "top%02d: name=\"%s\" addr=%s type=%s RSSI=%d best=%d seen=%lu last_seen_ms=%ld",
                 rank + 1, dev->name[0] ? dev->name : "Unnamed", addr, addr_type_to_str(dev->addr_type),
                 dev->last_rssi, dev->best_rssi, (unsigned long)dev->seen_count,
                 (long)((now - dev->last_seen_us) / 1000));
    }
}

static void schedule_scan_restart(uint32_t delay_ms)
{
    s_restart_due_us = esp_timer_get_time() + (int64_t)delay_ms * 1000;
    s_restart_scheduled = true;
}

static void finish_scan_window(const char *reason)
{
    ble_scan_state_t prev = s_scan_state;
    s_scan_state = BLE_SCAN_STATE_IDLE;
    s_scan_stop_requested_us = 0;

    uint32_t expired = expire_devices();
    ESP_LOGI(TAG, "window %lu finished: reason=%s previous_state=%s",
             (unsigned long)s_window, reason, scan_state_to_str(prev));
    print_window_summary(expired);
    schedule_scan_restart(BLE_RESTART_DELAY_MS);
}

static void start_scan_window(void)
{
    if (!s_scan_params_ready) {
        ESP_LOGW(TAG, "scan start requested before params are ready");
        schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        return;
    }
    if (s_scan_state != BLE_SCAN_STATE_IDLE) {
        ESP_LOGW(TAG, "scan start skipped while state=%s", scan_state_to_str(s_scan_state));
        schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        return;
    }

    s_window++;
    s_window_packets = 0;
    s_window_unique = 0;
    s_window_new = 0;
    s_state_started_us = esp_timer_get_time();
    s_scan_started_us = s_state_started_us;
    s_scan_stop_requested_us = 0;
    for (int i = 0; i < BLE_TABLE_MAX; ++i) {
        s_devices[i].seen_this_window = false;
    }

    s_scan_state = BLE_SCAN_STATE_STARTING;
    esp_err_t ret = esp_ble_gap_start_scanning(BLE_SCAN_WINDOW_SECONDS);
    if (ret != ESP_OK) {
        s_scan_state = BLE_SCAN_STATE_IDLE;
        ESP_LOGE(TAG, "window %lu scan start request failed: %s",
                 (unsigned long)s_window, esp_err_to_name(ret));
        schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        return;
    }
    ESP_LOGI(TAG, "window %lu scan start requested", (unsigned long)s_window);
    s_restart_scheduled = false;
}

static void request_scan_params(void)
{
    if (s_scan_state != BLE_SCAN_STATE_IDLE) {
        ESP_LOGW(TAG, "scan params request skipped while state=%s", scan_state_to_str(s_scan_state));
        schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        return;
    }

    s_scan_state = BLE_SCAN_STATE_PARAMS_PENDING;
    s_state_started_us = esp_timer_get_time();
    esp_err_t ret = esp_ble_gap_set_scan_params(&s_scan_params);
    if (ret != ESP_OK) {
        s_scan_state = BLE_SCAN_STATE_IDLE;
        s_scan_params_ready = false;
        ESP_LOGE(TAG, "set scan params request failed: %s", esp_err_to_name(ret));
        schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
    }
}

static void process_scheduled_scan_restart(void)
{
    if (!s_restart_scheduled || s_scan_state != BLE_SCAN_STATE_IDLE) {
        return;
    }
    if (esp_timer_get_time() < s_restart_due_us) {
        return;
    }

    s_restart_scheduled = false;
    if (s_scan_params_ready) {
        start_scan_window();
    } else {
        request_scan_params();
    }
}

static void mark_scan_running(void)
{
    s_restart_scheduled = false;
    s_scan_stop_requested_us = 0;
    s_scan_started_us = esp_timer_get_time();
    s_scan_state = BLE_SCAN_STATE_RUNNING;
    ESP_LOGI(TAG, "window %lu scan running for %u seconds",
             (unsigned long)s_window, BLE_SCAN_WINDOW_SECONDS);
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "set scan params failed: status=%d", (int)param->scan_param_cmpl.status);
            s_scan_params_ready = false;
            s_scan_state = BLE_SCAN_STATE_IDLE;
            schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
            break;
        }
        s_scan_params_ready = true;
        s_scan_state = BLE_SCAN_STATE_IDLE;
        start_scan_window();
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "window %lu scan start failed: status=%d",
                     (unsigned long)s_window, (int)param->scan_start_cmpl.status);
            s_scan_state = BLE_SCAN_STATE_IDLE;
            schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
            break;
        }
        if (s_scan_state != BLE_SCAN_STATE_STARTING) {
            if (s_scan_state == BLE_SCAN_STATE_IDLE) {
                ESP_LOGW(TAG, "scan start complete after timeout; adopting window %lu as running",
                         (unsigned long)s_window);
                mark_scan_running();
            } else {
                ESP_LOGW(TAG, "scan start complete while state=%s", scan_state_to_str(s_scan_state));
            }
            break;
        }
        mark_scan_running();
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "scan stop failed: status=%d", (int)param->scan_stop_cmpl.status);
        }
        if (s_scan_state == BLE_SCAN_STATE_STOPPING) {
            finish_scan_window("stop_complete");
        } else {
            ESP_LOGW(TAG, "scan stop complete while state=%s", scan_state_to_str(s_scan_state));
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            if (s_scan_state == BLE_SCAN_STATE_RUNNING) {
                record_device(param);
            }
        } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            if (s_scan_state == BLE_SCAN_STATE_RUNNING || s_scan_state == BLE_SCAN_STATE_STOPPING) {
                finish_scan_window("scan_complete");
            } else {
                ESP_LOGW(TAG, "scan complete while state=%s", scan_state_to_str(s_scan_state));
            }
        }
        break;
    default:
        break;
    }
}

void app_main(void)
{
    esp_extconn_config_t extconn_cfg = ESP_EXTCONN_CONFIG_DEFAULT();
    esp_err_t ret = esp_extconn_init(&extconn_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "extconn init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_ble_gap_register_callback(gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "START BLE 4.2 GAP scan, %u second aggregate windows", BLE_SCAN_WINDOW_SECONDS);
    request_scan_params();

    while (1) {
        int64_t now = esp_timer_get_time();
        if (s_scan_state == BLE_SCAN_STATE_PARAMS_PENDING && now - s_state_started_us > BLE_PARAM_ACK_TIMEOUT_US) {
            ESP_LOGE(TAG, "scan params acknowledgement timeout after %u ms",
                     (unsigned)(BLE_PARAM_ACK_TIMEOUT_US / 1000));
            s_scan_state = BLE_SCAN_STATE_IDLE;
            s_scan_params_ready = false;
            schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        }
        if (s_scan_state == BLE_SCAN_STATE_STARTING && now - s_state_started_us > BLE_START_ACK_TIMEOUT_US) {
            ESP_LOGE(TAG, "window %lu scan start acknowledgement timeout after %u ms",
                     (unsigned long)s_window, (unsigned)(BLE_START_ACK_TIMEOUT_US / 1000));
            s_scan_state = BLE_SCAN_STATE_IDLE;
            schedule_scan_restart(BLE_START_RETRY_DELAY_MS);
        }
        if (s_scan_state == BLE_SCAN_STATE_RUNNING && now - s_scan_started_us > BLE_SCAN_TIMEOUT_US) {
            ESP_LOGE(TAG, "window %lu timeout after %u ms; requesting stop",
                     (unsigned long)s_window, (unsigned)(BLE_SCAN_TIMEOUT_US / 1000));
            s_scan_state = BLE_SCAN_STATE_STOPPING;
            s_scan_stop_requested_us = now;
            esp_err_t stop_ret = esp_ble_gap_stop_scanning();
            if (stop_ret != ESP_OK) {
                ESP_LOGE(TAG, "scan stop request failed after timeout: %s", esp_err_to_name(stop_ret));
                finish_scan_window("timeout_stop_request_failed");
            }
        }
        if (s_scan_state == BLE_SCAN_STATE_STOPPING && s_scan_stop_requested_us > 0 &&
            now - s_scan_stop_requested_us > BLE_STOP_ACK_TIMEOUT_US) {
            ESP_LOGE(TAG, "window %lu stop acknowledgement timeout after %u ms",
                     (unsigned long)s_window, (unsigned)(BLE_STOP_ACK_TIMEOUT_US / 1000));
            finish_scan_window("stop_ack_timeout");
        }
        process_scheduled_scan_restart();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}