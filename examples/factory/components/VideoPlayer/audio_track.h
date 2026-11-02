/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_extractor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Turns the audio frames of a video file into 16-bit PCM: AAC and MP3 are decoded,
 *         16-bit PCM passes through
 */
typedef struct audio_track audio_track_t;

/**
 * @return ESP_ERR_NOT_SUPPORTED for other audio formats
 */
esp_err_t audio_track_open(const esp_extractor_audio_stream_info_t *info, audio_track_t **ret_track);

/**
 * @brief  Decode one container frame; the PCM stays valid, and writable, until the next call
 */
esp_err_t audio_track_decode(audio_track_t *track, uint8_t *data, uint32_t len, int16_t **pcm,
                             uint32_t *pcm_bytes);

/**
 * @brief  Format of the decoded PCM, known after the first decoded frame
 */
void audio_track_format(const audio_track_t *track, uint32_t *sample_rate, uint8_t *channels);

void audio_track_close(audio_track_t *track);

#ifdef __cplusplus
}
#endif
