/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/jpeg_decode.h"
#include "esp_jpeg_dec.h"
#include "png.h"
#include "gallery_decoder.h"

#define MAX_FILE_SIZE               (32 * 1024 * 1024)
#define MAX_DIMENSION               20000
/* The hardware decoder writes the whole picture at full size; bigger ones go to the scaling
 * software decoder instead of taking most of the PSRAM */
#define HW_MAX_OUTPUT_BYTES         (12 * 1024 * 1024)
#define HW_PSRAM_RESERVE            (3 * 1024 * 1024)
/* An interlaced PNG has to be held completely as RGBA before it can be shrunk */
#define PNG_MAX_INTERLACED_PIXELS   (2 * 1024 * 1024)
/* Buffers the decoder writes by DMA are whole cache lines, so invalidating them touches nothing else */
#define IMAGE_ALIGN                 (CONFIG_CACHE_L2_CACHE_LINE_SIZE > 16 ? CONFIG_CACHE_L2_CACHE_LINE_SIZE : 16)

#define ALIGN_UP(v, a)              ((((v) + (a) - 1) / (a)) * (a))

static const char *TAG = "gallery_dec";

static jpeg_decoder_handle_t s_hw;

typedef struct {
    int width;
    int height;
    int components;
    int mcu_w;
    int mcu_h;
    bool baseline;
    bool progressive;
    int orientation;    // EXIF orientation, 1 = upright
} jpeg_info_t;

static uint16_t *image_alloc(int width, int height)
{
    return heap_caps_aligned_alloc(IMAGE_ALIGN, (size_t)width * height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void gallery_image_free(gallery_image_t *img)
{
    if (img) {
        free(img->pixels);
        img->pixels = NULL;
        img->width = img->height = 0;
    }
}

esp_err_t gallery_decoder_init(void)
{
    if (s_hw) {
        return ESP_OK;
    }
    const jpeg_decode_engine_cfg_t config = {
        .intr_priority = 0,
        .timeout_ms = 1000,
    };
    return jpeg_new_decoder_engine(&config, &s_hw);
}

void gallery_decoder_deinit(void)
{
    if (s_hw) {
        jpeg_del_decoder_engine(s_hw);
        s_hw = NULL;
    }
}

bool gallery_is_picture(const char *name)
{
    if (!name || name[0] == '.') {
        return false;   // Hidden files, including the "._" companions macOS leaves on cards
    }
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0);
}

/* Large pictures shrink until the shorter side covers the screen; small ones keep their size */
static void target_size(int width, int height, int cover, int max_side, int *tw, int *th)
{
    float scale = fmaxf((float)cover / width, (float)cover / height);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    const int longest = width > height ? width : height;
    if (longest * scale > max_side) {
        scale = (float)max_side / longest;
    }
    *tw = (int)lroundf(width * scale);
    *th = (int)lroundf(height * scale);
    *tw = *tw > 0 ? *tw : 1;
    *th = *th > 0 ? *th : 1;
}

/* ------------------------------------------------------------------------------------------ */
/* Resampling                                                                                 */
/* ------------------------------------------------------------------------------------------ */

static inline uint16_t pack565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void resample_box(const gallery_image_t *src, int sx, int sy, int sw, int sh, uint16_t *dst, int dw, int dh)
{
    int *cols = malloc((dw + 1) * sizeof(int));
    uint32_t *acc = malloc(dw * 3 * sizeof(uint32_t));
    if (!cols || !acc) {
        free(cols);
        free(acc);
        memset(dst, 0, (size_t)dw * dh * 2);
        return;
    }
    for (int x = 0; x <= dw; x++) {
        cols[x] = sx + (int)((int64_t)x * sw / dw);
    }
    for (int y = 0; y < dh; y++) {
        const int y0 = sy + (int)((int64_t)y * sh / dh);
        const int y1 = sy + (int)((int64_t)(y + 1) * sh / dh);
        memset(acc, 0, dw * 3 * sizeof(uint32_t));
        for (int yy = y0; yy < y1; yy++) {
            const uint16_t *row = src->pixels + (size_t)yy * src->width;
            uint32_t *a = acc;
            for (int x = 0; x < dw; x++, a += 3) {
                for (int xx = cols[x]; xx < cols[x + 1]; xx++) {
                    const uint16_t p = row[xx];
                    a[0] += p >> 11;
                    a[1] += (p >> 5) & 0x3F;
                    a[2] += p & 0x1F;
                }
            }
        }
        uint16_t *out = dst + (size_t)y * dw;
        const uint32_t *a = acc;
        for (int x = 0; x < dw; x++, a += 3) {
            const uint32_t n = (uint32_t)(y1 - y0) * (cols[x + 1] - cols[x]);
            out[x] = pack565((a[0] + n / 2) / n, (a[1] + n / 2) / n, (a[2] + n / 2) / n);
        }
    }
    free(cols);
    free(acc);
}

static void resample_bilinear(const gallery_image_t *src, int sx, int sy, int sw, int sh, uint16_t *dst, int dw,
                              int dh)
{
    // 16.16 fixed point; pixel centres map onto pixel centres
    const int32_t step_x = (int32_t)(((int64_t)sw << 16) / dw);
    const int32_t step_y = (int32_t)(((int64_t)sh << 16) / dh);
    for (int y = 0; y < dh; y++) {
        int32_t fy = (int32_t)(((int64_t)y * step_y) + step_y / 2 - 0x8000);
        fy = fy < 0 ? 0 : fy;
        int y0 = fy >> 16;
        const uint32_t wy = (fy >> 8) & 0xFF;
        if (y0 >= sh - 1) {
            y0 = sh - 1;
        }
        const int y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
        const uint16_t *r0 = src->pixels + (size_t)(sy + y0) * src->width + sx;
        const uint16_t *r1 = src->pixels + (size_t)(sy + y1) * src->width + sx;
        uint16_t *out = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int32_t fx = (int32_t)(((int64_t)x * step_x) + step_x / 2 - 0x8000);
            fx = fx < 0 ? 0 : fx;
            int x0 = fx >> 16;
            const uint32_t wx = (fx >> 8) & 0xFF;
            if (x0 >= sw - 1) {
                x0 = sw - 1;
            }
            const int x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
            const uint16_t p00 = r0[x0], p01 = r0[x1], p10 = r1[x0], p11 = r1[x1];
            const uint32_t w00 = (256 - wx) * (256 - wy), w01 = wx * (256 - wy);
            const uint32_t w10 = (256 - wx) * wy, w11 = wx * wy;
            const uint32_t r = ((p00 >> 11) * w00 + (p01 >> 11) * w01 + (p10 >> 11) * w10 + (p11 >> 11) * w11 + 32768) >> 16;
            const uint32_t g = (((p00 >> 5) & 0x3F) * w00 + ((p01 >> 5) & 0x3F) * w01 + ((p10 >> 5) & 0x3F) * w10 +
                                ((p11 >> 5) & 0x3F) * w11 + 32768) >> 16;
            const uint32_t b = ((p00 & 0x1F) * w00 + (p01 & 0x1F) * w01 + (p10 & 0x1F) * w10 + (p11 & 0x1F) * w11 +
                                32768) >> 16;
            out[x] = pack565(r, g, b);
        }
    }
}

