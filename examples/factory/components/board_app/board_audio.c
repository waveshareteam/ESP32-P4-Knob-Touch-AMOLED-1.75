/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_audio_codec.h"
#include "board_app.h"

static const char *TAG = "board_audio";

static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

static bool _is_audio_init = false;
static int _volume_intensity = BOARD_AUDIO_DEFAULT_VOLUME;

/* Keep the normal listening range intact, then reserve a small, explicit
 * boost for the top fifth of the user-facing volume control. The codec keeps
 * this curve across close/open; it is registered after every DAC reopen so a
 * later format change cannot silently fall back to the SDK default. */
static esp_codec_dev_vol_map_t s_playback_volume_points[] = {
    {.vol = 0, .db_value = -50.0f},
    {.vol = 80, .db_value = -10.0f},
    {.vol = 100, .db_value = 6.0f},
};

static esp_codec_dev_vol_curve_t s_playback_volume_curve = {
    .vol_map = s_playback_volume_points,
    .count = sizeof(s_playback_volume_points) / sizeof(s_playback_volume_points[0]),
};


esp_err_t board_audio_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    (void)timeout_ms;
    int ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    *bytes_read = len;
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    (void)timeout_ms;
    int ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    *bytes_written = len;
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    int ret = ESP_CODEC_DEV_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
    }

    if (play_dev_handle) {
        const int play_ret = esp_codec_dev_open(play_dev_handle, &fs);
        ret |= play_ret;
        if (play_ret == ESP_CODEC_DEV_OK) {
            ret |= esp_codec_dev_set_vol_curve(play_dev_handle, &s_playback_volume_curve);
        }
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, BOARD_AUDIO_DEFAULT_ADC_GAIN);
    }
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_volume_set(int volume, int *volume_set)
{
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(play_dev_handle, volume) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG,
                        "Set codec volume failed");
    _volume_intensity = volume;
    if (volume_set) {
        *volume_set = volume;
    }

    ESP_LOGD(TAG, "Setting volume: %d", volume);

    return ESP_OK;
}

int board_audio_volume_get(void)
{
    return _volume_intensity;
}

esp_err_t board_audio_mute_set(bool enable)
{
    return esp_codec_dev_set_out_mute(play_dev_handle, enable) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_stop(void)
{
    int ret = ESP_CODEC_DEV_OK;

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
    }
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_resume(void)
{
    return board_audio_set_fs(BOARD_AUDIO_DEFAULT_SAMPLE_RATE, BOARD_AUDIO_DEFAULT_BIT_WIDTH,
                              BOARD_AUDIO_DEFAULT_CHANNEL);
}

esp_err_t board_audio_init(void)
{
    if (_is_audio_init) {
        return ESP_OK;
    }

    dev_audio_codec_handles_t *dac = NULL;
    dev_audio_codec_handles_t *adc = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac), TAG,
                        "audio_dac handle missing");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc), TAG,
                        "audio_adc handle missing");
    play_dev_handle = dac->codec_dev;
    record_dev_handle = adc->codec_dev;

    ESP_RETURN_ON_ERROR(board_audio_resume(), TAG, "Open codec failed");

    _is_audio_init = true;

    return ESP_OK;
}
