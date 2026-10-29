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
 * Audio is owned by one client at a time. A client that acquires audio takes it
 * over from the current owner, whose preempt callback runs first. Reads, writes
 * and format changes from a client that is not the owner fail immediately with
 * ESP_ERR_INVALID_STATE, so a stale task can never reach the codec.
 */
typedef struct audio_client *audio_client_handle_t;

/**
 * @brief  Called when another client takes audio over
 *
 *         Runs in the task of the acquiring client. It must stop the preempted
 *         client's playback or capture without waiting for the LVGL lock from a
 *         different task, and should only update UI state through flags.
 */
typedef void (*audio_preempt_cb_t)(audio_client_handle_t client, void *user_ctx);

/**
 * @brief  Checks whether a client may take audio after ownership switching is serialized
 *
 *         The callback runs with the ownership-switch lock held. It must not wait or call
 *         audio_service APIs.
 */
typedef bool (*audio_acquire_check_t)(void *user_ctx);

typedef struct {
    const char *name;               /*!< Client name used in logs */
    audio_preempt_cb_t on_preempt;  /*!< Optional preempt callback */
    void *user_ctx;                 /*!< Passed to on_preempt */
} audio_client_config_t;

/**
 * @brief  Initialize the service after board_audio_init(); restores the saved volume
 */
esp_err_t audio_service_init(void);

esp_err_t audio_client_register(const audio_client_config_t *config, audio_client_handle_t *ret_client);

/**
 * @brief  Take audio over; the codec is reopened at 16 kHz, 16-bit stereo
 */
esp_err_t audio_client_acquire(audio_client_handle_t client);

/**
 * @brief  Take audio over if check still permits it after waiting for ownership switching
 *
 *         Returns ESP_ERR_INVALID_STATE if check rejects the acquisition. The existing
 *         audio_client_acquire() always takes ownership and is otherwise unchanged.
 */
esp_err_t audio_client_acquire_if(audio_client_handle_t client, audio_acquire_check_t check, void *user_ctx);

/**
 * @brief  Give audio up if this client owns it; the codec is closed
 */
esp_err_t audio_client_release(audio_client_handle_t client);

bool audio_client_is_owner(audio_client_handle_t client);

esp_err_t audio_client_set_format(audio_client_handle_t client, uint32_t sample_rate, uint32_t bits_per_sample,
                                  uint32_t channels);
esp_err_t audio_client_write(audio_client_handle_t client, const void *data, size_t len);
esp_err_t audio_client_read(audio_client_handle_t client, void *data, size_t len);

/**
 * @brief  Mute output for the current owner only, for example around a pause
 *
 *         The flag is cleared whenever ownership changes, so it never leaks into
 *         the next owner. It is independent of the user mute set by
 *         audio_service_set_mute().
 */
esp_err_t audio_client_set_mute(audio_client_handle_t client, bool mute);

/**
 * @brief  Name of the current owner, or NULL when audio is idle
 */
const char *audio_service_owner_name(void);

/* Output volume 0-100, saved in NVS. It is the one volume of the device: Settings,
 * the knob in the music apps and a connected phone all change it. Requests update
 * the latest setting immediately and never wait behind streaming output I/O. */
esp_err_t audio_service_set_volume(int volume);
int audio_service_get_volume(void);

/**
 * @brief  Called after the volume changed, in the task that changed it
 */
typedef void (*audio_volume_cb_t)(int volume, void *user_ctx);
esp_err_t audio_service_add_volume_listener(audio_volume_cb_t cb, void *user_ctx);
/** Global mute follows the same non-blocking update path as volume. */
esp_err_t audio_service_set_mute(bool mute);
bool audio_service_get_mute(void);

#ifdef __cplusplus
}
#endif
