/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "audio_service.h"
#include "bt_service.h"

#define BT_DEVICE_NAME          "ESP32-P4-Knob"
#define NVS_NAMESPACE           "bt"
#define NVS_KEY_ENABLED         "enabled"

#define RINGBUF_SIZE            (64 * 1024)
#define RINGBUF_PREFETCH        (16 * 1024)     // About 90 ms of 44.1 kHz stereo before playback starts
#define WRITE_CHUNK             (4 * 1024)
#define PLAY_TASK_STACK         4096
#define PLAY_TASK_PRIORITY      (configMAX_PRIORITIES - 3)
#define POSITION_INTERVAL_S     1
#define COVER_MAX_SIZE          (512 * 1024)    // Linked thumbnails are 200x200 JPEGs, far below this

/* AVRCP transaction labels, one per kind of request */
enum {
    TL_GET_CAPS = 0,
    TL_GET_METADATA,
    TL_RN_TRACK_CHANGE,
    TL_RN_PLAY_STATUS,
    TL_RN_PLAY_POS,
    TL_PASSTHROUGH,
};

static const char *TAG = "bt_service";

static SemaphoreHandle_t s_lock;
static bt_info_t s_info;
static bool s_enabled = true;
static bool s_a2d_connected;
static esp_bd_addr_t s_peer_bda;
static esp_avrc_rn_evt_cap_mask_t s_peer_rn_cap;

static RingbufHandle_t s_ringbuf;
static TaskHandle_t s_play_task;
static audio_client_handle_t s_client;
static volatile bool s_streaming;       // The phone reported that audio started
static volatile bool s_preempted;       // Another app took audio during this stream
static volatile bool s_format_dirty;
static volatile uint32_t s_sample_rate = 44100;
/* The phone's volume slider mirrors the device volume (audio service) in AVRCP units */
static volatile uint8_t s_abs_volume = 0x7F;
static volatile bool s_volume_from_phone;       // The change being applied came from the phone
static bool s_volume_ntf_registered;            // The phone asked to hear about volume changes
static uint8_t s_work_buf[WRITE_CHUNK];

/* Cover art arrives in pieces over OBEX; a finished image replaces the shown one */
static bool s_cover_connected;
static bool s_cover_getting;
static uint8_t s_cover_handle[ESP_AVRC_CA_IMAGE_HANDLE_LEN];
static uint8_t *s_cover_rx;
static size_t s_cover_rx_len;
static bool s_cover_rx_overflow;
static uint8_t *s_cover;
static size_t s_cover_len;
static uint32_t s_cover_duration_ms;            // Track length when the cover arrived; a change means a new song

/* The phone reports the position now and then; it is advanced locally while playing */
static uint32_t s_pos_ms;
static int64_t s_pos_stamp_us;

static bool peer_supports(uint8_t event_id)
{
    return esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_peer_rn_cap, event_id);
}

static uint8_t volume_percent(uint8_t abs_volume)
{
    return (uint8_t)((abs_volume * 100 + 63) / 0x7F);
}

static uint8_t volume_abs(int percent)
{
    return (uint8_t)((percent * 0x7F + 50) / 100);
}

static void update_state_locked(void)
{
    if (!s_enabled) {
        s_info.state = BT_STATE_OFF;
    } else if (s_a2d_connected && s_streaming) {
        s_info.state = BT_STATE_STREAMING;
    } else if (s_a2d_connected) {
        s_info.state = BT_STATE_CONNECTED;
    } else {
        s_info.state = BT_STATE_WAITING;
    }
}

static void update_state(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    update_state_locked();
    xSemaphoreGive(s_lock);
}

/* Discoverable only while switched on and free; a connected phone keeps the link. */
static void apply_scan_mode(void)
{
    if (!s_enabled) {
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    } else if (s_a2d_connected) {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    } else {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    }
}

static void clear_cover_locked(void)
{
    free(s_cover);
    s_cover = NULL;
    s_cover_len = 0;
    memset(s_cover_handle, 0, sizeof(s_cover_handle));
    s_info.cover_seq++;
}

