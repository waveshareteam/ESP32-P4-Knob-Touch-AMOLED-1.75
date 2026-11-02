/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_service.h"
#include "pomodoro_chime.h"

#define SAMPLE_RATE     16000
#define CHUNK_FRAMES    256
#define NOTE_MS         170
#define AMPLITUDE       0.35f

static const char *TAG = "pomodoro_chime";

static audio_client_handle_t s_client;
static volatile bool s_playing;

/* Called with the audio ownership switch serialized. Do not let a queued chime
 * take the speaker after another application has started. */
static bool chime_still_needed(void *user_ctx)
{
    (void)user_ctx;
    return s_playing && audio_service_owner_name() == NULL;
}

static void chime_task(void *arg)
{
    (void)arg;
    // A rising A major arpeggio
    static const float NOTES_HZ[] = {880.0f, 1108.7f, 1318.5f};
    int16_t chunk[CHUNK_FRAMES * 2];

    if (audio_client_acquire_if(s_client, chime_still_needed, NULL) == ESP_OK &&
            audio_client_set_format(s_client, SAMPLE_RATE, 16, 2) == ESP_OK) {
        const int note_frames = SAMPLE_RATE * NOTE_MS / 1000;
        for (size_t n = 0; n < sizeof(NOTES_HZ) / sizeof(NOTES_HZ[0]); n++) {
            const float step = 2.0f * (float)M_PI * NOTES_HZ[n] / SAMPLE_RATE;
            for (int start = 0; start < note_frames; start += CHUNK_FRAMES) {
                for (int i = 0; i < CHUNK_FRAMES; i++) {
                    const int frame = start + i;
                    // Short attack, then an exponential decay like a struck bell
                    const float attack = fminf(1.0f, frame / (SAMPLE_RATE * 0.008f));
                    const float decay = expf(-3.5f * frame / note_frames);
                    const float v = (frame < note_frames) ? AMPLITUDE * attack * decay * sinf(step * frame) : 0.0f;
                    chunk[2 * i] = chunk[2 * i + 1] = (int16_t)(v * 32767.0f);
                }
                if (audio_client_write(s_client, chunk, sizeof(chunk)) != ESP_OK) {
                    break;  // Another app took the speaker
                }
            }
        }
    }
    audio_client_release(s_client);
    s_playing = false;
    vTaskDelete(NULL);
}

void pomodoro_chime_play(void)
{
    // Music or a call in progress wins; the vibration still marks the moment
    if (s_playing || audio_service_owner_name() != NULL) {
        return;
    }
    if (!s_client) {
        const audio_client_config_t config = {
            .name = "Pomodoro",
        };
        if (audio_client_register(&config, &s_client) != ESP_OK) {
            ESP_LOGW(TAG, "Register audio client failed");
            return;
        }
    }
    s_playing = true;
    if (xTaskCreate(chime_task, "pomo_chime", 4096, NULL, 5, NULL) != pdPASS) {
        s_playing = false;
    }
}
