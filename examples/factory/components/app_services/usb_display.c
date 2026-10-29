/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/jpeg_decode.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "audio_service.h"
#include "usb_descriptors.h"
#include "usb_service.h"
#include "usb_display.h"

/* Frames being received, waiting to be shown and being shown */
#define FRAME_COUNT             4
/* Without a frame for this long the PC has stopped drawing, or turned the screen off */
#define STREAM_TIMEOUT_US       (1000 * 1000)
/* The audio goes back to the apps once the PC has sent and taken nothing for this long */
#define AUDIO_IDLE_US           (1500 * 1000)
#define AUDIO_CHECK_PERIOD_US   (500 * 1000)
#define TOUCH_CONTACT_SIZE      8

/* The codec plays and records in one format, so both directions must agree */
_Static_assert(CONFIG_UAC_SPEAKER_CHANNEL_NUM == CONFIG_UAC_MIC_CHANNEL_NUM, "Speaker and mic channels must match");
_Static_assert(CONFIG_UAC_BIT_RESOLUTION == 16, "Audio is played as 16-bit samples");

/* Packets from the PC driver: a header, then the payload; a frame starts a new packet. The
 * header names the rectangle the payload draws, which may be just the part that changed. */

typedef struct __attribute__((packed)) {
    uint16_t crc16;
    uint8_t type;
    uint8_t cmd;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t frame_id: 10;
    uint32_t payload_total: 22;
} udisp_frame_header_t;

typedef struct {
    usb_display_frame_t frame;      // First, so a frame handed out leads back to its slot
    uint8_t *buf;
    size_t capacity;
    uint32_t total;                 // Payload size announced by the header
} frame_slot_t;

/* One finger in the HID touch report, matching s_desc_hid_report */
typedef struct __attribute__((packed)) {
    uint8_t tip;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} touch_finger_t;

typedef struct __attribute__((packed)) {
    touch_finger_t fingers[USB_DISPLAY_TOUCH_POINTS];
    uint8_t count;
} touch_report_t;

static const char *TAG = "usb_display";

static bool s_ready;
static volatile bool s_running;
static frame_slot_t s_slots[FRAME_COUNT];
static QueueHandle_t s_free;
static QueueHandle_t s_filled;
static volatile int64_t s_last_frame_us;

/* Receive state, owned by the USB task while running */
static SemaphoreHandle_t s_rx_lock;
static frame_slot_t *s_rx_slot;
static uint32_t s_rx_skip;          // Payload bytes of a dropped frame still to come

static usb_display_stats_t s_stats;

/* Fingers reported down last time */
static usb_display_touch_t s_touch_down[USB_DISPLAY_TOUCH_POINTS];
static size_t s_touch_down_count;

static audio_client_handle_t s_audio;
static SemaphoreHandle_t s_audio_lock;
static esp_timer_handle_t s_audio_timer;
static volatile int64_t s_audio_used_us;
static volatile bool s_audio_preempted;
static volatile int32_t s_pc_gain_q15 = 32768;
static volatile bool s_pc_mute;

/* ---------------------------------------------------------------------------
 * Frames
 * ------------------------------------------------------------------------- */

static void give_back(frame_slot_t *slot)
{
    if (slot) {
        xQueueSend(s_free, &slot, 0);
    }
}

/* Counts and refuses a frame this screen cannot show */
static bool header_valid(const udisp_frame_header_t *header)
{
    const uint32_t pixels = (uint32_t)header->width * header->height;
    uint32_t raw_len = 0;
    switch (header->type) {
    case USB_DISPLAY_FORMAT_RGB565:
        raw_len = pixels * 2;
        break;
    case USB_DISPLAY_FORMAT_RGB888:
        raw_len = pixels * 3;
        break;
    case USB_DISPLAY_FORMAT_JPEG:
        break;
    default:
        s_stats.bad_format++;
        return false;
    }
    if (pixels == 0 || header->x >= USB_DISPLAY_WIDTH || header->y >= USB_DISPLAY_HEIGHT) {
        s_stats.bad_rect++;
        return false;
    }
    if (header->payload_total == 0 || header->payload_total > s_slots[0].capacity ||
            (raw_len && header->payload_total != raw_len)) {
        s_stats.bad_len++;
        return false;
    }
    return true;
}

