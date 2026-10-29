/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "text_codec.h"

#define GBK_LEAD_FIRST   0x81
#define GBK_TRAIL_FIRST  0x40
#define GBK_TRAILS       (0xFF - GBK_TRAIL_FIRST)

extern const uint16_t text_codec_gbk_table[];

uint32_t text_codec_gbk(uint8_t lead, uint8_t trail)
{
    if (lead < GBK_LEAD_FIRST || lead == 0xFF || trail < GBK_TRAIL_FIRST || trail == 0xFF) {
        return 0;
    }
    return text_codec_gbk_table[(lead - GBK_LEAD_FIRST) * GBK_TRAILS + (trail - GBK_TRAIL_FIRST)];
}

size_t text_codec_gbk_decode(const uint8_t *data, size_t len, uint32_t *cp)
{
    if (len == 0) {
        return 0;
    }
    const uint8_t b = data[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if (b == 0x80) {
        *cp = 0x20AC;   // Euro sign in the Windows code page
        return 1;
    }
    if (b == 0xFF) {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    if (len < 2) {
        return 0;
    }
    if (data[1] >= '0' && data[1] <= '9') {
        // GB18030 four-byte form, outside GBK
        if (len < 4) {
            return 0;
        }
        *cp = TEXT_CODEC_REPLACEMENT;
        return 4;
    }
    const uint32_t u = text_codec_gbk(b, data[1]);
    if (u == 0) {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    *cp = u;
    return 2;
}

size_t text_codec_utf8_decode(const uint8_t *data, size_t len, uint32_t *cp)
{
    if (len == 0) {
        return 0;
    }
    const uint8_t b = data[0];
    size_t n;
    uint32_t u;
    uint32_t min;
    if (b < 0x80) {
        *cp = b;
        return 1;
    } else if ((b & 0xE0) == 0xC0) {
        n = 2;
        u = b & 0x1F;
        min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        n = 3;
        u = b & 0x0F;
        min = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
        n = 4;
        u = b & 0x07;
        min = 0x10000;
    } else {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    for (size_t i = 1; i < n; i++) {
        if (i >= len) {
            return 0;
        }
        if ((data[i] & 0xC0) != 0x80) {
            *cp = TEXT_CODEC_REPLACEMENT;
            return 1;
        }
        u = (u << 6) | (data[i] & 0x3F);
    }
    if (u < min || u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    *cp = u;
    return n;
}

size_t text_codec_utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool text_codec_is_utf8(const uint8_t *data, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp;
        const size_t n = text_codec_utf8_decode(data + pos, len - pos, &cp);
        if (n == 0) {
            return true;    // Cut off at the end
        }
        if (cp == TEXT_CODEC_REPLACEMENT && n == 1) {
            return false;
        }
        pos += n;
    }
    return true;
}

size_t text_codec_gbk_to_utf8(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    size_t used = 0;
    if (out_size == 0) {
        return 0;
    }
    while (pos < len) {
        uint32_t cp;
        const size_t n = text_codec_gbk_decode(data + pos, len - pos, &cp);
        if (n == 0) {
            break;
        }
        char utf8[4];
        const size_t m = text_codec_utf8_encode(cp, utf8);
        if (used + m >= out_size) {
            break;
        }
        for (size_t i = 0; i < m; i++) {
            out[used++] = utf8[i];
        }
        pos += n;
    }
    out[used] = '\0';
    return used;
}
