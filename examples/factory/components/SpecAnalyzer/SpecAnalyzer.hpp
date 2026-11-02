#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "esp_dsp.h"
#include "board_app.h"
#include "audio_service.h"

namespace esp_brookesia::apps
{
    class SpecAnalyzer : public systems::phone::App
    {
    public:
        static SpecAnalyzer *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
        ~SpecAnalyzer();

        using systems::phone::App::endRecordResource;
        using systems::phone::App::startRecordResource;

    protected:
        SpecAnalyzer(bool use_status_bar, bool use_navigation_bar);

        bool run(void) override;
        bool back(void) override;
        bool close(void) override;
        bool init(void) override;
        bool deinit(void) override;
        bool pause(void) override;
        bool resume(void) override;

    private:
        static SpecAnalyzer *_instance;

        // Audio pipeline parameters. The codec returns interleaved frames from the
        // ES7210: MIC1 in the first slot and MIC2 in the second.
        static constexpr uint16_t N_SAMPLES = 1024;       // FFT window per channel
        static constexpr uint16_t HOP_SAMPLES = 512;      // New samples per FFT; windows overlap by half
        static constexpr uint16_t MIC_COUNT = 2;          // Microphones in each captured frame
        static constexpr uint16_t BAND_COUNT = 64;        // Log-spaced frequency bands shown by every style
        static constexpr uint16_t CANVAS_WIDTH = BOARD_LCD_H_RES;
        static constexpr uint16_t CANVAS_HEIGHT = BOARD_LCD_V_RES;

        // Pages in swipe order; the app always opens on the microphone test
        enum class Style : uint8_t {
            MIC_TEST = 0,  // Both microphones stacked, for production testing
            BARS,
            RADIAL,
            HALO,
            MAX,
        };

        lv_obj_t *_canvas;
        lv_obj_t *_style_label;
        lv_obj_t *_mic_labels[MIC_COUNT];
        lv_obj_t *_page_dots[static_cast<int>(Style::MAX)];
        Style _style;
        lv_timer_t *_timer;
        uint32_t _frame_count;
        TaskHandle_t _audio_task_handle;
        volatile bool _audio_task_running;
        audio_client_handle_t _audio_client;

        // Audio task buffers
        __attribute__((aligned(16))) int16_t _raw_data[HOP_SAMPLES * MIC_COUNT];    // Raw interleaved samples
        __attribute__((aligned(16))) float _history[MIC_COUNT][N_SAMPLES];          // Latest window per microphone
        __attribute__((aligned(16))) float _audio_buffer[N_SAMPLES];                // Windowed samples
        __attribute__((aligned(16))) float _wind[N_SAMPLES];                        // Hann window
        __attribute__((aligned(16))) float _fft_buffer[N_SAMPLES * 2];              // Complex FFT input
        __attribute__((aligned(16))) float _spectrum[N_SAMPLES / 2];                // Spectrum in dB

        // Published by the audio task, read by the UI
        float _band_db[MIC_COUNT][BAND_COUNT];                                      // Band levels in dB
        float _rms_db[MIC_COUNT];                                                   // Level of the last hop in dBFS
        float _peak_dbfs[MIC_COUNT];                                                // Largest sample of the last hop
        uint32_t _zero_hops[MIC_COUNT];                                             // Consecutive all-zero hops

        // Render state, owned by the LVGL timer
        float _level[MIC_COUNT][BAND_COUNT];                                        // Displayed levels in [0, 1]
        float _peak[MIC_COUNT][BAND_COUNT];                                         // Slowly falling peak levels
        float _shown_rms_db[MIC_COUNT];                                             // Smoothed level for the labels
        uint16_t _band_color[MIC_COUNT][BAND_COUNT];                                // RGB565 colors per band
        uint16_t _band_dim[BAND_COUNT];
        uint16_t _band_faint[BAND_COUNT];

        uint16_t *_pixels; // RGB565 canvas buffer, drawn directly by the styles below

        bool startCapture();
        void stopCapture();
        static void audio_fft_task(void *pvParameters);
        static void on_audio_preempt(audio_client_handle_t client, void *user_ctx);
        static void timer_cb(lv_timer_t *timer);
        static void knob_cb(int steps, void *user_ctx);
        static void canvas_event_cb(lv_event_t *e);

        void createPageDots(lv_obj_t *parent);
        void setStyle(int style, bool announce);
        void updateLevels(void);
        void updateMicLabels(void);
        void drawBars(void);
        void drawRadial(void);
        void drawHalo(void);
        void drawMicTest(void);
    };
}
