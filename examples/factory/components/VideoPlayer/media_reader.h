/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  One stream of an MP4, MOV or AVI file, read in file order with esp_extractor
 *
 *         Audio and video each get a reader of their own, so each moves through its track at
 *         its own pace however the container interleaves them.
 */
typedef struct media_reader media_reader_t;

/**
 * @brief  An open file with a few cached blocks of it, shared by the readers of that file
 *
 *         Audio and video of the same moment sit close together in the file, so with one cache
 *         each block comes off the card once even though two readers go through it.
 */
typedef struct media_file media_file_t;

esp_err_t media_file_open(const char *path, media_file_t **ret_file);

/**
 * @brief  Close the file once none of its readers is open any more
 */
void media_file_close(media_file_t *file);

/**
 * @param stream_type  ESP_EXTRACTOR_STREAM_TYPE_VIDEO or ESP_EXTRACTOR_STREAM_TYPE_AUDIO
 * @param pool_size    Frame pool; at least twice the largest frame of the stream
 *
 * @return ESP_ERR_NOT_FOUND when the file has no stream of that type
 */
esp_err_t media_reader_open(media_file_t *file, esp_extractor_stream_type_t stream_type, uint32_t pool_size,
                            media_reader_t **ret_reader);

/**
 * @brief  The stream this reader delivers and its length in milliseconds
 */
const esp_extractor_stream_info_t *media_reader_info(const media_reader_t *reader);

/**
 * @brief  Next frame of the stream; release it with media_reader_release()
 *
 * @return ESP_ERR_NOT_FOUND at the end of the stream
 */
esp_err_t media_reader_read(media_reader_t *reader, esp_extractor_frame_info_t *frame);

void media_reader_release(media_reader_t *reader, esp_extractor_frame_info_t *frame);

esp_err_t media_reader_seek(media_reader_t *reader, uint32_t time_ms);

/**
 * @brief  Whether reader is so far ahead of other in the file that what it reads now would leave
 *         the shared cache before other gets there, and the card would be read twice
 *
 *         The reader ahead should wait while this holds, as long as it can afford to.
 */
bool media_reader_too_far_ahead(const media_reader_t *reader, const media_reader_t *other);

void media_reader_close(media_reader_t *reader);

/**
 * @brief  Whether the file name has an extension the reader opens (MP4, MOV, M4V or AVI)
 */
bool media_reader_is_video_file(const char *name);

#ifdef __cplusplus
}
#endif
