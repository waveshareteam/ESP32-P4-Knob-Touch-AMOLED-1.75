/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "decoder/esp_audio_dec.h"
#include "decoder/impl/esp_aac_dec.h"
#include "decoder/impl/esp_mp3_dec.h"
#include "audio_track.h"

#define PCM_BUFFER_START    (16 * 1024)

static const char *TAG = "audio_track";

struct audio_track {
    esp_extractor_audio_stream_info_t info;
    esp_audio_dec_handle_t decoder;
    bool passthrough;
    uint8_t *pcm;
    uint32_t pcm_size;
    uint32_t sample_rate;
    uint8_t channels;
};

static void register_decoders(void)
{
    static bool registered;
    if (!registered) {
        // Already registered by another user is fine
        esp_aac_dec_register();
        esp_mp3_dec_register();
        registered = true;
    }
}

esp_err_t audio_track_open(const esp_extractor_audio_stream_info_t *info, audio_track_t **ret_track)
{
    const bool pcm = info->format == ESP_EXTRACTOR_AUDIO_FORMAT_PCM;
    if (!(pcm && info->bits_per_sample == 16) && info->format != ESP_EXTRACTOR_AUDIO_FORMAT_AAC &&
            info->format != ESP_EXTRACTOR_AUDIO_FORMAT_MP3) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info->channel < 1 || info->channel > 2) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    audio_track_t *track = calloc(1, sizeof(*track));
    if (!track) {
        return ESP_ERR_NO_MEM;
    }
    track->info = *info;
    track->passthrough = pcm;
    track->sample_rate = info->sample_rate;
    track->channels = info->channel;
    *ret_track = track;
    return ESP_OK;
}

/* AAC in MP4 usually arrives without ADTS headers, in other containers with them; the first frame tells */
static esp_err_t open_decoder(audio_track_t *track, const uint8_t *data, uint32_t len)
{
    register_decoders();
    esp_aac_dec_cfg_t aac = {
        .sample_rate = (int32_t)track->info.sample_rate,
        .channel = track->info.channel,
        .bits_per_sample = 16,
        .no_adts_header = !(len >= 2 && data[0] == 0xFF && (data[1] & 0xF6) == 0xF0),
        .aac_plus_enable = true,
    };
    esp_audio_dec_cfg_t config = {
        .type = track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC ? ESP_AUDIO_TYPE_AAC : ESP_AUDIO_TYPE_MP3,
        .cfg = track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC ? &aac : NULL,
        .cfg_sz = track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC ? sizeof(aac) : 0,
    };
    if (esp_audio_dec_open(&config, &track->decoder) != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Open %s decoder failed", track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC ? "AAC" : "MP3");
        track->decoder = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "%s audio, %lu Hz, %u ch%s", track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC ? "AAC" : "MP3",
             (unsigned long)track->info.sample_rate, track->info.channel,
             (track->info.format == ESP_EXTRACTOR_AUDIO_FORMAT_AAC && aac.no_adts_header) ? ", raw frames" : "");
    return ESP_OK;
}

static bool grow_pcm(audio_track_t *track, uint32_t needed)
{
    if (needed <= track->pcm_size) {
        return true;
    }
    uint32_t size = track->pcm_size ? track->pcm_size : PCM_BUFFER_START;
    while (size < needed) {
        size *= 2;
    }
    uint8_t *pcm = heap_caps_realloc(track->pcm, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) {
        return false;
    }
    track->pcm = pcm;
    track->pcm_size = size;
    return true;
}

esp_err_t audio_track_decode(audio_track_t *track, uint8_t *data, uint32_t len, int16_t **pcm,
                             uint32_t *pcm_bytes)
{
    *pcm = NULL;
    *pcm_bytes = 0;
    if (track->passthrough) {
        *pcm = (int16_t *)data;
        *pcm_bytes = len & ~3u;
        return ESP_OK;
    }
    if (!track->decoder && open_decoder(track, data, len) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!grow_pcm(track, PCM_BUFFER_START)) {
        return ESP_ERR_NO_MEM;
    }
    esp_audio_dec_in_raw_t raw = {
        .buffer = data,
        .len = len,
    };
    uint32_t total = 0;
    while (raw.len > 0) {
        esp_audio_dec_out_frame_t out = {
            .buffer = track->pcm + total,
            .len = track->pcm_size - total,
        };
        const esp_audio_err_t err = esp_audio_dec_process(track->decoder, &raw, &out);
        if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            if (!grow_pcm(track, total + out.needed_size)) {
                return ESP_ERR_NO_MEM;
            }
            continue;
        }
        if (err != ESP_AUDIO_ERR_OK) {
            break;  // A damaged frame is dropped; the next one decodes on its own
        }
        total += out.decoded_size;
        if (raw.consumed == 0) {
            break;
        }
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
    }
    if (total > 0) {
        esp_audio_dec_info_t info = {0};
        if (esp_audio_dec_get_info(track->decoder, &info) == ESP_AUDIO_ERR_OK && info.sample_rate) {
            // HE-AAC decodes at twice the container's rate
            track->sample_rate = info.sample_rate;
            track->channels = info.channel ? info.channel : track->channels;
        }
    }
    *pcm = (int16_t *)track->pcm;
    *pcm_bytes = total;
    return ESP_OK;
}

void audio_track_format(const audio_track_t *track, uint32_t *sample_rate, uint8_t *channels)
{
    *sample_rate = track->sample_rate;
    *channels = track->channels;
}

void audio_track_close(audio_track_t *track)
{
    if (!track) {
        return;
    }
    if (track->decoder) {
        esp_audio_dec_close(track->decoder);
    }
    heap_caps_free(track->pcm);
    free(track);
}
