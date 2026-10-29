/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"

#define SAMPLE_RATE_HZ      16000
#define CHANNEL_COUNT       2
#define TONE_HZ             440
#define TONE_DURATION_MS    240
#define TONE_INTERVAL_MS    2000
#define TONE_VOLUME         20
#define CHUNK_FRAMES        160
#define TONE_AMPLITUDE      1600

static const char *TAG = "13_speaker_output";

static int16_t tone_sample(uint32_t phase)
{
    const uint32_t position = phase >> 16;
    const int32_t triangle = position < 32768U ? (int32_t)(position * 2U) :
                             (int32_t)((65535U - position) * 2U);

    return (int16_t)(((triangle - 32768) * TONE_AMPLITUDE) / 32768);
}

static esp_err_t play_tone(esp_codec_dev_handle_t speaker)
{
    esp_codec_dev_sample_info_t format = {
        .sample_rate = SAMPLE_RATE_HZ,
        .channel = CHANNEL_COUNT,
        .bits_per_sample = 16,
    };
    int16_t samples[CHUNK_FRAMES * CHANNEL_COUNT];
    const uint32_t phase_step = (uint32_t)(((uint64_t)TONE_HZ << 32) / SAMPLE_RATE_HZ);
    const size_t frames = SAMPLE_RATE_HZ * TONE_DURATION_MS / 1000;
    uint32_t phase = 0;

    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(speaker, TONE_VOLUME) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG,
                        "set speaker volume failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(speaker, &format) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG,
                        "open speaker failed");

    for (size_t offset = 0; offset < frames; offset += CHUNK_FRAMES) {
        const size_t chunk_frames = (frames - offset < CHUNK_FRAMES) ? frames - offset : CHUNK_FRAMES;
        for (size_t frame = 0; frame < chunk_frames; ++frame) {
            const int16_t sample = tone_sample(phase);
            samples[frame * CHANNEL_COUNT] = sample;
            samples[frame * CHANNEL_COUNT + 1] = sample;
            phase += phase_step;
        }
        ESP_RETURN_ON_FALSE(esp_codec_dev_write(speaker, samples, chunk_frames * CHANNEL_COUNT * sizeof(int16_t)) ==
                                ESP_CODEC_DEV_OK,
                            ESP_FAIL, TAG, "write speaker samples failed");
    }

    return ESP_OK;
}

void app_main(void)
{
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    if (speaker == NULL) {
        ESP_LOGE(TAG, "speaker codec initialization failed");
        return;
    }

    ESP_LOGI(TAG, "playing a %d Hz tone for %d ms every %d ms", TONE_HZ, TONE_DURATION_MS, TONE_INTERVAL_MS);
    while (true) {
        const esp_err_t ret = play_tone(speaker);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "tone playback failed: %s", esp_err_to_name(ret));
        }
        (void)esp_codec_dev_close(speaker);
        vTaskDelay(pdMS_TO_TICKS(TONE_INTERVAL_MS));
    }
}
