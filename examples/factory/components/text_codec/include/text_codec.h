/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_CODEC_REPLACEMENT  0xFFFD

/**
 * @brief  Unicode code point of a two-byte GBK character, or 0 when GBK assigns none
 */
uint32_t text_codec_gbk(uint8_t lead, uint8_t trail);

/**
 * @brief  Decode one character of GBK text
 *
 * @return Bytes used (1, 2 or 4), or 0 when len ends inside a character.
 *         An invalid byte decodes to TEXT_CODEC_REPLACEMENT and uses 1 byte; a four-byte
 *         GB18030 sequence decodes to TEXT_CODEC_REPLACEMENT and uses all 4.
 */
size_t text_codec_gbk_decode(const uint8_t *data, size_t len, uint32_t *cp);

/**
 * @brief  Decode one character of UTF-8 text
 *
 * @return Bytes used (1 to 4), or 0 when len ends inside a character.
 *         An invalid byte decodes to TEXT_CODEC_REPLACEMENT and uses 1 byte.
 */
size_t text_codec_utf8_decode(const uint8_t *data, size_t len, uint32_t *cp);

/**
 * @brief  Write a code point as UTF-8
 *
 * @param  out  Room for 4 bytes; no terminator is written
 * @return Bytes written
 */
size_t text_codec_utf8_encode(uint32_t cp, char *out);

/**
 * @brief  Whether data is valid UTF-8, ignoring a character cut off at the end
 */
bool text_codec_is_utf8(const uint8_t *data, size_t len);

/**
 * @brief  Convert GBK text to a NUL-terminated UTF-8 string
 *
 * @param  out_size  Size of out; the result is cut at a character boundary to fit
 * @return Length of the result without the terminator
 */
size_t text_codec_gbk_to_utf8(const uint8_t *data, size_t len, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
