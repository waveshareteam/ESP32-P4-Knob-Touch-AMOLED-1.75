#pragma once

#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "board_app.h"
#include "storage_service.h"
#include "audio_service.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_jpeg_dec.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "sdkconfig.h"
#include "media_reader.h"

namespace esp_brookesia::apps
{

    /**
     * @brief Plays MP4, MOV and AVI files with MJPEG video from /sdcard/videos
     *
     * Video and audio each have a reader of their own. Audio (AAC, MP3 or PCM) plays through the
     * audio service and sets the clock; video frames are shown on that clock, and frames that come
     * too late are skipped so the picture catches up.
     */
    class VideoPlayer : public systems::phone::App
    {
    public:
        static VideoPlayer *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
        ~VideoPlayer();

    protected:
        VideoPlayer(bool use_status_bar, bool use_navigation_bar);
        bool run(void) override;
        bool back(void) override;
        bool close(void) override;
        bool init(void) override;
        bool deinit(void) override;
        bool pause(void) override;
        bool resume(void) override;

    private:
        enum class PlayResult {
            DONE,       // The file played to its end
            STOPPED,    // Paused or closed; resume_ms says where
            FAILED,     // The file can't be played
        };

        static VideoPlayer *_instance;

        static constexpr int SWIPE_EXIT_THRESHOLD = 180;

        lv_display_t *display = nullptr;
        esp_lcd_panel_handle_t display_panel = nullptr;
        ppa_client_handle_t ppa_srm_handle = nullptr;
        esp_lcd_touch_handle_t touch_handle = nullptr;
        void *lcd_buffers[BOARD_LCD_DPI_BUFFER_NUMS] = {};
        int current_buf_idx = 0;
        size_t data_cache_line_size = 64;
        uint16_t last_video_width = 0;
        uint16_t last_video_height = 0;
        bool dummy_enabled = false;
        bool lvgl_paused = false;
        bool touch_active = false;
        uint16_t touch_start_x = 0;
        uint16_t touch_start_y = 0;

        jpeg_dec_handle_t jpeg_handle = nullptr;           // Software decoder, for frames the hardware rejects
        uint8_t *jpeg_output_buffer = nullptr;
        int jpeg_output_buffer_size = 0;
        jpeg_decoder_handle_t hw_jpeg_handle = nullptr;
        uint8_t *hw_jpeg_buffer = nullptr;
        size_t hw_jpeg_buffer_size = 0;

        TaskHandle_t play_task_handle = nullptr;
        SemaphoreHandle_t play_task_done = nullptr;
        SemaphoreHandle_t handoff_done = nullptr;
        lv_timer_t *exit_timer = nullptr;
        volatile bool loop_playback = false;
        volatile bool is_paused = false;
        volatile bool stop_requested = false;
        volatile bool exit_requested = false;
        std::vector<std::string> files;
        size_t file_index = 0;
        uint32_t resume_ms = 0;

        // Audio goes through the audio service: the video takes it over from music while it plays
        audio_client_handle_t audio_client = nullptr;
        volatile bool audio_preempted = false;  // Another app took audio; stay silent until resumed
        // One task reads and decodes into a short PCM queue, another feeds the codec from it, so a
        // card read that waits for the video reader does not leave the codec without sound
        TaskHandle_t audio_task_handle = nullptr;
        TaskHandle_t audio_out_task_handle = nullptr;
        SemaphoreHandle_t audio_task_done = nullptr;
        RingbufHandle_t audio_ring = nullptr;
        volatile bool audio_task_run = false;
        volatile bool audio_reader_done = false;
        volatile bool audio_started = false;    // First sound written, or the file plays without
        media_file_t *audio_file = nullptr;     // Shared with the video reader of the same file
        const media_reader_t *audio_peer = nullptr; // That video reader, which audio reading keeps near
        uint32_t audio_start_ms = 0;
        volatile uint32_t audio_rate = 0;       // Format of the queued PCM, set before it is queued
        volatile uint8_t audio_channels = 0;
        volatile int64_t audio_base_ms = -1;

        // Media time at a moment of esp_timer time; the audio task keeps moving it to the sound
        portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;
        int64_t clock_media_ms = 0;
        int64_t clock_stamp_us = 0;

        lv_obj_t *status_label = nullptr;

        void scanFiles(const char *dir_path);
        bool startPlayback();
        void stopPlayback();
        bool takeDisplayLock();
        esp_err_t initDisplayBypass();
        void deinitDisplayBypass();
        void restoreDisplayBypass();
        esp_err_t initJpegDecoder();
        void deinitJpegDecoder();
        esp_err_t ensureJpegOutputBuffer(int out_len);
        void releaseJpegOutputBuffer();
        bool decodeFrameHardware(uint8_t *data, size_t len, uint16_t *width, uint16_t *height, uint16_t *stride,
                                 uint16_t *rows);
        void renderFrame(uint8_t *data, size_t len);
        bool shouldExitBySwipe();
        bool shouldStop(PlayResult *result, bool *request_close);
        PlayResult playFile(const std::string &path, uint32_t start_ms, bool *request_close);
        void acquireAudio();
        void releaseAudio();
        void startAudioTask(media_file_t *file, const media_reader_t *video, uint32_t start_ms);
        void stopAudioTask();
        void setClock(int64_t media_ms);
        int64_t clockMs();

        static void playTask(void *arg);
        static void enterDisplayBypassCb(void *arg);
        static void exitTimerCb(lv_timer_t *timer);
        static void audioTask(void *arg);
        static void audioOutTask(void *arg);
        static void onAudioPreempt(audio_client_handle_t client, void *user_ctx);
        static void knobCallback(int steps, void *user_ctx);
        static void onStorageEvent(storage_event_t event, void *user_ctx);
    };

} // namespace esp_brookesia::apps