static uint32_t position_locked(int64_t now_us)
{
    uint32_t pos = s_pos_ms;
    if (s_info.playing) {
        pos += (uint32_t)((now_us - s_pos_stamp_us) / 1000);
        if (s_info.duration_ms > 0 && pos > s_info.duration_ms) {
            pos = s_info.duration_ms;
        }
    }
    return pos;
}

/* Changes the play state without making the shown position jump */
static void set_playing_locked(bool playing)
{
    const int64_t now = esp_timer_get_time();
    s_pos_ms = position_locked(now);
    s_pos_stamp_us = now;
    s_info.playing = playing;
}

static void clear_track_locked(void)
{
    s_info.title[0] = '\0';
    s_info.artist[0] = '\0';
    s_info.album[0] = '\0';
    s_info.playing = false;
    s_info.play_status_known = false;
    s_info.position_ms = 0;
    s_info.duration_ms = 0;
    s_pos_ms = 0;
    s_info.track_seq++;
}

/* Copies AVRCP text (not NUL-terminated) without cutting a UTF-8 sequence in half. */
static void copy_utf8(char *dst, size_t dst_size, const uint8_t *src, int len)
{
    size_t n = (len > 0) ? (size_t)len : 0;
    if (n >= dst_size) {
        n = dst_size - 1;
        while (n > 0 && (src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void send_passthrough(uint8_t key)
{
    esp_avrc_ct_send_passthrough_cmd(TL_PASSTHROUGH, key, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_avrc_ct_send_passthrough_cmd(TL_PASSTHROUGH, key, ESP_AVRC_PT_CMD_STATE_RELEASED);
}

static void request_metadata(void)
{
    uint8_t mask = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM |
                   ESP_AVRC_MD_ATTR_PLAYING_TIME;
    if (s_cover_connected) {
        mask |= ESP_AVRC_MD_ATTR_COVER_ART;
    }
    esp_avrc_ct_send_metadata_cmd(TL_GET_METADATA, mask);
}

static void register_notification(uint8_t event_id)
{
    if (!peer_supports(event_id)) {
        return;
    }
    switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
        esp_avrc_ct_send_register_notification_cmd(TL_RN_TRACK_CHANGE, event_id, 0);
        break;
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_STATUS, event_id, 0);
        break;
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_POS, event_id, POSITION_INTERVAL_S);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------------- */
/* Audio: the A2DP task fills a ring buffer and the play task drains it to the codec  */

static void on_preempt(audio_client_handle_t client, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    // Another app needs the speaker: stop the phone instead of dropping its audio silently
    s_preempted = true;
    send_passthrough(ESP_AVRC_PT_CMD_PAUSE);
    ESP_LOGI(TAG, "Audio taken by another app, pausing the phone");
}

/* Called with the audio ownership switch serialized. A queued Bluetooth
 * acquisition is no longer useful once another application preempted it. */
static bool audio_still_streaming(void *user_ctx)
{
    (void)user_ctx;
    return s_streaming && !s_preempted;
}

static void a2d_data_cb(const uint8_t *data, uint32_t len)
{
    if (!s_streaming || s_preempted || !s_ringbuf) {
        return;
    }
    // A full buffer means the codec fell behind; dropping keeps latency bounded
    xRingbufferSend(s_ringbuf, data, len, 0);
}

/* The board has one speaker, so both channels carry (L + R) / 2. Loudness is the device
 * volume, which the phone's volume slider controls too. */
static void mix_to_mono(int16_t *samples, size_t count)
{
    for (size_t i = 0; i + 1 < count; i += 2) {
        const int16_t mixed = (int16_t)(((int32_t)samples[i] + samples[i + 1]) / 2);
        samples[i] = mixed;
        samples[i + 1] = mixed;
    }
}

/* Any change of the device volume moves the phone's slider; the phone's own changes are not echoed */
static void on_volume_changed(int volume, void *user_ctx)
{
    (void)user_ctx;
    s_abs_volume = volume_abs(volume);
    if (s_volume_from_phone || !s_volume_ntf_registered || !s_a2d_connected) {
        return;
    }
    // AVRCP notifications are one-shot: answer once, the phone registers again
    s_volume_ntf_registered = false;
    esp_avrc_rn_param_t rn = {.volume = s_abs_volume};
    esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn);
}

static void drain_ringbuf(void)
{
    size_t size = 0;
    void *item;
    while ((item = xRingbufferReceiveUpTo(s_ringbuf, &size, 0, RINGBUF_SIZE)) != NULL) {
        vRingbufferReturnItem(s_ringbuf, item);
    }
}

static void play_task(void *arg)
{
    (void)arg;
    bool prefetching = true;

    while (true) {
        const bool own = audio_client_is_owner(s_client);
        if (s_streaming && !s_preempted && !own) {
            if (audio_client_acquire_if(s_client, audio_still_streaming, NULL) == ESP_OK) {
                s_format_dirty = true;
                prefetching = true;
                ESP_LOGI(TAG, "Playing phone audio");
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        if ((!s_streaming || s_preempted) && own) {
            audio_client_release(s_client);
            drain_ringbuf();
            continue;
        }
        if (!own) {
            drain_ringbuf();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        if (s_format_dirty) {
            s_format_dirty = false;
            audio_client_set_format(s_client, s_sample_rate, 16, 2);
        }
        if (prefetching) {
            UBaseType_t waiting = 0;
            vRingbufferGetInfo(s_ringbuf, NULL, NULL, NULL, NULL, &waiting);
            if (waiting < RINGBUF_PREFETCH) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            prefetching = false;
        }

        size_t size = 0;
        void *item = xRingbufferReceiveUpTo(s_ringbuf, &size, pdMS_TO_TICKS(20), sizeof(s_work_buf));
        if (!item) {
            // Underflow: wait for a little audio again so playback does not stutter
            prefetching = true;
            continue;
        }
        memcpy(s_work_buf, item, size);
        vRingbufferReturnItem(s_ringbuf, item);
        mix_to_mono((int16_t *)s_work_buf, size / sizeof(int16_t));
        audio_client_write(s_client, s_work_buf, size & ~3u);
    }
}

/* ---------------------------------------------------------------------------------- */
/* Bluedroid callbacks (BTC task context)                                              */

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Paired with %s", param->auth_cmpl.device_name);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_info.peer_name, (const char *)param->auth_cmpl.device_name, sizeof(s_info.peer_name));
            xSemaphoreGive(s_lock);
        } else {
            ESP_LOGW(TAG, "Pairing failed: %d", param->auth_cmpl.stat);
        }
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        // No display or keyboard to compare a passkey: accept like a headset does
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_info.peer_name, (const char *)param->read_rmt_name.rmt_name, sizeof(s_info.peer_name));
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "Connected phone: %s", s_info.peer_name);
        }
        break;
    default:
        break;
    }
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            memcpy(s_peer_bda, param->conn_stat.remote_bda, sizeof(esp_bd_addr_t));
            s_a2d_connected = true;
            esp_bt_gap_read_remote_name(s_peer_bda);
            ESP_LOGI(TAG, "Phone connected");
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_a2d_connected = false;
            s_streaming = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_info.peer_name[0] = '\0';
            clear_track_locked();
            clear_cover_locked();
            xSemaphoreGive(s_lock);
            s_cover_connected = false;
            s_cover_getting = false;
            ESP_LOGI(TAG, "Phone disconnected");
        } else {
            break;
        }
        apply_scan_mode();
        update_state();
        xTaskNotifyGive(s_play_task);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        s_streaming = (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        if (s_streaming) {
            // A new stream started by the user takes the speaker back
            s_preempted = false;
        }
        ESP_LOGI(TAG, "Phone audio %s", s_streaming ? "started" : "stopped");
        update_state();
        xTaskNotifyGive(s_play_task);
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        if (param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            const uint8_t freq = param->audio_cfg.mcc.cie.sbc_info.samp_freq;
            uint32_t rate = 16000;
            if (freq & ESP_A2D_SBC_CIE_SF_32K) {
                rate = 32000;
            } else if (freq & ESP_A2D_SBC_CIE_SF_44K) {
                rate = 44100;
            } else if (freq & ESP_A2D_SBC_CIE_SF_48K) {
                rate = 48000;
            }
            s_sample_rate = rate;
            s_format_dirty = true;
            ESP_LOGI(TAG, "Stream format: SBC %" PRIu32 " Hz", rate);
        }
        break;
    default:
        break;
    }
}

