#pragma once

#include "KnobWireless.h"

#ifdef __cplusplus
extern "C" {
#endif

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

int ws_bt_begin(void);
void ws_bt_set_event_callback(ws_bt_event_cb_t cb);
void ws_bt_set_pcm_callback(ws_bt_pcm_cb_t cb);
void ws_ble_set_result_callback(ws_ble_result_cb_t cb);
int ws_ble_start_scan(unsigned int duration_seconds);
int ws_bt_start_a2dp(const char *name);

#ifdef __cplusplus
}
#endif
