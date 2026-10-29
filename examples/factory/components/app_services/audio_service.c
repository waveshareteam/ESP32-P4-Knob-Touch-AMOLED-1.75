/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "board_app.h"
#include "audio_service.h"

#define NVS_NAMESPACE   "audio"
#define NVS_KEY_VOLUME  "volume"
#define SAVE_DELAY_US   (1000 * 1000)

struct audio_client {
    audio_client_config_t config;
};

static const char *TAG = "audio_service";

/* s_io_lock guards playback and s_in_lock recording, so one owner can play and record at once.
 * The owner and the codec format only change with both held (s_io_lock first), so either lock
 * alone keeps them steady. s_switch_lock serializes ownership changes so a preempt callback
 * never races another acquire. */
static SemaphoreHandle_t s_io_lock;
static SemaphoreHandle_t s_in_lock;
static SemaphoreHandle_t s_switch_lock;
static audio_client_handle_t s_owner;
static int s_volume = BOARD_AUDIO_DEFAULT_VOLUME;

#define MAX_VOLUME_LISTENERS 4
static struct {
    audio_volume_cb_t cb;
    void *user_ctx;
} s_volume_listeners[MAX_VOLUME_LISTENERS];
static bool s_mute;
static bool s_owner_mute;
static esp_timer_handle_t s_save_timer;

static void load_volume(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    int32_t volume = 0;
    if (nvs_get_i32(nvs, NVS_KEY_VOLUME, &volume) == ESP_OK && volume >= 0 && volume <= 100) {
        s_volume = volume;
    }
    nvs_close(nvs);
}

/* Runs one second after the last volume change so a dragged slider writes NVS once. */
static void save_volume(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Volume %d saved", s_volume);
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_set_i32(nvs, NVS_KEY_VOLUME, s_volume) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

static void lock_all(void)
{
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    xSemaphoreTake(s_in_lock, portMAX_DELAY);
}

static void unlock_all(void)
{
    xSemaphoreGive(s_in_lock);
    xSemaphoreGive(s_io_lock);
}

/* Reapplies output settings after the codec has been reopened. Caller holds s_io_lock. */
static void apply_output_settings_locked(void)
{
    board_audio_volume_set(s_volume, NULL);
    board_audio_mute_set(s_mute || s_owner_mute);
}

esp_err_t audio_service_init(void)
{
    if (s_io_lock) {
        return ESP_OK;
    }
    s_io_lock = xSemaphoreCreateMutex();
    s_in_lock = xSemaphoreCreateMutex();
    s_switch_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_io_lock && s_in_lock && s_switch_lock, ESP_ERR_NO_MEM, TAG, "Create locks failed");

    load_volume();
    const esp_timer_create_args_t timer_args = {
        .callback = save_volume,
        .name = "audio_vol_save",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_save_timer), TAG, "Create save timer failed");
    // Nobody owns audio yet; keep the codec closed so the amplifier stays quiet.
    ESP_RETURN_ON_ERROR(board_audio_stop(), TAG, "Close codec failed");
    ESP_LOGI(TAG, "Initialized, volume %d", s_volume);
    return ESP_OK;
}

esp_err_t audio_client_register(const audio_client_config_t *config, audio_client_handle_t *ret_client)
{
    ESP_RETURN_ON_FALSE(config && config->name && ret_client, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    audio_client_handle_t client = calloc(1, sizeof(*client));
    ESP_RETURN_ON_FALSE(client, ESP_ERR_NO_MEM, TAG, "No memory for client");
    client->config = *config;
    *ret_client = client;
    return ESP_OK;
}

esp_err_t audio_client_acquire_if(audio_client_handle_t client, audio_acquire_check_t check, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_switch_lock, portMAX_DELAY);
    if (check && !check(user_ctx)) {
        xSemaphoreGive(s_switch_lock);
        return ESP_ERR_INVALID_STATE;
    }

    lock_all();
    audio_client_handle_t previous = s_owner;
    if (previous == client) {
        unlock_all();
        xSemaphoreGive(s_switch_lock);
        return ESP_OK;
    }
    // From here on the previous owner's in-flight calls fail fast.
    s_owner = NULL;
    unlock_all();

    if (previous) {
        ESP_LOGI(TAG, "%s takes audio from %s", client->config.name, previous->config.name);
        if (previous->config.on_preempt) {
            previous->config.on_preempt(previous, previous->config.user_ctx);
        }
    } else {
        ESP_LOGI(TAG, "%s takes audio", client->config.name);
    }

    lock_all();
    esp_err_t ret = board_audio_resume();
    if (ret == ESP_OK) {
        s_owner_mute = false;
        apply_output_settings_locked();
        s_owner = client;
    } else {
        ESP_LOGE(TAG, "Open codec for %s failed", client->config.name);
    }
    unlock_all();

    xSemaphoreGive(s_switch_lock);
    return ret;
}

