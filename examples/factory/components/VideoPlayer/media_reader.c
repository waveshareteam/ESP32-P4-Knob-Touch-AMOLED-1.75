/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_extractor_defaults.h"
#include "media_reader.h"

/* The file is read in aligned 64 KB blocks, large enough for the card driver to transfer many
 * sectors per command. Both readers of a file share the blocks, and the one ahead stays within
 * LEAD_BLOCKS of the other, so what it loads is still cached when the other gets there. */
#define CACHE_BLOCK_SIZE    (64 * 1024)
#define CACHE_BLOCKS        16
#define LEAD_BLOCKS         (CACHE_BLOCKS - 4)
#define READ_RETRIES        100

static const char *TAG = "media_reader";

typedef struct {
    uint8_t *data;
    uint32_t pos;           // File offset of data[0]
    uint32_t len;           // Valid bytes
    uint32_t used;          // Last use, for replacing the oldest block
} cache_block_t;

struct media_file {
    int fd;
    uint32_t size;
    uint32_t fd_pos;        // Where the next read() of fd starts
    bool is_avi;
    SemaphoreHandle_t lock;
    cache_block_t blocks[CACHE_BLOCKS];
    uint32_t tick;
};

struct media_reader {
    media_file_t *file;
    uint32_t pos;           // Position the extractor reads from
    bool eos;
    esp_extractor_handle_t extractor;
    esp_extractor_stream_info_t info;
};

/* Copies what the cache holds at pos, reading the block from the card first if needed */
static uint32_t file_copy(media_file_t *file, uint32_t pos, uint8_t *out, uint32_t size)
{
    cache_block_t *block = NULL;
    cache_block_t *oldest = &file->blocks[0];
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        cache_block_t *b = &file->blocks[i];
        if (b->len && pos >= b->pos && pos < b->pos + b->len) {
            block = b;
            break;
        }
        if (b->used < oldest->used) {
            oldest = b;
        }
    }
    if (!block) {
        const uint32_t start = pos & ~(CACHE_BLOCK_SIZE - 1);
        if (start != file->fd_pos && lseek(file->fd, start, SEEK_SET) < 0) {
            return 0;
        }
        const ssize_t got = read(file->fd, oldest->data, CACHE_BLOCK_SIZE);
        file->fd_pos = got > 0 ? start + (uint32_t)got : UINT32_MAX;
        if (got <= 0) {
            oldest->len = 0;
            return 0;
        }
        oldest->pos = start;
        oldest->len = (uint32_t)got;
        block = oldest;
    }
    block->used = ++file->tick;
    uint32_t n = block->pos + block->len - pos;
    n = n < size ? n : size;
    memcpy(out, block->data + (pos - block->pos), n);
    return n;
}

static int file_read(void *dst, uint32_t size, void *ctx)
{
    media_reader_t *reader = ctx;
    media_file_t *file = reader->file;
    uint8_t *out = dst;
    uint32_t done = 0;
    xSemaphoreTake(file->lock, portMAX_DELAY);
    while (done < size && reader->pos < file->size) {
        const uint32_t n = file_copy(file, reader->pos, out + done, size - done);
        if (n == 0) {
            break;
        }
        done += n;
        reader->pos += n;
    }
    xSemaphoreGive(file->lock);
    return (int)done;
}

static int file_seek(uint32_t position, void *ctx)
{
    media_reader_t *reader = ctx;
    if (position > reader->file->size) {
        return -1;
    }
    reader->pos = position;
    return 0;
}

static uint32_t file_total_size(void *ctx)
{
    return ((media_reader_t *)ctx)->file->size;
}

static const char *extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

bool media_reader_is_video_file(const char *name)
{
    if (!name || name[0] == '.') {
        return false;   // Hidden files, including the "._" companions macOS leaves on cards
    }
    const char *ext = extension(name);
    return strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".m4v") == 0 ||
           strcasecmp(ext, ".avi") == 0;
}

static void register_extractors(void)
{
    static bool registered;
    if (!registered) {
        esp_mp4_extractor_register();
        esp_avi_extractor_register();
        registered = true;
    }
}

esp_err_t media_file_open(const char *path, media_file_t **ret_file)
{
    media_file_t *file = calloc(1, sizeof(*file));
    if (!file) {
        return ESP_ERR_NO_MEM;
    }
    file->fd = open(path, O_RDONLY);
    struct stat st;
    if (file->fd < 0 || fstat(file->fd, &st) != 0) {
        media_file_close(file);
        return ESP_FAIL;
    }
    file->size = (uint32_t)st.st_size;
    file->is_avi = strcasecmp(extension(path), ".avi") == 0;
    file->lock = xSemaphoreCreateMutex();
    bool ok = file->lock != NULL;
    for (int i = 0; ok && i < CACHE_BLOCKS; i++) {
        // Cache-line aligned PSRAM takes the card's DMA directly
        uint8_t *data = heap_caps_aligned_alloc(64, CACHE_BLOCK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        file->blocks[i].data = data;
        ok = data != NULL;
    }
    if (!ok) {
        media_file_close(file);
        return ESP_ERR_NO_MEM;
    }
    *ret_file = file;
    return ESP_OK;
}

void media_file_close(media_file_t *file)
{
    if (!file) {
        return;
    }
    if (file->fd >= 0) {
        close(file->fd);
    }
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        heap_caps_free(file->blocks[i].data);
    }
    if (file->lock) {
        vSemaphoreDelete(file->lock);
    }
    free(file);
}