void gallery_resample(const gallery_image_t *src, int sx, int sy, int sw, int sh, uint16_t *dst, int dst_w, int dst_h)
{
    if (sw == dst_w && sh == dst_h) {
        for (int y = 0; y < dst_h; y++) {
            memcpy(dst + (size_t)y * dst_w, src->pixels + (size_t)(sy + y) * src->width + sx, (size_t)dst_w * 2);
        }
    } else if (dst_w <= sw && dst_h <= sh) {
        resample_box(src, sx, sy, sw, sh, dst, dst_w, dst_h);
    } else {
        resample_bilinear(src, sx, sy, sw, sh, dst, dst_w, dst_h);
    }
}

/* Shrinks the visible part of a decoded picture into a new packed image */
static esp_err_t shrink_into(const gallery_image_t *src, int width, int height, int tw, int th, gallery_image_t *out)
{
    out->pixels = image_alloc(tw, th);
    if (!out->pixels) {
        return ESP_ERR_NO_MEM;
    }
    out->width = tw;
    out->height = th;
    gallery_resample(src, 0, 0, width, height, out->pixels, tw, th);
    return ESP_OK;
}

/* EXIF orientation 2..8 says how the stored picture must be flipped or turned to stand upright */
static void apply_orientation(gallery_image_t *img, int orientation)
{
    if (orientation <= 1 || orientation > 8) {
        return;
    }
    const int w = img->width;
    const int h = img->height;
    const int dw = orientation >= 5 ? h : w;
    const int dh = orientation >= 5 ? w : h;
    uint16_t *dst = image_alloc(dw, dh);
    if (!dst) {
        return;     // Shown as stored rather than not at all
    }
    for (int y = 0; y < dh; y++) {
        uint16_t *out = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            int sx;
            int sy;
            switch (orientation) {
            case 2: sx = w - 1 - x; sy = y; break;
            case 3: sx = w - 1 - x; sy = h - 1 - y; break;
            case 4: sx = x; sy = h - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = h - 1 - x; break;
            case 7: sx = w - 1 - y; sy = h - 1 - x; break;
            default: sx = w - 1 - y; sy = x; break;  // 8
            }
            out[x] = img->pixels[(size_t)sy * w + sx];
        }
    }
    free(img->pixels);
    img->pixels = dst;
    img->width = dw;
    img->height = dh;
}

