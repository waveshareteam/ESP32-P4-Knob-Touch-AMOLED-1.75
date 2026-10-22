#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"
#include "lvgl.h"

static const char *TAG = "12_mic_spectrum";

#define MIC_SAMPLE_RATE_HZ 16000
#define MIC_CHANNELS 2
#define MIC_BITS 16
#define FFT_SIZE 512
#define FFT_BINS (FFT_SIZE / 2)
#define BAR_COUNT 32
#define SPECTRUM_QUEUE_DEPTH 2
#define UI_REFRESH_MS 50
#define DISPLAY_W 466
#define DISPLAY_H 466
#define BARS_X 18
#define BARS_Y 116
#define BARS_W 430
#define BARS_H 246
#define BAR_GAP 4
#define BAR_W ((BARS_W - ((BAR_COUNT - 1) * BAR_GAP)) / BAR_COUNT)

typedef struct {
    float bars[BAR_COUNT];
    float rms_db;
    float peak_db;
    uint32_t frame_index;
    uint32_t queue_overruns;
    uint32_t read_errors;
    esp_err_t last_error;
    bool audio_ready;
} spectrum_frame_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *metrics;
    lv_obj_t *counts;
    lv_obj_t *output;
    lv_obj_t *bars[BAR_COUNT];
} ui_state_t;

static esp_codec_dev_handle_t s_mic;
static lv_display_t *s_disp;
static QueueHandle_t s_spectrum_queue;
static ui_state_t s_ui;
static int16_t s_pcm[FFT_SIZE * MIC_CHANNELS];
static float s_mono[FFT_SIZE];
static float s_fft[FFT_SIZE * 2];
static float s_hann[FFT_SIZE];
static float s_bar_smooth[BAR_COUNT];
static uint32_t s_queue_overruns;
static uint32_t s_read_errors;
static bool s_ui_ready;

static void pa_off(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BSP_POWER_AMP_IO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_conf);
    (void)gpio_set_level(BSP_POWER_AMP_IO, 0);
}