static void avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        if (param->conn_stat.connected) {
            esp_avrc_ct_send_get_rn_capabilities_cmd(TL_GET_CAPS);
        } else {
            s_peer_rn_cap.bits = 0;
        }
        break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        // Phones with AVRCP 1.6 cover art serve a 200x200 thumbnail per track
        if ((param->rmt_feats.tg_feat_flag & ESP_AVRC_FEAT_FLAG_TG_COVER_ART) && !s_cover_connected) {
            esp_avrc_ct_cover_art_connect(ESP_AVRC_CA_MTU_MAX);
        }
        break;
    case ESP_AVRC_CT_COVER_ART_STATE_EVT:
        s_cover_connected = (param->cover_art_state.state == ESP_AVRC_COVER_ART_CONNECTED);
        s_cover_getting = false;
        ESP_LOGI(TAG, "Cover art %s", s_cover_connected ? "available" : "unavailable");
        if (s_cover_connected) {
            request_metadata();
        }
        break;
    case ESP_AVRC_CT_COVER_ART_DATA_EVT:
        if (param->cover_art_data.status == ESP_BT_STATUS_SUCCESS && !s_cover_rx_overflow) {
            const size_t len = param->cover_art_data.data_len;
            if (!s_cover_rx) {
                s_cover_rx = heap_caps_malloc(COVER_MAX_SIZE, MALLOC_CAP_SPIRAM);
            }
            if (s_cover_rx && s_cover_rx_len + len <= COVER_MAX_SIZE) {
                memcpy(s_cover_rx + s_cover_rx_len, param->cover_art_data.p_data, len);
                s_cover_rx_len += len;
            } else {
                s_cover_rx_overflow = true;
            }
        }
        if (param->cover_art_data.final) {
            const bool ok = param->cover_art_data.status == ESP_BT_STATUS_SUCCESS && !s_cover_rx_overflow &&
                            s_cover_rx_len > 0;
            // Phones sometimes hand out a new handle for the same picture
            const bool same = ok && s_cover && s_cover_len == s_cover_rx_len &&
                              memcmp(s_cover, s_cover_rx, s_cover_rx_len) == 0;
            uint8_t *image = (ok && !same) ? heap_caps_malloc(s_cover_rx_len, MALLOC_CAP_SPIRAM) : NULL;
            if (same) {
                ESP_LOGD(TAG, "Cover art unchanged");
            } else if (image) {
                memcpy(image, s_cover_rx, s_cover_rx_len);
                xSemaphoreTake(s_lock, portMAX_DELAY);
                free(s_cover);
                s_cover = image;
                s_cover_len = s_cover_rx_len;
                s_cover_duration_ms = s_info.duration_ms;
                s_info.cover_seq++;
                xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "Cover art received (%u bytes)", (unsigned)s_cover_rx_len);
            } else {
                ESP_LOGW(TAG, "Cover art download failed");
            }
            s_cover_rx_len = 0;
            s_cover_rx_overflow = false;
            s_cover_getting = false;
        }
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        s_peer_rn_cap.bits = param->get_rn_caps_rsp.evt_set.bits;
        request_metadata();
        register_notification(ESP_AVRC_RN_TRACK_CHANGE);
        register_notification(ESP_AVRC_RN_PLAY_STATUS_CHANGE);
        register_notification(ESP_AVRC_RN_PLAY_POS_CHANGED);
        break;
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        const uint8_t *text = param->meta_rsp.attr_text;
        const int len = param->meta_rsp.attr_length;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        switch (param->meta_rsp.attr_id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            copy_utf8(s_info.title, sizeof(s_info.title), text, len);
            ESP_LOGI(TAG, "Track: %s", s_info.title);
            break;
        case ESP_AVRC_MD_ATTR_ARTIST:
            copy_utf8(s_info.artist, sizeof(s_info.artist), text, len);
            break;
        case ESP_AVRC_MD_ATTR_ALBUM:
            copy_utf8(s_info.album, sizeof(s_info.album), text, len);
            break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME: {
            char ms[16];
            copy_utf8(ms, sizeof(ms), text, len);
            s_info.duration_ms = (uint32_t)strtoul(ms, NULL, 10);
            break;
        }
        case ESP_AVRC_MD_ATTR_COVER_ART:
            if (len == ESP_AVRC_CA_IMAGE_HANDLE_LEN) {
                // Lyrics apps resend metadata per line; only a new handle means a new image
                if (memcmp(s_cover_handle, text, len) != 0 && !s_cover_getting) {
                    memcpy(s_cover_handle, text, len);
                    s_cover_getting = esp_avrc_ct_cover_art_get_linked_thumbnail((uint8_t *)text) == ESP_OK;
                }
            } else if (s_cover_len > 0 && s_info.duration_ms != s_cover_duration_ms) {
                // No cover for a different song; lyric updates of the same song keep theirs
                clear_cover_locked();
            }
            break;
        default:
            break;
        }
        s_info.track_seq++;
        xSemaphoreGive(s_lock);
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        switch (param->change_ntf.event_id) {
        case ESP_AVRC_RN_TRACK_CHANGE:
            // Keep the old text and position until the phone sends new ones: lyrics apps
            // report a track change for every line
            request_metadata();
            break;
        case ESP_AVRC_RN_PLAY_STATUS_CHANGE: {
            const uint8_t status = param->change_ntf.event_parameter.playback;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            set_playing_locked(status == ESP_AVRC_PLAYBACK_PLAYING);
            s_info.play_status_known = true;
            xSemaphoreGive(s_lock);
            break;
        }
        case ESP_AVRC_RN_PLAY_POS_CHANGED:
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_pos_ms = param->change_ntf.event_parameter.play_pos;
            s_pos_stamp_us = esp_timer_get_time();
            xSemaphoreGive(s_lock);
            break;
        default:
            break;
        }
        // Notifications are one-shot in AVRCP: register again for the next change
        register_notification(param->change_ntf.event_id);
        break;
    default:
        break;
    }
}