#if CFG_TUD_API_V0_19_COMPAT
void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint16_t bufsize)
#else
void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize)
#endif
{
    (void)idx;
    if (!s_running || !buffer || bufsize == 0) {
        return;
    }
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    s_stats.packets++;
    if (!s_rx_slot && !s_rx_skip) {
        // A packet that starts no frame is the padding after the last one
        const udisp_frame_header_t *header = (const udisp_frame_header_t *)buffer;
        if (bufsize < sizeof(*header)) {
            xSemaphoreGive(s_rx_lock);
            return;
        }
        s_stats.last_type = header->type;
        s_stats.last_x = header->x;
        s_stats.last_y = header->y;
        s_stats.last_width = header->width;
        s_stats.last_height = header->height;
        s_stats.last_len = header->payload_total;
        if (!header_valid(header)) {
            xSemaphoreGive(s_rx_lock);
            return;
        }
        frame_slot_t *slot = NULL;
        if (xQueueReceive(s_free, &slot, 0) == pdTRUE) {
            slot->frame.format = (usb_display_format_t)header->type;
            slot->frame.x = header->x;
            slot->frame.y = header->y;
            slot->frame.width = header->width;
            slot->frame.height = header->height;
            slot->frame.len = 0;
            slot->total = header->payload_total;
            s_rx_slot = slot;
        } else {
            // Every buffer is waiting to be shown: drop this frame
            s_rx_skip = header->payload_total;
            s_stats.busy_drops++;
        }
        buffer += sizeof(*header);
        bufsize -= sizeof(*header);
    }
    if (s_rx_skip) {
        s_rx_skip -= bufsize < s_rx_skip ? bufsize : s_rx_skip;
    } else if (s_rx_slot) {
        frame_slot_t *slot = s_rx_slot;
        const uint32_t left = slot->total - slot->frame.len;
        const uint32_t n = bufsize < left ? bufsize : left;
        memcpy(slot->buf + slot->frame.len, buffer, n);
        slot->frame.len += n;
        if (slot->frame.len >= slot->total) {
            s_rx_slot = NULL;
            s_stats.frames++;
            s_last_frame_us = esp_timer_get_time();
            if (xQueueSend(s_filled, &slot, 0) != pdTRUE) {
                give_back(slot);
            }
        }
    }
    xSemaphoreGive(s_rx_lock);
}

