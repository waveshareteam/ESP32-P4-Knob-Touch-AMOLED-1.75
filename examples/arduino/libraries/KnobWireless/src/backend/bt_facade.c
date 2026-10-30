#include <string.h>
#include "esp_extconn.h"
#include "esp_bt_main.h"
#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <stdbool.h>

typedef void (*ws_bt_event_cb_t)(int event, int value);
typedef void (*ws_bt_pcm_cb_t)(const unsigned char *pcm, unsigned int len,
                               unsigned int sample_rate, unsigned int channels);
typedef struct {
    unsigned char addr[6];
    unsigned char addr_type;
    signed char rssi;
    unsigned char data_len;
    unsigned char data[31];
} ws_ble_result_t;
typedef void (*ws_ble_result_cb_t)(const ws_ble_result_t *result);
static ws_bt_event_cb_t s_event_cb;
static ws_bt_pcm_cb_t s_pcm_cb;
static ws_ble_result_cb_t s_ble_cb;
static uint32_t s_ble_duration;
static unsigned int s_pcm_rate = 44100;
static unsigned int s_pcm_channels = 2;
static bool s_bt_started;

extern int ws_wireless_begin(void);

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (!param) return;
    if (event == ESP_BT_GAP_CFM_REQ_EVT) {
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
    } else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t pin = {0};
        uint8_t length = 4;
        if (param->pin_req.min_16_digit) {
            length = 16;
        } else {
            pin[0] = '1'; pin[1] = '2'; pin[2] = '3'; pin[3] = '4';
        }
        esp_bt_gap_pin_reply(param->pin_req.bda, true, length, pin);
    }
}

static void bt_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    int value = 0;
    if (event == ESP_A2D_AUDIO_CFG_EVT && param && param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
        uint8_t f = param->audio_cfg.mcc.cie.sbc_info.samp_freq;
        s_pcm_rate = (f & ESP_A2D_SBC_CIE_SF_48K) ? 48000 :
                     (f & ESP_A2D_SBC_CIE_SF_44K) ? 44100 :
                     (f & ESP_A2D_SBC_CIE_SF_32K) ? 32000 : 16000;
        uint8_t c = param->audio_cfg.mcc.cie.sbc_info.ch_mode;
        s_pcm_channels = (c == ESP_A2D_SBC_CIE_CH_MODE_MONO) ? 1 : 2;
    } else if (event == ESP_A2D_PROF_STATE_EVT && param) {
        value = (int)param->a2d_prof_stat.init_state;
    } else if (event == ESP_A2D_AUDIO_STATE_EVT && param) {
        value = (int)param->audio_stat.state;
    }
    if (s_event_cb) s_event_cb((int)event, value);
}

static void bt_pcm_cb(const uint8_t *buf, uint32_t len)
{
    if (s_pcm_cb) s_pcm_cb(buf, len, s_pcm_rate, s_pcm_channels);
}

void ws_bt_set_event_callback(ws_bt_event_cb_t cb) { s_event_cb = cb; }
void ws_bt_set_pcm_callback(ws_bt_pcm_cb_t cb) { s_pcm_cb = cb; }
void ws_ble_set_result_callback(ws_ble_result_cb_t cb) { s_ble_cb = cb; }

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (!param) return;
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT &&
        param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        esp_ble_gap_start_scanning(s_ble_duration);
    } else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT &&
               param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT && s_ble_cb) {
        ws_ble_result_t result = {0};
        memcpy(result.addr, param->scan_rst.bda, sizeof(result.addr));
        result.addr_type = (unsigned char)param->scan_rst.ble_addr_type;
        result.rssi = (signed char)param->scan_rst.rssi;
        result.data_len = param->scan_rst.adv_data_len > sizeof(result.data) ?
                          sizeof(result.data) : param->scan_rst.adv_data_len;
        memcpy(result.data, param->scan_rst.ble_adv, result.data_len);
        s_ble_cb(&result);
    }
}

int ws_ble_start_scan(unsigned int duration_seconds)
{
    s_ble_duration = duration_seconds;
    esp_err_t err = esp_ble_gap_register_callback(ble_gap_cb);
    if (err != ESP_OK) return (int)err;
    static esp_ble_scan_params_t params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };
    return (int)esp_ble_gap_set_scan_params(&params);
}

int ws_bt_begin(void)
{
    if (s_bt_started) return ESP_OK;
    esp_err_t err = (esp_err_t)ws_wireless_begin();
    if (err != ESP_OK) return (int)err;
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        return (int)err;
    }
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        return (int)err;
    }
    err = esp_bt_gap_register_callback(bt_gap_cb);
    if (err != ESP_OK) return (int)err;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    err = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    if (err != ESP_OK) return (int)err;
    s_bt_started = true;
    return (int)err;
}

int ws_bt_start_a2dp(const char *name)
{
    esp_err_t err = esp_bt_gap_set_device_name(name ? name : "Knob_BT_INO");
    if (err != ESP_OK) return (int)err;
    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (err != ESP_OK) return (int)err;
    err = esp_a2d_register_callback(bt_event_cb);
    if (err != ESP_OK) return (int)err;
    err = esp_a2d_sink_register_data_callback(bt_pcm_cb);
    if (err != ESP_OK) return (int)err;
    return (int)esp_a2d_sink_init();
}