static void avrc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        // The phone's slider sets the device volume, the same one Settings shows
        const uint8_t percent = volume_percent(param->set_abs_vol.volume & 0x7F);
        s_volume_from_phone = true;
        audio_service_set_volume(percent);
        s_volume_from_phone = false;
        ESP_LOGI(TAG, "Phone volume %d%%", percent);
        break;
    }
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (param->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_volume_ntf_registered = true;
            s_abs_volume = volume_abs(audio_service_get_volume());
            esp_avrc_rn_param_t rn = {.volume = s_abs_volume};
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn);
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------------- */

static void save_enabled(bool enabled)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_ENABLED, enabled);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static bool load_enabled(void)
{
    uint8_t enabled = 1;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_KEY_ENABLED, &enabled);
        nvs_close(nvs);
    }
    return enabled != 0;
}

esp_err_t bt_service_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "Create lock failed");
    s_enabled = load_enabled();
    strlcpy(s_info.local_name, BT_DEVICE_NAME, sizeof(s_info.local_name));
    s_abs_volume = volume_abs(audio_service_get_volume());
    audio_service_add_volume_listener(on_volume_changed, NULL);

    s_ringbuf = xRingbufferCreateWithCaps(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_ringbuf, ESP_ERR_NO_MEM, TAG, "Create ring buffer failed");
    const audio_client_config_t client_cfg = {
        .name = "Bluetooth",
        .on_preempt = on_preempt,
    };
    ESP_RETURN_ON_ERROR(audio_client_register(&client_cfg, &s_client), TAG, "Register audio client failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(play_task, "bt_play", PLAY_TASK_STACK, NULL, PLAY_TASK_PRIORITY, &s_play_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Create play task failed");

    // The host runs here; the ESP32 co-processor provides the controller through esp-extconn
    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&cfg), TAG, "Bluedroid init failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "Bluedroid enable failed");

    esp_bt_gap_set_device_name(BT_DEVICE_NAME);
    esp_bt_gap_register_callback(gap_cb);

    esp_avrc_ct_register_callback(avrc_ct_cb);
    ESP_RETURN_ON_ERROR(esp_avrc_ct_init(), TAG, "AVRCP controller init failed");
    esp_avrc_tg_register_callback(avrc_tg_cb);
    ESP_RETURN_ON_ERROR(esp_avrc_tg_init(), TAG, "AVRCP target init failed");
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set);

    esp_a2d_register_callback(a2d_cb);
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), TAG, "A2DP sink init failed");
    esp_a2d_sink_register_data_callback(a2d_data_cb);

    // Just Works pairing: the board has nothing to show or type a passkey with
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin);

    apply_scan_mode();
    update_state();
    ESP_LOGI(TAG, "Bluetooth %s as \"%s\"", s_enabled ? "on" : "off", BT_DEVICE_NAME);
    return ESP_OK;
}