usb_display_frame_t *usb_display_get_frame(uint32_t timeout_ms)
{
    if (!s_ready) {
        return NULL;
    }
    frame_slot_t *slot = NULL;
    if (xQueueReceive(s_filled, &slot, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }
    // Behind the PC: skip ahead to a newer frame of the whole panel, which covers what came before.
    // Updates of a part must all be shown, in order.
    frame_slot_t *newer = NULL;
    while (xQueuePeek(s_filled, &newer, 0) == pdTRUE && newer->frame.x == 0 && newer->frame.y == 0 &&
            newer->frame.width >= USB_DISPLAY_WIDTH && newer->frame.height >= USB_DISPLAY_HEIGHT) {
        xQueueReceive(s_filled, &newer, 0);
        give_back(slot);
        slot = newer;
    }
    return &slot->frame;
}

void usb_display_return_frame(usb_display_frame_t *frame)
{
    give_back((frame_slot_t *)frame);
}

/* ---------------------------------------------------------------------------
 * Touch
 * ------------------------------------------------------------------------- */

static void put_finger(touch_finger_t *finger, const usb_display_touch_t *point, bool down)
{
    finger->tip = down ? 1 : 0;
    finger->id = point->id;
    finger->x = point->x < USB_DISPLAY_WIDTH ? point->x : USB_DISPLAY_WIDTH - 1;
    finger->y = point->y < USB_DISPLAY_HEIGHT ? point->y : USB_DISPLAY_HEIGHT - 1;
    finger->width = TOUCH_CONTACT_SIZE;
    finger->height = TOUCH_CONTACT_SIZE;
}

esp_err_t usb_display_report_touch(const usb_display_touch_t *points, size_t count)
{
    ESP_RETURN_ON_FALSE(points || count == 0, ESP_ERR_INVALID_ARG, TAG, "Invalid points");
    if (!s_running || !tud_mounted()) {
        s_touch_down_count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    if (count > USB_DISPLAY_TOUCH_POINTS) {
        count = USB_DISPLAY_TOUCH_POINTS;
    }
    touch_report_t report = {0};
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        put_finger(&report.fingers[n++], &points[i], true);
    }
    // Windows needs each lifted finger once more with the tip switch off
    for (size_t i = 0; i < s_touch_down_count && n < USB_DISPLAY_TOUCH_POINTS; i++) {
        bool still_down = false;
        for (size_t j = 0; j < count && !still_down; j++) {
            still_down = points[j].id == s_touch_down[i].id;
        }
        if (!still_down) {
            put_finger(&report.fingers[n++], &s_touch_down[i], false);
        }
    }
    if (n == 0) {
        return ESP_OK;
    }
    report.count = n;
    if (!tud_hid_ready() || !tud_hid_report(REPORT_ID_TOUCH, &report, sizeof(report))) {
        return ESP_ERR_NOT_FINISHED;
    }
    memcpy(s_touch_down, points, count * sizeof(points[0]));
    s_touch_down_count = count;
    return ESP_OK;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_type;
    if (report_id == REPORT_ID_MAX_COUNT && reqlen >= 1) {
        buffer[0] = USB_DISPLAY_TOUCH_POINTS;
        return 1;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/* ---------------------------------------------------------------------------
 * Sound
 * ------------------------------------------------------------------------- */

static void on_audio_preempt(audio_client_handle_t client, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    // Leave it to the app that took it until the PC stops and starts playing again
    s_audio_preempted = true;
}

/* Called by audio_service after it serializes ownership changes, without taking any locks. */
static bool audio_still_needed(void *user_ctx)
{
    (void)user_ctx;
    return s_running && !s_audio_preempted;
}

/* Takes audio for the PC on its first sound; called from the speaker and microphone tasks */
static bool audio_take(void)
{
    s_audio_used_us = esp_timer_get_time();
    bool owner = false;
    xSemaphoreTake(s_audio_lock, portMAX_DELAY);
    // The stream callback may have passed its earlier state check while stop released the PC audio.
    // Check again under the same lock as release before acquiring it again.
    if (!s_running || s_audio_preempted) {
        owner = false;
    } else if (audio_client_is_owner(s_audio)) {
        owner = true;
    } else if (audio_client_acquire_if(s_audio, audio_still_needed, NULL) == ESP_OK) {
        owner = audio_client_set_format(s_audio, CONFIG_UAC_SAMPLE_RATE, 16, CONFIG_UAC_SPEAKER_CHANNEL_NUM) == ESP_OK;
    }
    xSemaphoreGive(s_audio_lock);
    return owner;
}

static void audio_give(void)
{
    xSemaphoreTake(s_audio_lock, portMAX_DELAY);
    audio_client_release(s_audio);
    s_audio_preempted = false;
    xSemaphoreGive(s_audio_lock);
}

static void audio_timer_cb(void *arg)
{
    (void)arg;
    const bool idle = esp_timer_get_time() - s_audio_used_us > AUDIO_IDLE_US;
    if (idle && (audio_client_is_owner(s_audio) || s_audio_preempted)) {
        audio_give();
    }
}

static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *cb_ctx)
{
    (void)cb_ctx;
    if (!audio_take()) {
        return ESP_OK;
    }
    // The PC's own volume slider scales this stream; the board volume applies on top
    int16_t *samples = (int16_t *)buf;
    const int32_t gain = s_pc_mute ? 0 : s_pc_gain_q15;
    if (gain != 32768) {
        for (size_t i = 0; i < len / sizeof(int16_t); i++) {
            samples[i] = (int16_t)((samples[i] * gain) >> 15);
        }
    }
    audio_client_write(s_audio, buf, len);
    return ESP_OK;
}

static esp_err_t uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx)
{
    (void)cb_ctx;
    if (!audio_take() || audio_client_read(s_audio, buf, len) != ESP_OK) {
        // Silence keeps the stream going; the task paces itself
        memset(buf, 0, len);
    }
    *bytes_read = len;
    return ESP_OK;
}

static void uac_set_mute_cb(uint32_t mute, void *cb_ctx)
{
    (void)cb_ctx;
    s_pc_mute = mute != 0;
}

static void uac_set_volume_cb(uint32_t volume, void *cb_ctx)
{
    (void)cb_ctx;
    // 0-100 stands for -50 dB to 0 dB
    const float db = (float)volume / 2.0f - 50.0f;
    s_pc_gain_q15 = volume == 0 ? 0 : volume >= 100 ? 32768 : (int32_t)(powf(10.0f, db / 20.0f) * 32768.0f);
}

/* ---------------------------------------------------------------------------
 * Control
 * ------------------------------------------------------------------------- */

static esp_err_t prepare(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_free = xQueueCreate(FRAME_COUNT, sizeof(frame_slot_t *));
    s_filled = xQueueCreate(FRAME_COUNT, sizeof(frame_slot_t *));
    s_rx_lock = xSemaphoreCreateMutex();
    s_audio_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_free && s_filled && s_rx_lock && s_audio_lock, ESP_ERR_NO_MEM, TAG, "No memory for queues");

    const jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    for (int i = 0; i < FRAME_COUNT; i++) {
        size_t size = 0;
        s_slots[i].buf = jpeg_alloc_decoder_mem(USB_DISPLAY_BUFFER_SIZE, &mem_cfg, &size);
        ESP_RETURN_ON_FALSE(s_slots[i].buf, ESP_ERR_NO_MEM, TAG, "No memory for frame %d", i);
        s_slots[i].capacity = size;
        s_slots[i].frame.data = s_slots[i].buf;
        frame_slot_t *slot = &s_slots[i];
        xQueueSend(s_free, &slot, 0);
    }

    const audio_client_config_t audio_cfg = {
        .name = "PC",
        .on_preempt = on_audio_preempt,
    };
    ESP_RETURN_ON_ERROR(audio_client_register(&audio_cfg, &s_audio), TAG, "Register audio client failed");
    const esp_timer_create_args_t timer_args = {
        .callback = audio_timer_cb,
        .name = "usb_display_audio",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_audio_timer), TAG, "Create audio timer failed");

    // Its tasks wait until the PC opens a stream; TinyUSB itself runs in usb_service
    uac_device_config_t uac_cfg = {
        .skip_tinyusb_init = true,
        .output_cb = uac_output_cb,
        .input_cb = uac_input_cb,
        .set_mute_cb = uac_set_mute_cb,
        .set_volume_cb = uac_set_volume_cb,
        .cb_ctx = NULL,
        .spk_itf_num = ITF_DISPLAY_AUDIO_SPK,
        .mic_itf_num = ITF_DISPLAY_AUDIO_MIC,
    };
    ESP_RETURN_ON_ERROR(uac_device_init(&uac_cfg), TAG, "UAC init failed");
    s_ready = true;
    return ESP_OK;
}

