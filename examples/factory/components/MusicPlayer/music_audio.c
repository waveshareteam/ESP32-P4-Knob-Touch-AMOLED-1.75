/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// fopencookie() needs _GNU_SOURCE, which the IDF build defines
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "audio_player.h"
#include "audio_service.h"
#include "music_audio.h"

static const char *TAG = "music_audio";

static audio_client_handle_t s_client;
static bool s_player_ready;
static volatile bool s_preempted;
static volatile bool s_finished;
static volatile bool s_stopping;    // An IDLE event after this is a stop, not the end of the file
static volatile uint64_t s_bytes_played;

/* Last format requested by the decoder, reapplied when audio is taken back mid-track. */
static uint32_t s_rate;
static uint32_t s_bits;
static i2s_slot_mode_t s_channels;

static void on_preempt(audio_client_handle_t client, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    s_preempted = true;
    audio_player_pause();
}

static void on_player_event(audio_player_cb_ctx_t *ctx)
{
    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        if (!s_stopping) {
            s_finished = true;
        }
        s_stopping = false;
    }
}

/* The board has one speaker, so stereo frames carry (L + R) / 2 on both channels. */
static void mix_to_mono(int16_t *samples, size_t count)
{
    for (size_t i = 0; i + 1 < count; i += 2) {
        const int16_t mixed = (int16_t)(((int32_t)samples[i] + samples[i + 1]) / 2);
        samples[i] = mixed;
        samples[i + 1] = mixed;
    }
}

static esp_err_t player_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_bits == 16 && s_channels == I2S_SLOT_MODE_STEREO) {
        mix_to_mono((int16_t *)audio_buffer, len / sizeof(int16_t));
    }
    esp_err_t ret = audio_client_write(s_client, audio_buffer, len);
    *bytes_written = ret == ESP_OK ? len : 0;
    if (ret == ESP_OK) {
        s_bytes_played += len;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        // Preempted: the pause request is still in the decoder's queue; do not spin.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ret;
}

static esp_err_t player_set_clock(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    s_rate = rate;
    s_bits = bits_cfg;
    s_channels = ch;
    return audio_client_set_format(s_client, rate, bits_cfg, ch);
}

static esp_err_t player_mute(AUDIO_PLAYER_MUTE_SETTING setting)
{
    audio_client_set_mute(s_client, setting == AUDIO_PLAYER_MUTE);
    return ESP_OK;
}

/* A view of a file that starts at an offset, so the decoder never sees the tag in front */
typedef struct {
    FILE *fp;
    long base;
} offset_file_t;

static ssize_t offset_file_read(void *cookie, char *buf, size_t n)
{
    offset_file_t *f = (offset_file_t *)cookie;
    return (ssize_t)fread(buf, 1, n, f->fp);
}

static int offset_file_seek(void *cookie, off_t *pos, int whence)
{
    offset_file_t *f = (offset_file_t *)cookie;
    const long target = (whence == SEEK_SET) ? f->base + (long)*pos : (long)*pos;
    if (fseek(f->fp, target, whence) != 0) {
        return -1;
    }
    *pos = (off_t)(ftell(f->fp) - f->base);
    return 0;
}

static int offset_file_close(void *cookie)
{
    offset_file_t *f = (offset_file_t *)cookie;
    const int ret = fclose(f->fp);
    free(f);
    return ret;
}

static FILE *open_from(const char *path, uint32_t offset)
{
    FILE *fp = fopen(path, "rb");
    if (!fp || offset == 0) {
        return fp;
    }
    offset_file_t *f = malloc(sizeof(*f));
    if (f && fseek(fp, (long)offset, SEEK_SET) == 0) {
        f->fp = fp;
        f->base = (long)offset;
        const cookie_io_functions_t io = {
            .read = offset_file_read,
            .seek = offset_file_seek,
            .close = offset_file_close,
        };
        FILE *view = fopencookie(f, "rb", io);
        if (view) {
            return view;
        }
    }
    free(f);
    fclose(fp);
    return NULL;
}

static esp_err_t take_audio(void)
{
    s_preempted = false;
    ESP_RETURN_ON_ERROR(audio_client_acquire(s_client), TAG, "Acquire audio failed");
    if (s_rate) {
        ESP_RETURN_ON_ERROR(audio_client_set_format(s_client, s_rate, s_bits, s_channels), TAG, "Restore format failed");
    }
    return ESP_OK;
}

esp_err_t music_audio_init(void)
{
    if (!s_client) {
        const audio_client_config_t config = {
            .name = "MusicPlayer",
            .on_preempt = on_preempt,
        };
        ESP_RETURN_ON_ERROR(audio_client_register(&config, &s_client), TAG, "Register audio client failed");
    }
    if (s_player_ready) {
        return ESP_OK;
    }

    audio_player_config_t config = {
        .mute_fn = player_mute,
        .clk_set_fn = player_set_clock,
        .write_fn = player_write,
        .priority = 5,
    };
    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "Create audio player failed");
    audio_player_callback_register(on_player_event, NULL);
    s_player_ready = true;
    return ESP_OK;
}

esp_err_t music_audio_deinit(void)
{
    if (s_player_ready) {
        audio_player_delete();
        s_player_ready = false;
    }
    s_rate = 0;
    s_finished = false;
    return audio_client_release(s_client);
}

esp_err_t music_audio_play_file(const char *path, uint32_t start_offset)
{
    ESP_RETURN_ON_FALSE(path, ESP_ERR_INVALID_ARG, TAG, "path is NULL");
    FILE *fp = open_from(path, start_offset);
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "Open %s failed", path);

    // The decoder only reports a format when it differs from the previous file, so
    // take_audio() restores the last reported one; a different format follows via
    // player_set_clock().
    esp_err_t ret = take_audio();
    if (ret == ESP_OK) {
        s_finished = false;
        s_bytes_played = 0;
        ret = audio_player_play(fp);
    }
    if (ret != ESP_OK) {
        fclose(fp);
        return ret;
    }
    ESP_LOGI(TAG, "Playing %s", path);
    return ESP_OK;
}

esp_err_t music_audio_resume(void)
{
    ESP_RETURN_ON_ERROR(take_audio(), TAG, "Resume failed");
    return audio_player_resume();
}

esp_err_t music_audio_pause(void)
{
    return audio_player_pause();
}

esp_err_t music_audio_stop(void)
{
    if (!s_player_ready || audio_player_get_state() == AUDIO_PLAYER_STATE_IDLE) {
        return ESP_OK;
    }
    s_stopping = true;
    return audio_player_stop();
}

bool music_audio_is_playing(void)
{
    return s_player_ready && audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING && !s_preempted;
}

uint32_t music_audio_position_ms(void)
{
    const uint32_t frame_bytes = (s_channels == I2S_SLOT_MODE_STEREO ? 2 : 1) * (s_bits ? s_bits / 8 : 2);
    if (!s_rate || !frame_bytes) {
        return 0;
    }
    return (uint32_t)(s_bytes_played / frame_bytes * 1000 / s_rate);
}

bool music_audio_take_preempted(void)
{
    bool preempted = s_preempted;
    s_preempted = false;
    return preempted;
}

bool music_audio_take_finished(void)
{
    bool finished = s_finished;
    s_finished = false;
    return finished;
}
