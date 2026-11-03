#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "esp_task.h"
#include "esp_heap_caps.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:SpecAnalyzer"
#include "esp_lib_utils.h"
#include "SpecAnalyzer.hpp"
#include "knob_service.h"
#include <algorithm>
#include <cmath>
#define APP_NAME "Spectrum"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(img_app_specanalyzer);

namespace esp_brookesia::apps {

static const char *const STYLE_NAMES[] = {"Mic Test", "Bars", "Radial", "Halo"};
static constexpr uint32_t FRAME_PERIOD_MS = 20;
static constexpr uint32_t LABEL_UPDATE_FRAMES = 10;     // Mic test labels refresh every 200 ms
static constexpr uint32_t NO_DATA_HOPS = 30;            // About 1 s of all-zero samples

// Mic test panels: MIC1 above MIC2, both inside the round panel
static constexpr int MIC_PANEL_TOP[] = {118, 262};
static constexpr int MIC_PANEL_HEIGHT = 90;

SpecAnalyzer *SpecAnalyzer::_instance = nullptr;

SpecAnalyzer *SpecAnalyzer::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new SpecAnalyzer(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

SpecAnalyzer::SpecAnalyzer(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_specanalyzer, true, use_status_bar, use_navigation_bar),
    _canvas(nullptr),
    _style_label(nullptr),
    _mic_labels{},
    _page_dots{},
    _style(Style::MIC_TEST),
    _timer(nullptr),
    _frame_count(0),
    _audio_task_handle(nullptr),
    _audio_task_running(false),
    _audio_client(nullptr),
    _pixels(nullptr)
{
    // Clear DSP state so the first rendered frame starts from silence.
    memset(_raw_data, 0, sizeof(_raw_data));
    memset(_history, 0, sizeof(_history));
    memset(_audio_buffer, 0, sizeof(_audio_buffer));
    memset(_fft_buffer, 0, sizeof(_fft_buffer));
    memset(_spectrum, 0, sizeof(_spectrum));
    for (int ch = 0; ch < MIC_COUNT; ch++) {
        for (int i = 0; i < BAND_COUNT; i++) {
            _band_db[ch][i] = -90.0f;
        }
        _rms_db[ch] = -90.0f;
        _peak_dbfs[ch] = -90.0f;
        _zero_hops[ch] = 0;
        _shown_rms_db[ch] = -90.0f;
    }
    memset(_level, 0, sizeof(_level));
    memset(_peak, 0, sizeof(_peak));

    // MIC1 runs from cyan through blue to magenta, MIC2 from orange to yellow
    for (int i = 0; i < BAND_COUNT; i++) {
        const uint16_t hue = (uint16_t)(190 + i * 140 / BAND_COUNT) % 360;
        _band_color[0][i] = lv_color_to_u16(lv_color_hsv_to_rgb(hue, 100, 100));
        _band_color[1][i] = lv_color_to_u16(lv_color_hsv_to_rgb(15 + i * 40 / BAND_COUNT, 100, 100));
        _band_dim[i] = lv_color_to_u16(lv_color_hsv_to_rgb(hue, 100, 55));
        _band_faint[i] = lv_color_to_u16(lv_color_hsv_to_rgb(hue, 100, 28));
    }
}

SpecAnalyzer::~SpecAnalyzer()
{
    close();
}

bool SpecAnalyzer::run(void)
{
    ESP_UTILS_LOGD("Run");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Prefer PSRAM for the large canvas buffer and fall back to SRAM if PSRAM is exhausted.
    size_t buf_size = CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    _pixels = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!_pixels) {
        ESP_UTILS_LOGE("PSRAM allocation failed! Using fallback SRAM");
        _pixels = (uint16_t *)malloc(buf_size);
    }
    if (!_pixels) {
        ESP_UTILS_LOGE("Canvas buffer allocation failed!");
        return false;
    }
    memset(_pixels, 0, buf_size);

    // The styles write RGB565 pixels straight into the canvas buffer; LVGL only shows it.
    _canvas = lv_canvas_create(lv_scr_act());
    if (!_canvas) {
        ESP_UTILS_LOGE("Canvas creation failed!");
        free(_pixels);
        _pixels = nullptr;
        return false;
    }
    lv_obj_set_size(_canvas, CANVAS_WIDTH, CANVAS_HEIGHT);
    lv_obj_align(_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_set_buffer(_canvas, _pixels, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    // Horizontal swipes on the canvas switch pages, so the canvas keeps its own gestures
    lv_obj_add_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_canvas, canvas_event_cb, LV_EVENT_GESTURE, this);

    // Initialize the ESP-DSP radix-2 FFT tables once before the audio task starts.
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_UTILS_LOGE("FFT init failed: %d", ret);
        close();
        return false;
    }

    // Hann window reduces spectral leakage before the FFT.
    dsps_wind_hann_f32(_wind, N_SAMPLES);
    ESP_UTILS_LOGI("FFT and window initialized");

    _timer = lv_timer_create(timer_cb, FRAME_PERIOD_MS, this);
    if (!_timer) {
        ESP_UTILS_LOGE("Spectrum timer creation failed!");
        close();
        return false;
    }

    if (!startCapture()) {
        close();
        return false;
    }

    // Mic test labels sit above each microphone panel and only show in that style
    for (int ch = 0; ch < MIC_COUNT; ch++) {
        _mic_labels[ch] = lv_label_create(scr);
        lv_obj_set_style_text_font(_mic_labels[ch], &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_align(_mic_labels[ch], LV_ALIGN_TOP_MID, 0, MIC_PANEL_TOP[ch] - 34);
        lv_obj_add_flag(_mic_labels[ch], LV_OBJ_FLAG_HIDDEN);
    }

    _style_label = lv_label_create(scr);
    lv_obj_set_style_text_font(_style_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(_style_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_style_label, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_style_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(_style_label, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_style_label, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_style_label, 6, LV_PART_MAIN);
    lv_obj_align(_style_label, LV_ALIGN_TOP_MID, 0, 40);

    createPageDots(scr);
    setStyle(static_cast<int>(Style::MIC_TEST), true);

    knob_service_attach(knob_cb, this);
    return true;
}

bool SpecAnalyzer::back(void)
{
    ESP_UTILS_LOGD("Back");
    notifyCoreClosed();
    return true;
}

bool SpecAnalyzer::close(void)
{
    ESP_UTILS_LOGD("Close");

    knob_service_detach(knob_cb, this);
    stopCapture();
    if (_audio_client) {
        audio_client_release(_audio_client);
    }

    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }

    if (_canvas) {
        lv_obj_del(_canvas);
        _canvas = nullptr;
    }

    if (_style_label) {
        lv_obj_del(_style_label);
        _style_label = nullptr;
    }

    for (int ch = 0; ch < MIC_COUNT; ch++) {
        if (_mic_labels[ch]) {
            lv_obj_del(_mic_labels[ch]);
            _mic_labels[ch] = nullptr;
        }
    }

    // The dots live in one container, which deletes them with it
    if (_page_dots[0]) {
        lv_obj_del(lv_obj_get_parent(_page_dots[0]));
    }
    memset(_page_dots, 0, sizeof(_page_dots));

    if (_pixels) {
        free(_pixels);
        _pixels = nullptr;
    }

    return true;
}

bool SpecAnalyzer::init() { return true; }
bool SpecAnalyzer::deinit() { return true; }
// The microphone is only held while the analyzer is in the foreground.
bool SpecAnalyzer::pause()
{
    knob_service_detach(knob_cb, this);
    stopCapture();
    if (_audio_client) {
        audio_client_release(_audio_client);
    }
    return true;
}

bool SpecAnalyzer::resume()
{
    if (_canvas) {
        knob_service_attach(knob_cb, this);
        if (!startCapture()) {
            ESP_UTILS_LOGE("Restart capture failed");
        }
    }
    return true;
}

bool SpecAnalyzer::startCapture()
{
    if (!_audio_client) {
        const audio_client_config_t config = {
            .name = APP_NAME,
            .on_preempt = on_audio_preempt,
            .user_ctx = this,
        };
        ESP_UTILS_CHECK_FALSE_RETURN(audio_client_register(&config, &_audio_client) == ESP_OK, false,
                                     "Register audio client failed");
    }
    ESP_UTILS_CHECK_FALSE_RETURN(audio_client_acquire(_audio_client) == ESP_OK, false, "Acquire microphone failed");

    // The codec read blocks, so it runs outside the LVGL task and publishes band levels.
    _audio_task_running = true;
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_fft_task,
        "audio_fft",
        8 * 1024,
        this,
        5,
        &_audio_task_handle,
        PRO_CPU_NUM
    );
    if (task_ret != pdPASS) {
        ESP_UTILS_LOGE("Audio FFT task creation failed!");
        _audio_task_running = false;
        audio_client_release(_audio_client);
        return false;
    }
    return true;
}

void SpecAnalyzer::stopCapture()
{
    if (!_audio_task_running && _audio_task_handle == nullptr) {
        return;
    }
    _audio_task_running = false;

    // One read is at most HOP_SAMPLES / 16 kHz long; allow a few of them.
    for (int i = 0; i < 20 && _audio_task_handle != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (_audio_task_handle) {
        vTaskDelete(_audio_task_handle);
        _audio_task_handle = nullptr;
    }
}

void SpecAnalyzer::on_audio_preempt(audio_client_handle_t client, void *user_ctx)
{
    (void)client;
    static_cast<SpecAnalyzer *>(user_ctx)->stopCapture();
}

// Audio processing task. Each FFT reuses the newer half of the previous window, so the
// spectrum updates every HOP_SAMPLES (32 ms at 16 kHz) instead of every full window.
void SpecAnalyzer::audio_fft_task(void *pvParameters)
{
    SpecAnalyzer *app = static_cast<SpecAnalyzer*>(pvParameters);
    constexpr int BIN_COUNT = N_SAMPLES / 2;

    while (app->_audio_task_running) {
        esp_err_t ret = audio_client_read(app->_audio_client, app->_raw_data, sizeof(app->_raw_data));
        if (ret != ESP_OK) {
            if (ret != ESP_ERR_INVALID_STATE) {
                ESP_UTILS_LOGW("Microphone read error: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        for (int ch = 0; ch < MIC_COUNT; ch++) {
            float *history = app->_history[ch];
            memmove(history, history + HOP_SAMPLES, (N_SAMPLES - HOP_SAMPLES) * sizeof(float));

            // Level statistics of the new samples feed the mic test labels
            float sum_sq = 0;
            int max_abs = 0;
            for (int i = 0; i < HOP_SAMPLES; i++) {
                const int16_t sample = app->_raw_data[i * MIC_COUNT + ch];
                history[N_SAMPLES - HOP_SAMPLES + i] = sample / 32768.0f;
                sum_sq += (float)sample * sample;
                max_abs = std::max(max_abs, std::abs((int)sample));
            }
            app->_rms_db[ch] = 20 * log10f(sqrtf(sum_sq / HOP_SAMPLES) / 32768.0f + 1e-9f);
            app->_peak_dbfs[ch] = 20 * log10f(max_abs / 32768.0f + 1e-9f);
            app->_zero_hops[ch] = (max_abs == 0) ? (app->_zero_hops[ch] + 1) : 0;

            dsps_mul_f32(history, app->_wind, app->_audio_buffer, N_SAMPLES, 1, 1, 1);
            for (int i = 0; i < N_SAMPLES; i++) {
                app->_fft_buffer[2 * i] = app->_audio_buffer[i];
                app->_fft_buffer[2 * i + 1] = 0;
            }
            dsps_fft2r_fc32(app->_fft_buffer, N_SAMPLES);
            dsps_bit_rev_fc32(app->_fft_buffer, N_SAMPLES);

            // Convert complex bins to dB. The floor term avoids log10(0).
            for (int i = 0; i < BIN_COUNT; i++) {
                float real = app->_fft_buffer[2 * i];
                float imag = app->_fft_buffer[2 * i + 1];
                float magnitude = sqrtf(real * real + imag * imag);
                app->_spectrum[i] = 20 * log10f(magnitude / BIN_COUNT + 1e-9);
            }

            // Group FFT bins into log-spaced bands so bass and treble get similar space and
            // publish the loudest bin of each band. Smoothing happens per frame in the UI.
            int lo = 1;
            for (int i = 0; i < BAND_COUNT; i++) {
                int hi = (int)lroundf(powf((float)BIN_COUNT, (float)(i + 1) / BAND_COUNT));
                hi = std::min(std::max(hi, lo + 1), BIN_COUNT);
                float band_db = -120.0f;
                for (int bin = lo; bin < hi; bin++) {
                    band_db = fmaxf(band_db, app->_spectrum[bin]);
                }
                lo = std::min(hi, BIN_COUNT - 1);
                app->_band_db[ch][i] = fmaxf(-90.0f, fminf(0.0f, band_db + 10.0f));
            }
        }
    }

    app->_audio_task_handle = nullptr;
    vTaskDelete(NULL);
}

void SpecAnalyzer::setStyle(int style, bool announce)
{
    const int count = static_cast<int>(Style::MAX);
    style = ((style % count) + count) % count;
    _style = static_cast<Style>(style);
    memset(_peak, 0, sizeof(_peak));

    for (int i = 0; i < count; i++) {
        if (_page_dots[i]) {
            const bool active = (i == style);
            lv_obj_set_width(_page_dots[i], active ? 22 : 8);
            lv_obj_set_style_bg_opa(_page_dots[i], active ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        }
    }

    for (int ch = 0; ch < MIC_COUNT; ch++) {
        if (_mic_labels[ch]) {
            if (_style == Style::MIC_TEST) {
                lv_obj_remove_flag(_mic_labels[ch], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_mic_labels[ch], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (_style == Style::MIC_TEST) {
        updateMicLabels();
    }

    if (announce && _style_label) {
        lv_label_set_text(_style_label, STYLE_NAMES[style]);
        lv_obj_set_style_opa(_style_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_fade_out(_style_label, 400, 1200);
    }
}

void SpecAnalyzer::knob_cb(int steps, void *user_ctx)
{
    SpecAnalyzer *app = static_cast<SpecAnalyzer *>(user_ctx);
    if (app && app->_canvas) {
        app->setStyle(static_cast<int>(app->_style) + steps, true);
    }
}

// Swipe left for the next page, right for the previous one
void SpecAnalyzer::canvas_event_cb(lv_event_t *e)
{
    SpecAnalyzer *app = static_cast<SpecAnalyzer *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    if (!app || !indev) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        app->setStyle(static_cast<int>(app->_style) + ((dir == LV_DIR_LEFT) ? 1 : -1), true);
        lv_indev_wait_release(indev);
    }
}

// Small dots near the bottom of the round panel; the current page is a wider white pill
void SpecAnalyzer::createPageDots(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -64);

    for (int i = 0; i < static_cast<int>(Style::MAX); i++) {
        _page_dots[i] = lv_obj_create(bar);
        lv_obj_remove_style_all(_page_dots[i]);
        lv_obj_set_size(_page_dots[i], 8, 8);
        lv_obj_set_style_radius(_page_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_page_dots[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_page_dots[i], LV_OPA_40, LV_PART_MAIN);
        lv_obj_remove_flag(_page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

// Eases the displayed levels toward the latest spectrum: quick to rise, slower to fall.
void SpecAnalyzer::updateLevels(void)
{
    for (int ch = 0; ch < MIC_COUNT; ch++) {
        for (int i = 0; i < BAND_COUNT; i++) {
            float norm = (_band_db[ch][i] + 90.0f) / 90.0f;
            norm = fmaxf(0.0f, fminf(1.0f, norm));
            // Nonlinear mapping keeps quiet audio visible while preserving peaks.
            const float target = powf(norm, 0.8f);
            float &level = _level[ch][i];
            level += (target - level) * ((target > level) ? 0.55f : 0.18f);

            float &peak = _peak[ch][i];
            if (peak < level) {
                peak = level;
            } else {
                // Higher peaks fall faster, which gives a natural decay without another timer.
                peak = fmaxf(0.0f, peak - (0.003f + peak * 0.008f));
            }
        }
        _shown_rms_db[ch] += (_rms_db[ch] - _shown_rms_db[ch]) * 0.3f;
    }
}

// Labels for the mic test: level in dBFS and a verdict a tester can read at a glance.
void SpecAnalyzer::updateMicLabels(void)
{
    for (int ch = 0; ch < MIC_COUNT; ch++) {
        if (!_mic_labels[ch]) {
            continue;
        }
        const int other = (ch + 1) % MIC_COUNT;
        const char *verdict = "OK";
        uint32_t color = 0x4CAF50;
        if (_zero_hops[ch] >= NO_DATA_HOPS) {
            verdict = "NO DATA";
            color = 0xF44336;
        } else if (_peak_dbfs[ch] >= -1.0f) {
            verdict = "CLIP";
            color = 0xFF9800;
        } else if (_shown_rms_db[ch] < -75.0f) {
            verdict = "QUIET";
            color = 0x9E9E9E;
        } else if (_shown_rms_db[ch] < _shown_rms_db[other] - 20.0f) {
            // Both mics hear the same room, so a large gap points at this one
            verdict = "LOW";
            color = 0xFF9800;
        }
        lv_label_set_text_fmt(_mic_labels[ch], "MIC%d  %d dBFS  %s", ch + 1, (int)lroundf(_shown_rms_db[ch]), verdict);
        lv_obj_set_style_text_color(_mic_labels[ch], lv_color_hex(color), LV_PART_MAIN);
    }
}

/* Minimal RGB565 primitives. Drawing straight into the canvas buffer is far cheaper
 * than one LVGL draw task per line; shapes get one pixel of coverage-based edge
 * smoothing so diagonal strokes stay clean. */
namespace {

constexpr int W = BOARD_LCD_H_RES;
constexpr int H = BOARD_LCD_V_RES;
constexpr uint32_t OPA_MAX = 32;

// Blends fg over bg with alpha in [0, OPA_MAX]
inline uint16_t blend565(uint16_t bg, uint16_t fg, uint32_t alpha)
{
    const uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
    const uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
    const uint32_t r = ((((f - b) * alpha) >> 5) + b) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}

inline void plot(uint16_t *fb, int x, int y, uint16_t c, uint32_t alpha)
{
    uint16_t *p = fb + y * W + x;
    *p = (alpha >= OPA_MAX) ? c : blend565(*p, c, alpha);
}

inline void fill_span(uint16_t *fb, int y, int x1, int x2, uint16_t c)
{
    if (y < 0 || y >= H) {
        return;
    }
    x1 = std::max(x1, 0);
    x2 = std::min(x2, W - 1);
    uint16_t *p = fb + y * W + x1;
    for (int x = x1; x <= x2; x++) {
        *p++ = c;
    }
}

void fill_rect(uint16_t *fb, int x1, int y1, int x2, int y2, uint16_t c)
{
    for (int y = std::max(y1, 0); y <= std::min(y2, H - 1); y++) {
        fill_span(fb, y, x1, x2, c);
    }
}

// Filled disc: whole interior rows are filled directly, only the rim is smoothed
void aa_disc(uint16_t *fb, float cx, float cy, float r, uint16_t c)
{
    const float reach = r + 0.5f;
    const float solid = fmaxf(0.0f, r - 0.5f);
    const int y1 = std::max(0, (int)floorf(cy - reach));
    const int y2 = std::min(H - 1, (int)ceilf(cy + reach));
    for (int y = y1; y <= y2; y++) {
        const float dy = y - cy;
        if (fabsf(dy) > reach) {
            continue;
        }
        const float outer = sqrtf(fmaxf(0.0f, reach * reach - dy * dy));
        const float inner = (fabsf(dy) < solid) ? sqrtf(solid * solid - dy * dy) : -1.0f;
        int fill_l = W;
        int fill_r = -1;
        if (inner >= 0.0f) {
            fill_l = (int)ceilf(cx - inner);
            fill_r = (int)floorf(cx + inner);
            fill_span(fb, y, fill_l, fill_r, c);
        }
        const int xs = std::max(0, (int)floorf(cx - outer));
        const int xe = std::min(W - 1, (int)ceilf(cx + outer));
        for (int x = xs; x <= xe; x++) {
            if (x >= fill_l && x <= fill_r) {
                x = fill_r;
                continue;
            }
            const float dx = x - cx;
            const float cover = reach - sqrtf(dx * dx + dy * dy);
            if (cover > 0.0f) {
                plot(fb, x, y, c, (uint32_t)(fminf(cover, 1.0f) * OPA_MAX));
            }
        }
    }
}

// Round-capped stroke from (x0, y0) to (x1, y1). Each row only visits the band around
// the line; pixels well inside the stroke are written directly and only the edge
// pixels pay for a square root.
void aa_line(uint16_t *fb, float x0, float y0, float x1, float y1, float width, uint16_t c)
{
    const float reach = width * 0.5f + 0.5f;
    const float reach2 = reach * reach;
    const float solid = fmaxf(0.0f, reach - 1.0f);
    const float solid2 = solid * solid;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float inv_len2 = (len2 > 1e-6f) ? 1.0f / len2 : 0.0f;
    const float len = sqrtf(len2);
    const float ux = (len > 1e-3f) ? dx / len : 1.0f;
    const float uy = (len > 1e-3f) ? dy / len : 0.0f;
    const float inv_uy = (fabsf(uy) > 1e-3f) ? 1.0f / fabsf(uy) : 0.0f;

    const int bx1 = std::max(0, (int)floorf(fminf(x0, x1) - reach));
    const int bx2 = std::min(W - 1, (int)ceilf(fmaxf(x0, x1) + reach));
    const int by1 = std::max(0, (int)floorf(fminf(y0, y1) - reach));
    const int by2 = std::min(H - 1, (int)ceilf(fmaxf(y0, y1) + reach));

    for (int y = by1; y <= by2; y++) {
        const float ry = y - y0;
        int xs = bx1;
        int xe = bx2;
        if (inv_uy > 0.0f) {
            // Perpendicular distance to the line is linear in x along this row
            const float center = x0 + ry * ux / uy;
            const float half = reach * inv_uy;
            xs = std::max(xs, (int)floorf(center - half));
            xe = std::min(xe, (int)ceilf(center + half));
        }
        uint16_t *p = fb + y * W + xs;
        for (int x = xs; x <= xe; x++, p++) {
            const float rx = x - x0;
            const float t = fmaxf(0.0f, fminf(1.0f, (rx * dx + ry * dy) * inv_len2));
            const float ex = t * dx - rx;
            const float ey = t * dy - ry;
            const float d2 = ex * ex + ey * ey;
            if (d2 >= reach2) {
                continue;
            }
            if (d2 <= solid2) {
                *p = c;
                continue;
            }
            const float cover = reach - sqrtf(d2);
            *p = blend565(*p, c, (uint32_t)(fminf(cover, 1.0f) * OPA_MAX));
        }
    }
}

} // namespace

// Classic bars on a baseline below the center, with a dim reflection. The band area
// stays inside the round panel.
void SpecAnalyzer::drawBars(void)
{
    const int pitch = 360 / BAND_COUNT;
    const int left = (CANVAS_WIDTH - pitch * BAND_COUNT) / 2;
    const int bar_width = pitch - 1;
    const int base_y = CANVAS_HEIGHT * 62 / 100;
    const int max_height = 170;
    const int reflection = 50;
    const uint16_t white = lv_color_to_u16(lv_color_white());

    for (int i = 0; i < BAND_COUNT; i++) {
        const int x1 = left + i * pitch;
        const int x2 = x1 + bar_width - 1;
        const int height = 4 + (int)(_level[0][i] * max_height);

        fill_rect(_pixels, x1, base_y - height, x2, base_y, _band_color[0][i]);
        fill_rect(_pixels, x1, base_y + 3, x2, base_y + 3 + height * reflection / max_height, _band_faint[i]);

        const int peak_y = base_y - 4 - (int)(_peak[0][i] * max_height);
        fill_rect(_pixels, x1, peak_y - 2, x2, peak_y, white);
    }
}

// Spokes around a pulsing core, mirrored left/right so the low bands sit at the top.
void SpecAnalyzer::drawRadial(void)
{
    constexpr int SPOKES = BAND_COUNT;
    const float cx = CANVAS_WIDTH / 2.0f;
    const float cy = CANVAS_HEIGHT / 2.0f;
    const float inner = 96.0f;
    const float max_len = 118.0f;
    const uint16_t white = lv_color_to_u16(lv_color_white());
    const float *level = _level[0];
    const float *peak = _peak[0];

    float energy = 0;
    for (int i = 0; i < BAND_COUNT / 4; i++) {
        energy += level[i];
    }
    energy /= (BAND_COUNT / 4);

    const float core_r = 56 + energy * 30;
    aa_disc(_pixels, cx, cy, core_r, lv_color_to_u16(lv_color_hsv_to_rgb(200, 80, 25 + (uint8_t)(energy * 55))));
    aa_disc(_pixels, cx, cy, core_r - 4, lv_color_to_u16(lv_color_hsv_to_rgb(200, 80, 12 + (uint8_t)(energy * 30))));

    for (int k = 0; k < SPOKES; k++) {
        // Spoke k uses band 2*k on the right half and mirrors it on the left half
        const int band = (k < SPOKES / 2) ? (2 * k) : (2 * (SPOKES - 1 - k));
        const float angle = (k + 0.5f) * 2.0f * (float)M_PI / SPOKES - (float)M_PI / 2;
        const float c = cosf(angle);
        const float sn = sinf(angle);
        const float len = 6.0f + level[band] * max_len;
        const float x0 = cx + c * inner;
        const float y0 = cy + sn * inner;
        const float x1 = cx + c * (inner + len);
        const float y1 = cy + sn * (inner + len);

        aa_line(_pixels, x0, y0, x1, y1, 4.5f, _band_color[0][band]);
        const float p = inner + 10.0f + peak[band] * max_len;
        aa_disc(_pixels, cx + c * p, cy + sn * p, 2.0f, white);
    }
}

// A closed outline whose radius follows the spectrum, with a faint ring for the peaks.
void SpecAnalyzer::drawHalo(void)
{
    constexpr int POINTS = BAND_COUNT * 2;
    const float cx = CANVAS_WIDTH / 2.0f;
    const float cy = CANVAS_HEIGHT / 2.0f;
    const float base = 112.0f;
    const float amp = 96.0f;

    float px[POINTS + 1], py[POINTS + 1], qx[POINTS + 1], qy[POINTS + 1];
    for (int k = 0; k <= POINTS; k++) {
        const int idx = k % POINTS;
        const int band = (idx < BAND_COUNT) ? idx : (POINTS - 1 - idx);
        const float angle = idx * 2.0f * (float)M_PI / POINTS - (float)M_PI / 2;
        const float c = cosf(angle);
        const float sn = sinf(angle);
        const float r = base + _level[0][band] * amp;
        const float rp = base + 8.0f + _peak[0][band] * amp;
        px[k] = cx + c * r;
        py[k] = cy + sn * r;
        qx[k] = cx + c * rp;
        qy[k] = cy + sn * rp;
    }

    for (int k = 0; k < POINTS; k++) {
        const int band = (k < BAND_COUNT) ? k : (POINTS - 1 - k);
        aa_line(_pixels, qx[k], qy[k], qx[k + 1], qy[k + 1], 1.5f, _band_dim[band]);
        aa_line(_pixels, px[k], py[k], px[k + 1], py[k + 1], 3.5f, _band_color[0][band]);
    }
}

// Two stacked spectra, MIC1 above MIC2, with a shared frequency axis
void SpecAnalyzer::drawMicTest(void)
{
    const int pitch = 320 / BAND_COUNT;
    const int left = (CANVAS_WIDTH - pitch * BAND_COUNT) / 2;
    const int bar_width = pitch - 1;
    const uint16_t white = lv_color_to_u16(lv_color_white());
    const uint16_t axis = lv_color_to_u16(lv_color_hex(0x404040));

    for (int ch = 0; ch < MIC_COUNT; ch++) {
        const int base_y = MIC_PANEL_TOP[ch] + MIC_PANEL_HEIGHT;
        fill_rect(_pixels, left, base_y + 2, left + pitch * BAND_COUNT - 2, base_y + 2, axis);
        for (int i = 0; i < BAND_COUNT; i++) {
            const int x1 = left + i * pitch;
            const int x2 = x1 + bar_width - 1;
            const int height = 2 + (int)(_level[ch][i] * (MIC_PANEL_HEIGHT - 2));
            fill_rect(_pixels, x1, base_y - height, x2, base_y, _band_color[ch][i]);

            const int peak_y = base_y - 2 - (int)(_peak[ch][i] * (MIC_PANEL_HEIGHT - 2));
            fill_rect(_pixels, x1, peak_y - 1, x2, peak_y, white);
        }
    }
}

// LVGL timer callback. The FFT task only publishes numbers; all drawing happens here.
void SpecAnalyzer::timer_cb(lv_timer_t *timer)
{
    SpecAnalyzer *app = static_cast<SpecAnalyzer*>(lv_timer_get_user_data(timer));
    if (!app || !app->_canvas || !app->_pixels) return;

    app->updateLevels();

    memset(app->_pixels, 0, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t));
    switch (app->_style) {
    case Style::RADIAL:
        app->drawRadial();
        break;
    case Style::HALO:
        app->drawHalo();
        break;
    case Style::MIC_TEST:
        app->drawMicTest();
        if (++app->_frame_count % LABEL_UPDATE_FRAMES == 0) {
            app->updateMicLabels();
        }
        break;
    case Style::BARS:
    default:
        app->drawBars();
        break;
    }
    lv_obj_invalidate(app->_canvas);
}

} // namespace esp_brookesia::apps
