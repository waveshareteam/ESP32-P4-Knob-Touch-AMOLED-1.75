/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The board is a Bluetooth speaker (A2DP sink) for a phone. While the phone streams,
 * the service owns audio through the audio service; when another app takes audio
 * over, the phone is asked to pause. The phone's volume slider and the device volume
 * (audio_service_set_volume()) follow each other.
 */
typedef enum {
    BT_STATE_OFF = 0,       /*!< Bluetooth switched off in Settings */
    BT_STATE_WAITING,       /*!< Discoverable, no phone connected */
    BT_STATE_CONNECTED,     /*!< A phone is connected but not streaming */
    BT_STATE_STREAMING,     /*!< The phone is streaming audio */
} bt_state_t;

typedef struct {
    bt_state_t state;
    char local_name[32];    /*!< Name the phone sees */
    char peer_name[64];     /*!< Connected phone, empty when unknown */
    char title[128];        /*!< Track metadata from the phone (UTF-8) */
    char artist[96];
    char album[96];
    bool playing;           /*!< Play status reported by the phone */
    bool play_status_known; /*!< The phone reports play status; otherwise only streaming tells */
    uint32_t position_ms;   /*!< Playback position, advanced locally between phone reports */
    uint32_t duration_ms;   /*!< Track length, 0 when unknown */
    uint32_t track_seq;     /*!< Increments whenever the track metadata changes */
    uint32_t cover_seq;     /*!< Increments whenever a new cover image (or none) is available */
} bt_info_t;

/**
 * @brief  Start the Bluetooth host; call after esp_extconn_init()
 */
esp_err_t bt_service_init(void);

/**
 * @brief  Copy the current connection and track state
 */
void bt_service_get_info(bt_info_t *info);

/**
 * @brief  Switch Bluetooth on or off; the setting is kept across reboots
 */
esp_err_t bt_service_set_enabled(bool enabled);
bool bt_service_is_enabled(void);

/* Remote control of the phone's player (AVRCP) */
esp_err_t bt_service_play(void);
esp_err_t bt_service_pause(void);
esp_err_t bt_service_next(void);
esp_err_t bt_service_prev(void);

/**
 * @brief  Copy the current cover image (JPEG) sent by the phone
 *
 * @param[out]  jpeg  Allocated copy, release it with free()
 * @return  ESP_ERR_NOT_FOUND when the phone sent no cover for this track
 */
esp_err_t bt_service_copy_cover(uint8_t **jpeg, size_t *len);

/**
 * @brief  Disconnect the current phone
 */
esp_err_t bt_service_disconnect(void);

/**
 * @brief  Disconnect and remove all paired phones
 */
esp_err_t bt_service_forget_all(void);

const char *bt_service_state_name(bt_state_t state);

#ifdef __cplusplus
}
#endif