esp_err_t audio_client_acquire(audio_client_handle_t client)
{
    return audio_client_acquire_if(client, NULL, NULL);
}

esp_err_t audio_client_release(audio_client_handle_t client)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_switch_lock, portMAX_DELAY);
    lock_all();
    if (s_owner == client) {
        s_owner = NULL;
        board_audio_stop();
        ESP_LOGI(TAG, "%s released audio", client->config.name);
    }
    unlock_all();
    xSemaphoreGive(s_switch_lock);
    return ESP_OK;
}

bool audio_client_is_owner(audio_client_handle_t client)
{
    return client && s_owner == client;
}

esp_err_t audio_client_set_format(audio_client_handle_t client, uint32_t sample_rate, uint32_t bits_per_sample,
                                  uint32_t channels)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    lock_all();
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (s_owner == client) {
        ret = board_audio_set_fs(sample_rate, bits_per_sample, (i2s_slot_mode_t)channels);
        if (ret == ESP_OK) {
            apply_output_settings_locked();
        }
    }
    unlock_all();
    return ret;
}

esp_err_t audio_client_write(audio_client_handle_t client, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (s_owner == client) {
        size_t written = 0;
        ret = board_audio_write((void *)data, len, &written, portMAX_DELAY);
    }
    xSemaphoreGive(s_io_lock);
    return ret;
}

esp_err_t audio_client_read(audio_client_handle_t client, void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_in_lock, portMAX_DELAY);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (s_owner == client) {
        size_t read = 0;
        ret = board_audio_read(data, len, &read, portMAX_DELAY);
    }
    xSemaphoreGive(s_in_lock);
    return ret;
}

esp_err_t audio_client_set_mute(audio_client_handle_t client, bool mute)
{
    ESP_RETURN_ON_FALSE(client && s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (s_owner == client) {
        s_owner_mute = mute;
        apply_output_settings_locked();
        ret = ESP_OK;
    }
    xSemaphoreGive(s_io_lock);
    return ret;
}

const char *audio_service_owner_name(void)
{
    audio_client_handle_t owner = s_owner;
    return owner ? owner->config.name : NULL;
}

esp_err_t audio_service_set_volume(int volume)
{
    ESP_RETURN_ON_FALSE(s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    const bool changed = volume != s_volume;
    s_volume = volume;
    if (s_owner) {
        board_audio_volume_set(s_volume, NULL);
    }
    xSemaphoreGive(s_io_lock);
    if (!changed) {
        return ESP_OK;
    }

    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
    for (int i = 0; i < MAX_VOLUME_LISTENERS; i++) {
        if (s_volume_listeners[i].cb) {
            s_volume_listeners[i].cb(volume, s_volume_listeners[i].user_ctx);
        }
    }
    return ESP_OK;
}

esp_err_t audio_service_add_volume_listener(audio_volume_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "Invalid callback");
    for (int i = 0; i < MAX_VOLUME_LISTENERS; i++) {
        if (!s_volume_listeners[i].cb) {
            s_volume_listeners[i].user_ctx = user_ctx;
            s_volume_listeners[i].cb = cb;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

int audio_service_get_volume(void)
{
    return s_volume;
}

esp_err_t audio_service_set_mute(bool mute)
{
    ESP_RETURN_ON_FALSE(s_io_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    s_mute = mute;
    if (s_owner) {
        board_audio_mute_set(s_mute || s_owner_mute);
    }
    xSemaphoreGive(s_io_lock);
    return ESP_OK;
}

bool audio_service_get_mute(void)
{
    return s_mute;
}