static float clampf_local(float value, float lo, float hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static void publish_frame(const spectrum_frame_t *frame)
{
    if (!s_spectrum_queue) {
        return;
    }
    if (xQueueSend(s_spectrum_queue, frame, 0) == pdTRUE) {
        return;
    }

    spectrum_frame_t dropped;
    (void)xQueueReceive(s_spectrum_queue, &dropped, 0);
    s_queue_overruns++;

    spectrum_frame_t latest = *frame;
    latest.queue_overruns = s_queue_overruns;
    (void)xQueueSend(s_spectrum_queue, &latest, 0);
}

static void build_error_frame(spectrum_frame_t *frame, esp_err_t err)
{
    memset(frame, 0, sizeof(*frame));
    frame->audio_ready = false;
    frame->last_error = err;
    frame->read_errors = s_read_errors;
    frame->queue_overruns = s_queue_overruns;
    frame->rms_db = -96.0f;
    frame->peak_db = -96.0f;
}

static esp_err_t process_fft_frame(spectrum_frame_t *frame, uint32_t frame_index)
{
    float sum = 0.0f;
    float sum_sq = 0.0f;
    float peak = 0.0f;

    for (int i = 0; i < FFT_SIZE; ++i) {
        int32_t mixed = 0;
        for (int ch = 0; ch < MIC_CHANNELS; ++ch) {
            mixed += s_pcm[(i * MIC_CHANNELS) + ch];
        }
        float sample = (float)mixed / ((float)MIC_CHANNELS * 32768.0f);
        s_mono[i] = sample;
        sum += sample;
        sum_sq += sample * sample;
        float abs_sample = fabsf(sample);
        if (abs_sample > peak) {
            peak = abs_sample;
        }
    }

    float dc = sum / (float)FFT_SIZE;
    for (int i = 0; i < FFT_SIZE; ++i) {
        s_fft[i * 2] = (s_mono[i] - dc) * s_hann[i];
        s_fft[(i * 2) + 1] = 0.0f;
    }

    esp_err_t ret = dsps_fft2r_fc32(s_fft, FFT_SIZE);
    if (ret != ESP_OK) {
        return ret;
    }
    dsps_bit_rev_fc32(s_fft, FFT_SIZE);
    dsps_cplx2reC_fc32(s_fft, FFT_SIZE);

    memset(frame, 0, sizeof(*frame));
    frame->frame_index = frame_index;
    frame->audio_ready = true;
    frame->last_error = ESP_OK;
    frame->read_errors = s_read_errors;
    frame->queue_overruns = s_queue_overruns;
    frame->rms_db = 20.0f * log10f(sqrtf((sum_sq / (float)FFT_SIZE) + 1.0e-12f));
    frame->peak_db = 20.0f * log10f(peak + 1.0e-6f);

    for (int bar = 0; bar < BAR_COUNT; ++bar) {
        int start = 1 + ((bar * bar) * (FFT_BINS - 1)) / (BAR_COUNT * BAR_COUNT);
        int end = 1 + (((bar + 1) * (bar + 1)) * (FFT_BINS - 1)) / (BAR_COUNT * BAR_COUNT);
        if (end <= start) {
            end = start + 1;
        }
        if (end > FFT_BINS) {
            end = FFT_BINS;
        }

        float mag_sum = 0.0f;
        int mag_count = 0;
        for (int bin = start; bin < end; ++bin) {
            float re = s_fft[bin * 2];
            float im = s_fft[(bin * 2) + 1];
            mag_sum += sqrtf((re * re) + (im * im)) / (float)FFT_SIZE;
            mag_count++;
        }
        float avg_mag = mag_count > 0 ? mag_sum / (float)mag_count : 0.0f;
        float db = 20.0f * log10f(avg_mag + 1.0e-6f);
        float normalized = clampf_local((db + 72.0f) / 72.0f, 0.0f, 1.0f);
        s_bar_smooth[bar] = (s_bar_smooth[bar] * 0.68f) + (normalized * 0.32f);
        frame->bars[bar] = s_bar_smooth[bar];
    }

    return ESP_OK;
}

static void mic_fft_task(void *arg)
{
    (void)arg;
    uint32_t frame_index = 0;

    while (1) {
        int ret = esp_codec_dev_read(s_mic, s_pcm, sizeof(s_pcm));
        if (ret != ESP_CODEC_DEV_OK) {
            s_read_errors++;
            spectrum_frame_t frame;
            build_error_frame(&frame, ESP_FAIL);
            publish_frame(&frame);
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        spectrum_frame_t frame;
        ret = process_fft_frame(&frame, ++frame_index);
        if (ret != ESP_OK) {
            s_read_errors++;
            build_error_frame(&frame, ret);
        }
        publish_frame(&frame);
    }
}

static lv_color_t bar_color(float level)
{
    if (level > 0.78f) {
        return lv_color_hex(0xff7056);
    }
    if (level > 0.52f) {
        return lv_color_hex(0xffc857);
    }
    return lv_color_hex(0x25d9a5);
}

static void ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071013), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_ui.title = lv_label_create(screen);
    lv_label_set_text(s_ui.title, "MIC SPECTRUM");
    lv_obj_set_style_text_color(s_ui.title, lv_color_hex(0xf7f2df), 0);
    lv_obj_set_width(s_ui.title, DISPLAY_W - 36);
    lv_obj_align(s_ui.title, LV_ALIGN_TOP_MID, 0, 18);

    s_ui.status = lv_label_create(screen);
    lv_label_set_text(s_ui.status, "Sampling: starting");
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0x9ed8d0), 0);
    lv_obj_set_width(s_ui.status, DISPLAY_W - 36);
    lv_obj_align(s_ui.status, LV_ALIGN_TOP_LEFT, 18, 52);

    s_ui.output = lv_label_create(screen);
    lv_label_set_text(s_ui.output, "Output codec: disabled");
    lv_obj_set_style_text_color(s_ui.output, lv_color_hex(0xd7c78f), 0);
    lv_obj_set_width(s_ui.output, DISPLAY_W - 36);
    lv_obj_align(s_ui.output, LV_ALIGN_TOP_LEFT, 18, 78);

    for (int i = 0; i < BAR_COUNT; ++i) {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_remove_style_all(bar);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x25d9a5), 0);
        lv_obj_set_style_radius(bar, 2, 0);
        int x = BARS_X + (i * (BAR_W + BAR_GAP));
        lv_obj_set_pos(bar, x, BARS_Y + BARS_H - 4);
        lv_obj_set_size(bar, BAR_W, 4);
        s_ui.bars[i] = bar;
    }

    s_ui.metrics = lv_label_create(screen);
    lv_label_set_text(s_ui.metrics, "RMS --.- dBFS  Peak --.- dBFS");
    lv_obj_set_style_text_color(s_ui.metrics, lv_color_hex(0xf7f2df), 0);
    lv_obj_set_width(s_ui.metrics, DISPLAY_W - 36);
    lv_obj_align(s_ui.metrics, LV_ALIGN_TOP_LEFT, 18, 382);

    s_ui.counts = lv_label_create(screen);
    lv_label_set_text(s_ui.counts, "Overrun 0  Error 0");
    lv_obj_set_style_text_color(s_ui.counts, lv_color_hex(0x9ed8d0), 0);
    lv_obj_set_width(s_ui.counts, DISPLAY_W - 36);
    lv_obj_align(s_ui.counts, LV_ALIGN_TOP_LEFT, 18, 414);
}