/* ------------------------------------------------------------------------------------------ */
/* JPEG                                                                                       */
/* ------------------------------------------------------------------------------------------ */

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint16_t tiff16(const uint8_t *p, bool le)
{
    return le ? (uint16_t)(p[0] | (p[1] << 8)) : be16(p);
}

static uint32_t tiff32(const uint8_t *p, bool le)
{
    return le ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24) :
           ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Reads tag 0x0112 from IFD0 of the TIFF block inside an Exif APP1 segment */
static int exif_orientation(const uint8_t *tiff, size_t len)
{
    if (len < 8 || !((tiff[0] == 'I' && tiff[1] == 'I') || (tiff[0] == 'M' && tiff[1] == 'M'))) {
        return 1;
    }
    const bool le = tiff[0] == 'I';
    const uint32_t ifd = tiff32(tiff + 4, le);
    if ((size_t)ifd + 2 > len) {
        return 1;
    }
    const int count = tiff16(tiff + ifd, le);
    for (int i = 0; i < count; i++) {
        const size_t entry = ifd + 2 + 12 * (size_t)i;
        if (entry + 12 > len) {
            break;
        }
        if (tiff16(tiff + entry, le) == 0x0112) {
            const int value = tiff16(tiff + entry + 8, le);
            return (value >= 1 && value <= 8) ? value : 1;
        }
    }
    return 1;
}

/* Walks the markers up to the scan: frame type, size, sampling and EXIF orientation */
static bool jpeg_parse(const uint8_t *data, size_t len, jpeg_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->orientation = 1;
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    size_t pos = 2;
    while (pos + 4 <= len) {
        if (data[pos] != 0xFF) {
            return false;
        }
        const uint8_t marker = data[pos + 1];
        if (marker == 0xFF) {
            pos++;          // Fill byte
            continue;
        }
        pos += 2;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {
            continue;       // Markers without a length
        }
        if (marker == 0xD9 || marker == 0xDA) {
            break;          // End of image or start of scan: the header is over
        }
        const size_t seg = be16(data + pos);
        if (seg < 2 || pos + seg > len) {
            return false;
        }
        const uint8_t *s = data + pos + 2;
        const size_t slen = seg - 2;
        if (marker == 0xE1 && slen > 6 && memcmp(s, "Exif\0\0", 6) == 0) {
            info->orientation = exif_orientation(s + 6, slen - 6);
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (slen < 6) {
                return false;
            }
            info->height = be16(s + 1);
            info->width = be16(s + 3);
            info->components = s[5];
            info->baseline = marker == 0xC0 || marker == 0xC1;
            info->progressive = marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE;
            int hmax = 1;
            int vmax = 1;
            for (int c = 0; c < info->components && 6 + 3 * (size_t)c + 2 < slen; c++) {
                const uint8_t factors = s[6 + 3 * c + 1];
                hmax = (factors >> 4) > hmax ? (factors >> 4) : hmax;
                vmax = (factors & 0x0F) > vmax ? (factors & 0x0F) : vmax;
            }
            info->mcu_w = 8 * hmax;
            info->mcu_h = 8 * vmax;
        }
        pos += seg;
    }
    return info->width > 0 && info->height > 0;
}

/* The hardware decoder only takes sizes in whole 8 px blocks. The coded data always covers whole
 * MCUs, so declaring the MCU-aligned size decodes the same picture plus a margin that is cropped */