void bt_service_get_info(bt_info_t *info)
{
    if (!info) {
        return;
    }
    if (!s_lock) {
        memset(info, 0, sizeof(*info));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *info = s_info;
    info->position_ms = position_locked(esp_timer_get_time());
    xSemaphoreGive(s_lock);
}

esp_err_t bt_service_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    if (enabled == s_enabled) {
        return ESP_OK;
    }
    s_enabled = enabled;
    save_enabled(enabled);
    if (!enabled && s_a2d_connected) {
        esp_a2d_sink_disconnect(s_peer_bda);
    }
    apply_scan_mode();
    update_state();
    ESP_LOGI(TAG, "Bluetooth %s", enabled ? "on" : "off");
    return ESP_OK;
}

bool bt_service_is_enabled(void)
{
    return s_enabled;
}

static esp_err_t send_key(uint8_t key)
{
    ESP_RETURN_ON_FALSE(s_a2d_connected, ESP_ERR_INVALID_STATE, TAG, "No phone connected");
    send_passthrough(key);
    return ESP_OK;
}

/* Shows the requested state at once; the phone's play status notification confirms it */
static esp_err_t send_play_state(uint8_t key, bool playing)
{
    esp_err_t ret = send_key(key);
    if (ret == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_playing_locked(playing);
        xSemaphoreGive(s_lock);
    }
    return ret;
}