static void ui_update(const spectrum_frame_t *frame)
{
    if (!frame) {
        return;
    }

    if (frame->audio_ready) {
        lv_label_set_text_fmt(s_ui.status, "Sampling: ES7210 input  Frame %lu", (unsigned long)frame->frame_index);
    } else {
        lv_label_set_text_fmt(s_ui.status, "Sampling error: %s", esp_err_to_name(frame->last_error));
    }
    lv_label_set_text_fmt(s_ui.metrics, "RMS %.1f dBFS  Peak %.1f dBFS", frame->rms_db, frame->peak_db);
    lv_label_set_text_fmt(s_ui.counts, "Overrun %lu  Error %lu",
                          (unsigned long)frame->queue_overruns, (unsigned long)frame->read_errors);

    for (int i = 0; i < BAR_COUNT; ++i) {
        float level = clampf_local(frame->bars[i], 0.0f, 1.0f);
        int height = 4 + (int)(level * (float)(BARS_H - 4));
        int x = BARS_X + (i * (BAR_W + BAR_GAP));
        int y = BARS_Y + BARS_H - height;
        lv_obj_set_pos(s_ui.bars[i], x, y);
        lv_obj_set_size(s_ui.bars[i], BAR_W, height);
        lv_obj_set_style_bg_color(s_ui.bars[i], bar_color(level), 0);
    }
}

static void ui_show_error(const char *stage, esp_err_t err)
{
    ESP_LOGE(TAG, "%s FAILED: %s", stage, esp_err_to_name(err));
    if (!s_ui_ready) {
        return;
    }
    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "%s FAILED: LVGL lock timeout", stage);
        return;
    }
    lv_label_set_text_fmt(s_ui.status, "FAILED: %s: %s", stage, esp_err_to_name(err));
    lv_label_set_text(s_ui.metrics, "RMS --.- dBFS  Peak --.- dBFS");
    lv_label_set_text(s_ui.counts, "Overrun 0  Error 1");
    for (int i = 0; i < BAR_COUNT; ++i) {
        lv_obj_set_size(s_ui.bars[i], BAR_W, 4);
        lv_obj_set_pos(s_ui.bars[i], BARS_X + (i * (BAR_W + BAR_GAP)), BARS_Y + BARS_H - 4);
        lv_obj_set_style_bg_color(s_ui.bars[i], lv_color_hex(0x46525a), 0);
    }
    bsp_display_unlock();
}

static esp_err_t start_audio_pipeline(void)
{
    s_spectrum_queue = xQueueCreate(SPECTRUM_QUEUE_DEPTH, sizeof(spectrum_frame_t));
    if (!s_spectrum_queue) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        return ret;
    }
    dsps_wind_hann_f32(s_hann, FFT_SIZE);

    s_mic = bsp_audio_codec_microphone_init();
    if (!s_mic) {
        return ESP_FAIL;
    }
    int codec_ret = esp_codec_dev_set_in_gain(s_mic, 24.0f);
    if (codec_ret != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(s_mic);
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = MIC_SAMPLE_RATE_HZ,
        .channel = MIC_CHANNELS,
        .bits_per_sample = MIC_BITS,
    };
    codec_ret = esp_codec_dev_open(s_mic, &fs);
    if (codec_ret != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(s_mic);
        return ESP_FAIL;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(mic_fft_task, "mic_fft", 8192, NULL, 5, NULL, tskNO_AFFINITY);
    if (task_ok != pdPASS) {
        esp_codec_dev_close(s_mic);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_main(void)
{
    pa_off();

    s_disp = bsp_display_start();
    if (!s_disp) {
        ESP_LOGE(TAG, "LVGL INIT FAILED: %s", esp_err_to_name(ESP_FAIL));
        pa_off();
        return;
    }

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "LVGL UI CREATE FAILED: lock timeout");
        pa_off();
        return;
    }
    ui_create();
    s_ui_ready = true;
    bsp_display_unlock();

    esp_err_t ret = start_audio_pipeline();
    if (ret != ESP_OK) {
        pa_off();
        ui_show_error("ES7210 input pipeline", ret);
    } else {
        ESP_LOGI(TAG, "ES7210 input running at %d Hz, %d-point FFT; output codec disabled", MIC_SAMPLE_RATE_HZ, FFT_SIZE);
    }

    int64_t next_ui_us = esp_timer_get_time();
    while (1) {
        int64_t now = esp_timer_get_time();
        if (now >= next_ui_us) {
            spectrum_frame_t frame;
            spectrum_frame_t latest;
            bool have_latest = false;
            while (s_spectrum_queue && xQueueReceive(s_spectrum_queue, &frame, 0) == pdTRUE) {
                latest = frame;
                have_latest = true;
            }
            if (have_latest && bsp_display_lock(-1)) {
                ui_update(&latest);
                bsp_display_unlock();
            }
            next_ui_us = now + ((int64_t)UI_REFRESH_MS * 1000LL);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}