/* Puts every frame not handed out back into the free queue */
static void reset_frames(void)
{
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    give_back(s_rx_slot);
    s_rx_slot = NULL;
    s_rx_skip = 0;
    xSemaphoreGive(s_rx_lock);
    frame_slot_t *slot = NULL;
    while (xQueueReceive(s_filled, &slot, 0) == pdTRUE) {
        give_back(slot);
    }
}

esp_err_t usb_display_start(void)
{
    ESP_RETURN_ON_ERROR(prepare(), TAG, "Prepare failed");
    if (s_running) {
        return ESP_OK;
    }
    reset_frames();
    memset(&s_stats, 0, sizeof(s_stats));
    s_last_frame_us = 0;
    s_touch_down_count = 0;
    s_audio_preempted = false;
    s_running = true;
    const esp_err_t ret = usb_service_start_display();
    if (ret != ESP_OK) {
        s_running = false;
        return ret;
    }
    esp_timer_start_periodic(s_audio_timer, AUDIO_CHECK_PERIOD_US);
    ESP_LOGI(TAG, "Screen attached as %dx%d", USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT);
    return ESP_OK;
}

esp_err_t usb_display_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_running = false;
    const esp_err_t ret = usb_service_stop();
    esp_timer_stop(s_audio_timer);
    audio_give();
    reset_frames();
    ESP_LOGI(TAG, "Screen detached");
    return ret;
}

usb_display_status_t usb_display_get_status(void)
{
    if (!s_running) {
        return USB_DISPLAY_OFF;
    }
    if (!tud_mounted() || tud_suspended()) {
        return USB_DISPLAY_WAITING_HOST;
    }
    const int64_t last = s_last_frame_us;
    return (last && esp_timer_get_time() - last < STREAM_TIMEOUT_US) ? USB_DISPLAY_STREAMING : USB_DISPLAY_CONNECTED;
}

void usb_display_get_stats(usb_display_stats_t *stats)
{
    if (stats) {
        *stats = s_stats;
    }
}