esp_err_t bt_service_play(void)
{
    // Pressing play here is a request to hear the phone again
    s_preempted = false;
    return send_play_state(ESP_AVRC_PT_CMD_PLAY, true);
}

esp_err_t bt_service_pause(void)
{
    return send_play_state(ESP_AVRC_PT_CMD_PAUSE, false);
}

esp_err_t bt_service_next(void)
{
    return send_key(ESP_AVRC_PT_CMD_FORWARD);
}

esp_err_t bt_service_prev(void)
{
    return send_key(ESP_AVRC_PT_CMD_BACKWARD);
}

esp_err_t bt_service_copy_cover(uint8_t **jpeg, size_t *len)
{
    ESP_RETURN_ON_FALSE(jpeg && len, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_cover && s_cover_len > 0) {
        *jpeg = heap_caps_malloc(s_cover_len, MALLOC_CAP_SPIRAM);
        if (*jpeg) {
            memcpy(*jpeg, s_cover, s_cover_len);
            *len = s_cover_len;
            ret = ESP_OK;
        } else {
            ret = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t bt_service_disconnect(void)
{
    ESP_RETURN_ON_FALSE(s_a2d_connected, ESP_ERR_INVALID_STATE, TAG, "No phone connected");
    return esp_a2d_sink_disconnect(s_peer_bda);
}

esp_err_t bt_service_forget_all(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    if (s_a2d_connected) {
        esp_a2d_sink_disconnect(s_peer_bda);
    }
    int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0) {
        return ESP_OK;
    }
    esp_bd_addr_t *list = calloc(count, sizeof(esp_bd_addr_t));
    ESP_RETURN_ON_FALSE(list, ESP_ERR_NO_MEM, TAG, "No memory");
    if (esp_bt_gap_get_bond_device_list(&count, list) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            esp_bt_gap_remove_bond_device(list[i]);
        }
    }
    free(list);
    ESP_LOGI(TAG, "Forgot %d paired device(s)", count);
    return ESP_OK;
}

const char *bt_service_state_name(bt_state_t state)
{
    switch (state) {
    case BT_STATE_OFF:
        return "Off";
    case BT_STATE_WAITING:
        return "Waiting for a phone";
    case BT_STATE_CONNECTED:
        return "Connected";
    case BT_STATE_STREAMING:
        return "Playing";
    default:
        return "Unknown";
    }
}
