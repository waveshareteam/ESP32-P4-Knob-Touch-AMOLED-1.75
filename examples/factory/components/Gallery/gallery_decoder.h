/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  RGB565 picture in LVGL byte order with packed rows (stride = width * 2)
 */
typedef struct {
    uint16_t *pixels;
    int width;
    int height;
} gallery_image_t;

/**
 * @brief  Prepare the hardware JPEG decoder; decoding still works in software without it
 */
esp_err_t gallery_decoder_init(void);

void gallery_decoder_deinit(void);

/**
 * @brief  Whether the file name has a picture extension the decoder reads (JPEG or PNG)
 */
bool gallery_is_picture(const char *name);

/**
 * @brief  Decode a JPEG or PNG file into a working copy for display
 *
 *         Large pictures shrink until their shorter side is `cover` pixels, and the longer side
 *         stays within `max_side`; smaller pictures keep their size. EXIF orientation is applied.
 *         The file is read completely before decoding, so it is closed when `read_done` runs.
 *
 * @param[out] reason  On failure, a short explanation for the user
 */
esp_err_t gallery_decode_file(const char *path, int cover, int max_side, gallery_image_t *out, const char **reason,
                              void (*read_done)(void *ctx), void *ctx);

void gallery_image_free(gallery_image_t *img);

/**
 * @brief  Scale the region (sx, sy, sw, sh) of `src` into `dst` (dst_w x dst_h)
 *
 *         Shrinking averages every source pixel; enlarging interpolates bilinearly.
 */
void gallery_resample(const gallery_image_t *src, int sx, int sy, int sw, int sh, uint16_t *dst, int dst_w,
                      int dst_h);

#ifdef __cplusplus
}
#endif