static bool jpeg_align_size(uint8_t *data, size_t len, int width, int height)
{
    size_t pos = 2;
    while (pos + 9 <= len && data[pos] == 0xFF) {
        const uint8_t marker = data[pos + 1];
        if (marker == 0xFF) {
            pos++;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            return false;
        }
        if (marker >= 0xC0 && marker <= 0xC1) {
            data[pos + 5] = (uint8_t)(height >> 8);
            data[pos + 6] = (uint8_t)height;
            data[pos + 7] = (uint8_t)(width >> 8);
            data[pos + 8] = (uint8_t)width;
            return true;
        }
        pos += 2 + be16(data + pos + 2);
    }
    return false;
}

static esp_err_t jpeg_decode_hw(uint8_t *data, size_t len, const jpeg_info_t *info, gallery_image_t *full)
{
    const int stride = ALIGN_UP(info->width, info->mcu_w);
    const int rows = ALIGN_UP(info->height, info->mcu_h);
    const size_t need = (size_t)stride * rows * 2;
    if (!s_hw || need > HW_MAX_OUTPUT_BYTES || (info->components != 1 && info->components != 3) ||
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < need + HW_PSRAM_RESERVE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const bool resized = info->width % 8 || info->height % 8;
    if (resized && !jpeg_align_size(data, len, stride, rows)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    // Unlike jpeg_alloc_decoder_mem() this skips zero-filling megabytes the decoder overwrites
    const size_t got = ALIGN_UP(need, IMAGE_ALIGN);
    uint16_t *pixels = heap_caps_aligned_alloc(IMAGE_ALIGN, got, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t ret = pixels ? ESP_OK : ESP_ERR_NO_MEM;
    uint32_t out_size = 0;
    if (ret == ESP_OK) {
        const jpeg_decode_cfg_t config = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
        };
        ret = jpeg_decoder_process(s_hw, &config, data, len, (uint8_t *)pixels, got, &out_size);
        if (ret == ESP_OK && out_size < need) {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    if (ret != ESP_OK) {
        free(pixels);
        if (resized) {
            jpeg_align_size(data, len, info->width, info->height);  // The software decoder gets the real size
        }
        return ret;
    }
    full->pixels = pixels;
    full->width = stride;
    full->height = rows;
    return ESP_OK;
}

static esp_err_t jpeg_decode_sw(const uint8_t *data, size_t len, const jpeg_info_t *info, int tw, int th,
                                gallery_image_t *full)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    // The scaler takes multiples of 8 and at most 1/8; the rest is resampled afterwards
    int sw = ALIGN_UP(tw > info->width / 8 ? tw : info->width / 8, 8);
    int sh = ALIGN_UP(th > info->height / 8 ? th : info->height / 8, 8);
    if (sw < info->width && sh < info->height) {
        config.scale.width = sw;
        config.scale.height = sh;
    }
    jpeg_dec_handle_t dec = NULL;
    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)data,
        .inbuf_len = (int)len,
    };
    jpeg_dec_header_info_t header = {0};
    int out_len = 0;
    uint8_t *pixels = NULL;
    jpeg_error_t err = jpeg_dec_open(&config, &dec);
    if (err == JPEG_ERR_OK) {
        err = jpeg_dec_parse_header(dec, &io, &header);
    }
    if (err == JPEG_ERR_OK) {
        err = jpeg_dec_get_outbuf_len(dec, &out_len);
    }
    if (err == JPEG_ERR_OK && out_len > 0) {
        pixels = heap_caps_aligned_alloc(IMAGE_ALIGN, out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        err = pixels ? JPEG_ERR_OK : JPEG_ERR_NO_MEM;
    }
    if (err == JPEG_ERR_OK) {
        io.outbuf = pixels;
        err = jpeg_dec_process(dec, &io);
    }
    if (dec) {
        jpeg_dec_close(dec);
    }
    if (err != JPEG_ERR_OK) {
        free(pixels);
        ESP_LOGW(TAG, "Software JPEG decode failed (%d)", err);
        return err == JPEG_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    full->pixels = (uint16_t *)pixels;
    full->width = config.scale.width ? config.scale.width : header.width;
    full->height = config.scale.height ? config.scale.height : header.height;
    return ESP_OK;
}

static esp_err_t jpeg_decode(uint8_t *data, size_t len, int cover, int max_side, gallery_image_t *out,
                             const char **reason, const char **method)
{
    jpeg_info_t info;
    if (!jpeg_parse(data, len, &info)) {
        *reason = "This JPEG is damaged";
        return ESP_FAIL;
    }
    if (info.progressive) {
        *reason = "Progressive JPEG is not supported";
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!info.baseline || (info.components != 1 && info.components != 3)) {
        *reason = "This kind of JPEG is not supported";
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info.width > MAX_DIMENSION || info.height > MAX_DIMENSION) {
        *reason = "This picture is too large";
        return ESP_ERR_NOT_SUPPORTED;
    }

    int tw;
    int th;
    target_size(info.width, info.height, cover, max_side, &tw, &th);
    gallery_image_t full = {0};
    esp_err_t ret = jpeg_decode_hw(data, len, &info, &full);
    if (ret == ESP_OK) {
        *method = "hardware JPEG";
        ret = shrink_into(&full, info.width, info.height, tw, th, out);
        gallery_image_free(&full);
    } else {
        *method = "software JPEG";
        ret = jpeg_decode_sw(data, len, &info, tw, th, &full);
        if (ret == ESP_OK && (full.width != tw || full.height != th)) {
            ret = shrink_into(&full, full.width, full.height, tw, th, out);
            gallery_image_free(&full);
        } else if (ret == ESP_OK) {
            *out = full;
        }
        if (ret != ESP_OK) {
            *reason = ret == ESP_ERR_NO_MEM ? "Not enough memory for this picture" : "This JPEG can't be decoded";
        }
    }
    if (ret == ESP_OK) {
        apply_orientation(out, info.orientation);
    }
    return ret;
}

/* ------------------------------------------------------------------------------------------ */
/* PNG                                                                                        */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    // Everything below is freed after a longjmp, so it lives in memory rather than registers
    uint8_t *row;
    uint8_t *image;
    png_bytep *rows;
    uint32_t *acc;
    gallery_image_t pre;
} png_job_t;

static void png_read_mem(png_structp png, png_bytep out, png_size_t count)
{
    png_job_t *job = png_get_io_ptr(png);
    if (job->pos + count > job->len) {
        png_error(png, "Unexpected end of file");
    }
    memcpy(out, job->data + job->pos, count);
    job->pos += count;
}

/* Adds one RGBA row to the k x k box sums; every k-th row emits a row of the shrunk picture */
static void png_take_row(png_job_t *job, const uint8_t *rgba, int y, int k, int pw, int ph)
{
    const int out_y = y / k;
    if (out_y >= ph) {
        return;
    }
    uint32_t *acc = job->acc;
    for (int x = 0; x < pw * k; x++) {
        const uint8_t *p = rgba + 4 * x;
        const uint32_t a = p[3];
        uint32_t *s = acc + 3 * (x / k);
        // Transparent parts sit on the black screen
        s[0] += (p[0] * a + 127) / 255;
        s[1] += (p[1] * a + 127) / 255;
        s[2] += (p[2] * a + 127) / 255;
    }
    if (y % k != k - 1) {
        return;
    }
    const uint32_t n = (uint32_t)k * k;
    uint16_t *out = job->pre.pixels + (size_t)out_y * pw;
    for (int x = 0; x < pw; x++) {
        const uint32_t *s = acc + 3 * x;
        const uint32_t r = (s[0] + n / 2) / n;
        const uint32_t g = (s[1] + n / 2) / n;
        const uint32_t b = (s[2] + n / 2) / n;
        out[x] = pack565(r >> 3, g >> 2, b >> 3);
    }
    memset(acc, 0, (size_t)pw * 3 * sizeof(uint32_t));
}

static esp_err_t png_decode(const uint8_t *data, size_t len, int cover, int max_side, gallery_image_t *out,
                            const char **reason)
{
    png_job_t job_storage = {
        .data = data,
        .len = len,
    };
    png_job_t *const job = &job_storage;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        *reason = "Not enough memory for this picture";
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_FAIL;
    if (setjmp(png_jmpbuf(png))) {
        *reason = "This PNG is damaged";
        goto done;
    }
    png_set_read_fn(png, job, png_read_mem);
    png_set_user_limits(png, MAX_DIMENSION, MAX_DIMENSION);
    png_read_info(png, info);
    const int width = (int)png_get_image_width(png, info);
    const int height = (int)png_get_image_height(png, info);
    // Every format arrives as 8-bit RGBA
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    const int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    const size_t row_bytes = png_get_rowbytes(png, info);
    if (row_bytes != (size_t)width * 4) {
        *reason = "This kind of PNG is not supported";
        ret = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }

    int tw;
    int th;
    target_size(width, height, cover, max_side, &tw, &th);
    // Whole-pixel boxes while streaming; the remaining factor below 2 is resampled afterwards
    int k = width / tw < height / th ? width / tw : height / th;
    k = k > 0 ? k : 1;
    const int pw = width / k;
    const int ph = height / k;
    job->pre.pixels = image_alloc(pw, ph);
    job->acc = calloc((size_t)pw * 3, sizeof(uint32_t));
    if (!job->pre.pixels || !job->acc) {
        *reason = "Not enough memory for this picture";
        ret = ESP_ERR_NO_MEM;
        goto done;
    }
    job->pre.width = pw;
    job->pre.height = ph;

    if (passes > 1) {
        if ((size_t)width * height > PNG_MAX_INTERLACED_PIXELS) {
            *reason = "Interlaced PNG this large is not supported";
            ret = ESP_ERR_NOT_SUPPORTED;
            goto done;
        }
        job->image = heap_caps_malloc(row_bytes * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        job->rows = malloc(height * sizeof(png_bytep));
        if (!job->image || !job->rows) {
            *reason = "Not enough memory for this picture";
            ret = ESP_ERR_NO_MEM;
            goto done;
        }
        for (int y = 0; y < height; y++) {
            job->rows[y] = job->image + row_bytes * y;
        }
        png_read_image(png, job->rows);
        for (int y = 0; y < height; y++) {
            png_take_row(job, job->rows[y], y, k, pw, ph);
        }
    } else {
        job->row = malloc(row_bytes);
        if (!job->row) {
            *reason = "Not enough memory for this picture";
            ret = ESP_ERR_NO_MEM;
            goto done;
        }
        for (int y = 0; y < height; y++) {
            png_read_row(png, job->row, NULL);
            png_take_row(job, job->row, y, k, pw, ph);
        }
    }

    if (pw == tw && ph == th) {
        *out = job->pre;
        job->pre.pixels = NULL;
        ret = ESP_OK;
    } else {
        ret = shrink_into(&job->pre, pw, ph, tw, th, out);
        if (ret != ESP_OK) {
            *reason = "Not enough memory for this picture";
        }
    }

done:
    png_destroy_read_struct(&png, &info, NULL);
    free(job->row);
    free(job->image);
    free(job->rows);
    free(job->acc);
    gallery_image_free(&job->pre);
    return ret;
}

/* ------------------------------------------------------------------------------------------ */

static uint8_t *read_file(const char *path, size_t *len, const char **reason)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        *reason = "This file can't be opened";
        return NULL;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size <= 0) {
        fclose(f);
        *reason = "This file is empty";
        return NULL;
    }
    if (st.st_size > MAX_FILE_SIZE) {
        fclose(f);
        *reason = "This file is too large";
        return NULL;
    }
    const size_t size = st.st_size;
    // Cache-line aligned so the card driver can read into it directly
    uint8_t *data = heap_caps_aligned_alloc(IMAGE_ALIGN, ALIGN_UP(size, IMAGE_ALIGN), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        fclose(f);
        *reason = "Not enough memory for this picture";
        return NULL;
    }
    const size_t got = fread(data, 1, size, f);
    fclose(f);
    if (got != size) {
        free(data);
        *reason = "This file can't be read";
        return NULL;
    }
    *len = size;
    return data;
}

esp_err_t gallery_decode_file(const char *path, int cover, int max_side, gallery_image_t *out, const char **reason,
                              void (*read_done)(void *ctx), void *ctx)
{
    memset(out, 0, sizeof(*out));
    *reason = NULL;
    const int64_t start = esp_timer_get_time();
    size_t len = 0;
    uint8_t *data = read_file(path, &len, reason);
    if (read_done) {
        read_done(ctx);
    }
    if (!data) {
        return ESP_FAIL;
    }
    const int64_t read_us = esp_timer_get_time() - start;

    esp_err_t ret;
    const char *method = "PNG";
    // The content decides, not the file name
    if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        ret = png_decode(data, len, cover, max_side, out, reason);
    } else if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        ret = jpeg_decode(data, len, cover, max_side, out, reason, &method);
    } else {
        *reason = "Not a JPEG or PNG picture";
        ret = ESP_ERR_NOT_SUPPORTED;
    }
    free(data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s: %u KB, read %lld ms, %s %lld ms -> %dx%d", path, (unsigned)(len / 1024), read_us / 1000,
                 method, (esp_timer_get_time() - start - read_us) / 1000, out->width, out->height);
    } else {
        ESP_LOGW(TAG, "%s: %s", path, *reason ? *reason : esp_err_to_name(ret));
        gallery_image_free(out);
    }
    return ret;
}