esp_err_t media_reader_open(media_file_t *file, esp_extractor_stream_type_t stream_type, uint32_t pool_size,
                            media_reader_t **ret_reader)
{
    register_extractors();
    media_reader_t *reader = calloc(1, sizeof(*reader));
    if (!reader) {
        return ESP_ERR_NO_MEM;
    }
    reader->file = file;
    esp_extractor_config_t config = {
        .type = file->is_avi ? ESP_EXTRACTOR_TYPE_AVI : ESP_EXTRACTOR_TYPE_MP4,
        .extract_mask = stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO ? ESP_EXTRACT_MASK_VIDEO : ESP_EXTRACT_MASK_AUDIO,
        .in_read_cb = file_read,
        .in_seek_cb = file_seek,
        .in_size_cb = file_total_size,
        .in_ctx = reader,
        .out_pool_size = pool_size,
        .out_align = 64,
    };
    uint16_t streams = 0;
    esp_extractor_err_t err = esp_extractor_open(&config, &reader->extractor);
    if (err == ESP_EXTRACTOR_ERR_OK) {
        err = esp_extractor_parse_stream(reader->extractor);
    }
    if (err == ESP_EXTRACTOR_ERR_OK) {
        err = esp_extractor_get_stream_num(reader->extractor, stream_type, &streams);
    }
    if (err == ESP_EXTRACTOR_ERR_OK && streams == 0) {
        err = ESP_EXTRACTOR_ERR_NOT_FOUND;
    }
    if (err == ESP_EXTRACTOR_ERR_OK) {
        err = esp_extractor_get_stream_info(reader->extractor, stream_type, 0, &reader->info);
    }
    if (err != ESP_EXTRACTOR_ERR_OK) {
        if (err != ESP_EXTRACTOR_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "No %s stream (%d)", stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO ? "video" : "audio", err);
        }
        media_reader_close(reader);
        return err == ESP_EXTRACTOR_ERR_NOT_FOUND ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    *ret_reader = reader;
    return ESP_OK;
}

const esp_extractor_stream_info_t *media_reader_info(const media_reader_t *reader)
{
    return &reader->info;
}

esp_err_t media_reader_read(media_reader_t *reader, esp_extractor_frame_info_t *frame)
{
    if (reader->eos) {
        return ESP_ERR_NOT_FOUND;
    }
    for (int i = 0; i < READ_RETRIES; i++) {
        memset(frame, 0, sizeof(*frame));
        const esp_extractor_err_t err = esp_extractor_read_frame(reader->extractor, frame);
        if (err == ESP_EXTRACTOR_ERR_OK) {
            reader->eos = EXTRACTOR_IS_EOS(frame->frame_flag);
            if (frame->frame_size == 0) {
                media_reader_release(reader, frame);
                if (reader->eos) {
                    return ESP_ERR_NOT_FOUND;
                }
                continue;
            }
            return ESP_OK;
        }
        if (err == ESP_EXTRACTOR_ERR_EOS) {
            reader->eos = true;
            return ESP_ERR_NOT_FOUND;
        }
        if (err == ESP_EXTRACTOR_ERR_WAITING_OUTPUT) {
            vTaskDelay(1);  // Every frame is released before the next read, so this passes quickly
            continue;
        }
        if (err == ESP_EXTRACTOR_ERR_SKIPPED) {
            ESP_LOGW(TAG, "Frame larger than the pool skipped");
            continue;
        }
        ESP_LOGW(TAG, "Read frame failed (%d)", err);
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

void media_reader_release(media_reader_t *reader, esp_extractor_frame_info_t *frame)
{
    if (frame->frame_buffer) {
        esp_extractor_release_frame(reader->extractor, frame);
        frame->frame_buffer = NULL;
    }
}

esp_err_t media_reader_seek(media_reader_t *reader, uint32_t time_ms)
{
    reader->eos = false;
    return esp_extractor_seek(reader->extractor, time_ms) == ESP_EXTRACTOR_ERR_OK ? ESP_OK : ESP_FAIL;
}

bool media_reader_too_far_ahead(const media_reader_t *reader, const media_reader_t *other)
{
    return other && !other->eos && reader->pos > other->pos + LEAD_BLOCKS * CACHE_BLOCK_SIZE;
}

void media_reader_close(media_reader_t *reader)
{
    if (!reader) {
        return;
    }
    if (reader->extractor) {
        esp_extractor_close(reader->extractor);
    }
    free(reader);
}
