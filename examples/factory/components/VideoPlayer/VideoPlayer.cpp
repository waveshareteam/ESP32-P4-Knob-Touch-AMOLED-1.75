/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "VideoPlayer.hpp"
#include <algorithm>
#include <dirent.h>
#include <strings.h>
#include "lvgl.h"
#include "esp_brookesia.hpp"

#include "board_app.h"
#include "esp_lib_utils.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "knob_service.h"
#include "audio_track.h"
#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:VideoPlayer"

LV_IMG_DECLARE(img_app_vedioplayer);

#define DISPLAY_WIDTH  BOARD_LCD_H_RES
#define DISPLAY_HEIGHT BOARD_LCD_V_RES
#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))
#define KNOB_VOLUME_STEP 5
#define VIDEO_POOL_BYTES (1024 * 1024)  // Twice a large MJPEG frame
#define AUDIO_POOL_BYTES (256 * 1024)   // AVI files may carry big PCM chunks
#define AUDIO_QUEUE_BYTES (48 * 1024)   // About 0.3 s of decoded sound
#define AUDIO_WRITE_BYTES 4096
/* Audio reading may run ahead of the video reader beyond the file cache while less is queued */
#define AUDIO_MIN_QUEUED_MS 120
/* Written audio sits in the codec's DMA buffer for about this long before it is heard */
#define AUDIO_LATENCY_MS 40
/* A frame this many frame times late is skipped instead of shown, a few in a row at most */
#define LATE_FRAMES      2
#define MAX_DROPS_IN_ROW 6
#define MAX_WAIT_MS      50
/* Further behind than this the card can't keep up: jump to the sound instead of reading every frame */
#define CATCH_UP_MS      1000
/* The first frame waits this long at most for the sound, whose reader may still be indexing */
#define AUDIO_START_TIMEOUT_MS 5000
#define DISPLAY_LOCK_WAIT_MS 20

#if (BOARD_LCD_BITS_PER_PIXEL == 16)
#define VIDEO_JPEG_PIXEL_FORMAT JPEG_PIXEL_FORMAT_RGB565_LE
#define VIDEO_PPA_COLOR_MODE    PPA_SRM_COLOR_MODE_RGB565
#define VIDEO_BYTES_PER_PIXEL   2
#elif (BOARD_LCD_BITS_PER_PIXEL == 24)
#define VIDEO_JPEG_PIXEL_FORMAT JPEG_PIXEL_FORMAT_RGB888
#define VIDEO_PPA_COLOR_MODE    PPA_SRM_COLOR_MODE_RGB888
#define VIDEO_BYTES_PER_PIXEL   3
#else
#error "Unsupported LCD color format"
#endif

namespace esp_brookesia::apps
{

    VideoPlayer *VideoPlayer::_instance = nullptr;

    VideoPlayer *VideoPlayer::requestInstance(bool use_status_bar, bool use_navigation_bar)
    {
        if (_instance == nullptr)
        {
            _instance = new VideoPlayer(use_status_bar, use_navigation_bar);
        }
        return _instance;
    }

    VideoPlayer::VideoPlayer(bool use_status_bar, bool use_navigation_bar) : App("VideoPlayer", &img_app_vedioplayer, true, use_status_bar, use_navigation_bar)
    {
        play_task_done = xSemaphoreCreateBinary();
        handoff_done = xSemaphoreCreateBinary();
        xSemaphoreGive(play_task_done);
    }

    VideoPlayer::~VideoPlayer()
    {
        close();
    }

    bool VideoPlayer::run(void)
    {
        ESP_UTILS_LOGD("Run");
        is_paused = false;
        audio_preempted = false;
        const bool sd_mounted = storage_sd_is_mounted();
        board_display_lock(-1);
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
        status_label = lv_label_create(lv_scr_act());
        lv_label_set_text(status_label, sd_mounted ? "loading files..." : "microSD unavailable");
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(status_label, DISPLAY_WIDTH);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
        board_display_unlock();

        if (!sd_mounted)
        {
            return true;
        }

        scanFiles(STORAGE_DIR_VIDEOS);
        if (files.empty())
        {
            board_display_lock(-1);
            lv_label_set_text(status_label, "no MP4 or AVI files\nin /videos");
            board_display_unlock();
            return true;
        }

        board_display_lock(-1);
        lv_obj_del(status_label);
        status_label = nullptr;
        board_display_unlock();

        exit_timer = lv_timer_create(exitTimerCb, 100, this);
        file_index = 0;
        resume_ms = 0;
        if (!startPlayback()) {
            return false;
        }
        knob_service_attach(knobCallback, this);
        return true;
    }

    bool VideoPlayer::back(void)
    {
        ESP_UTILS_LOGD("Back");
        // If the app needs to exit, call notifyCoreClosed() to notify the core to close the app
        ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
        return true;
    }

    bool VideoPlayer::close()
    {
        ESP_UTILS_LOGD("Close");
        knob_service_detach(knobCallback, this);
        stopPlayback();

        stopAudioTask();
        releaseAudio();
        deinitJpegDecoder();
        releaseJpegOutputBuffer();
        if (exit_timer) {
            lv_timer_delete(exit_timer);
            exit_timer = nullptr;
        }

        board_display_lock(-1);
        if (status_label)
        {
            lv_obj_del(status_label);
            status_label = nullptr;
        }
        board_display_unlock();

        files.clear();
        return true;
    }

    bool VideoPlayer::init()
    {
        ESP_UTILS_LOGD("Init");
        // Playback must stop and files must close before the card goes to USB.
        storage_add_listener(onStorageEvent, this);
        audio_task_done = xSemaphoreCreateCounting(2, 0);  // Given once by each audio task
        ESP_UTILS_CHECK_FALSE_RETURN(audio_task_done != nullptr, false, "Create semaphore failed");
        const audio_client_config_t audio_config = {
            .name = "Video",
            .on_preempt = onAudioPreempt,
            .user_ctx = this,
        };
        ESP_UTILS_CHECK_FALSE_RETURN(audio_client_register(&audio_config, &audio_client) == ESP_OK, false,
                                     "Register audio client failed");
        return true;
    }

    void VideoPlayer::onStorageEvent(storage_event_t event, void *user_ctx)
    {
        if (event == STORAGE_EVENT_SD_RELEASE)
        {
            static_cast<VideoPlayer *>(user_ctx)->close();
        }
    }

    void VideoPlayer::exitTimerCb(lv_timer_t *timer)
    {
        VideoPlayer *self = static_cast<VideoPlayer *>(lv_timer_get_user_data(timer));
        if (self->exit_requested) {
            self->exit_requested = false;
            self->notifyCoreClosed();
        }
    }

    bool VideoPlayer::deinit()
    {
        ESP_UTILS_LOGD("Deinit");
        return true;
    }

    bool VideoPlayer::pause()
    {
        ESP_UTILS_LOGD("Pause");
        knob_service_detach(knobCallback, this);
        stopPlayback();
        return true;
    }

    bool VideoPlayer::resume()
    {
        ESP_UTILS_LOGD("Resume");
        // Coming back is a new request for sound, even if another app took it meanwhile
        audio_preempted = false;
        if (!startPlayback()) {
            return false;
        }
        knob_service_attach(knobCallback, this);
        return true;
    }

    bool VideoPlayer::startPlayback()
    {
        if (play_task_handle || files.empty()) {
            return true;
        }
        xSemaphoreTake(play_task_done, portMAX_DELAY);
        xSemaphoreTake(handoff_done, 0);
        stop_requested = false;
        exit_requested = false;
        is_paused = false;
        loop_playback = true;
        if (xTaskCreatePinnedToCore(playTask, "video_play", 16384, this, 7, &play_task_handle, 0) != pdPASS) {
            loop_playback = false;
            play_task_handle = nullptr;
            xSemaphoreGive(play_task_done);
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Create playback task failed");
            return false;
        }
        return true;
    }

    void VideoPlayer::stopPlayback()
    {
        stop_requested = true;
        loop_playback = false;
        is_paused = false;
        if (play_task_handle) {
            xSemaphoreTake(play_task_done, portMAX_DELAY);
            xSemaphoreGive(play_task_done);
            play_task_handle = nullptr;
        }
        lv_async_call_cancel(enterDisplayBypassCb, this);
        restoreDisplayBypass();
    }

    void VideoPlayer::scanFiles(const char *dir_path)
    {
        files.clear();
        DIR *dir = opendir(dir_path);
        if (!dir)
        {
            return;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (entry->d_type == DT_REG && media_reader_is_video_file(entry->d_name))
            {
                files.push_back(std::string(dir_path) + "/" + entry->d_name);
            }
        }
        closedir(dir);
        std::sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        });
    }

    esp_err_t VideoPlayer::initDisplayBypass()
    {
#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
        data_cache_line_size = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
#else
        data_cache_line_size = 64;
#endif

        if (ppa_srm_handle == nullptr) {
            ppa_client_config_t ppa_srm_config = {
                .oper_type = PPA_OPERATION_SRM,
            };
            ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_srm_config, &ppa_srm_handle), ESP_UTILS_LOG_TAG, "Register PPA SRM failed");
        }

        if (!takeDisplayLock()) {
            return ESP_ERR_INVALID_STATE;
        }
        xSemaphoreTake(handoff_done, 0);
        const lv_result_t queued = stop_requested ? LV_RESULT_INVALID : lv_async_call(enterDisplayBypassCb, this);
        board_display_unlock();
        if (queued != LV_RESULT_OK) {
            return ESP_FAIL;
        }
        while (!stop_requested) {
            if (xSemaphoreTake(handoff_done, pdMS_TO_TICKS(DISPLAY_LOCK_WAIT_MS)) == pdTRUE) {
                return dummy_enabled && lvgl_paused ? ESP_OK : ESP_FAIL;
            }
        }
        return ESP_ERR_INVALID_STATE;
    }

    void VideoPlayer::deinitDisplayBypass()
    {
        if (!takeDisplayLock()) {
            return;
        }
        restoreDisplayBypass();
        board_display_unlock();
    }

    void VideoPlayer::restoreDisplayBypass()
    {
        if (dummy_enabled && display) {
            esp_lv_adapter_set_dummy_draw(display, false);
            dummy_enabled = false;
        }
        if (lvgl_paused) {
            esp_lv_adapter_resume();
            lvgl_paused = false;
        }

        // The touch handle belongs to Board Manager and stays registered with LVGL.
        touch_handle = nullptr;

        touch_active = false;
        last_video_width = 0;
        last_video_height = 0;
    }

    bool VideoPlayer::takeDisplayLock()
    {
        while (!stop_requested) {
            if (board_display_lock(DISPLAY_LOCK_WAIT_MS)) {
                return true;
            }
        }
        return false;
    }

    void VideoPlayer::enterDisplayBypassCb(void *arg)
    {
        VideoPlayer *self = static_cast<VideoPlayer *>(arg);
        if (!self->stop_requested) {
            self->display = lv_display_get_default();
            self->display_panel = board_display_get_panel_handle();
            self->touch_handle = board_display_get_touch_handle();
            if (self->display && self->display_panel && self->touch_handle) {
#if BOARD_LCD_DPI_BUFFER_NUMS >= 3
                const esp_err_t fb_ret = esp_lcd_dpi_panel_get_frame_buffer(self->display_panel,
                    BOARD_LCD_DPI_BUFFER_NUMS, &self->lcd_buffers[0], &self->lcd_buffers[1], &self->lcd_buffers[2]);
#else
                const esp_err_t fb_ret = esp_lcd_dpi_panel_get_frame_buffer(self->display_panel,
                    BOARD_LCD_DPI_BUFFER_NUMS, &self->lcd_buffers[0], &self->lcd_buffers[1]);
#endif
                if (fb_ret == ESP_OK && esp_lv_adapter_set_dummy_draw(self->display, true) == ESP_OK) {
                    self->dummy_enabled = true;
                    if (!self->stop_requested && esp_lv_adapter_pause(-1) == ESP_OK) {
                        self->lvgl_paused = true;
                    } else {
                        esp_lv_adapter_set_dummy_draw(self->display, false);
                        self->dummy_enabled = false;
                    }
                }
            }
        }
        xSemaphoreGive(self->handoff_done);
    }

    esp_err_t VideoPlayer::initJpegDecoder()
    {
        if (jpeg_handle != nullptr)
            return ESP_OK;

        if (hw_jpeg_handle == nullptr) {
            const jpeg_decode_engine_cfg_t engine_config = {
                .intr_priority = 0,
                .timeout_ms = 200,
            };
            if (jpeg_new_decoder_engine(&engine_config, &hw_jpeg_handle) != ESP_OK) {
                ESP_LOGW(ESP_UTILS_LOG_TAG, "Hardware JPEG decoder unavailable; decoding in software");
                hw_jpeg_handle = nullptr;
            }
        }

        jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
        config.output_type = VIDEO_JPEG_PIXEL_FORMAT;

        jpeg_error_t err = jpeg_dec_open(&config, &jpeg_handle);
        return (err == JPEG_ERR_OK) ? ESP_OK : ESP_FAIL;
    }

    void VideoPlayer::deinitJpegDecoder()
    {
        if (jpeg_handle)
        {
            jpeg_dec_close(jpeg_handle);
            jpeg_handle = nullptr;
        }
        if (hw_jpeg_handle)
        {
            jpeg_del_decoder_engine(hw_jpeg_handle);
            hw_jpeg_handle = nullptr;
        }
        if (hw_jpeg_buffer)
        {
            heap_caps_free(hw_jpeg_buffer);
            hw_jpeg_buffer = nullptr;
            hw_jpeg_buffer_size = 0;
        }
    }

    esp_err_t VideoPlayer::ensureJpegOutputBuffer(int out_len)
    {
        if (out_len <= 0) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (jpeg_output_buffer && jpeg_output_buffer_size >= out_len) {
            return ESP_OK;
        }

        releaseJpegOutputBuffer();
        size_t alloc_size = ALIGN_UP(static_cast<size_t>(out_len), 16);
        jpeg_output_buffer = static_cast<uint8_t *>(
            heap_caps_aligned_calloc(16, 1, alloc_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!jpeg_output_buffer) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "JPEG output buffer allocation failed: %u bytes", static_cast<unsigned>(alloc_size));
            return ESP_ERR_NO_MEM;
        }

        jpeg_output_buffer_size = static_cast<int>(alloc_size);
        return ESP_OK;
    }

    void VideoPlayer::releaseJpegOutputBuffer()
    {
        if (jpeg_output_buffer) {
            heap_caps_free(jpeg_output_buffer);
            jpeg_output_buffer = nullptr;
            jpeg_output_buffer_size = 0;
        }
    }

    /* The hardware decoder only takes sizes in whole 8 px blocks. The coded data always covers
     * whole MCUs, so declaring the MCU-aligned size in the frame header decodes the same picture
     * plus a margin on the right and bottom, which the PPA copy leaves out. */
    static bool alignFrameSize(uint8_t *data, size_t len, uint16_t width, uint16_t height)
    {
        size_t pos = 2;
        while (pos + 9 <= len && data[pos] == 0xFF) {
            const uint8_t marker = data[pos + 1];
            if (marker == 0xFF) {
                pos++;
                continue;
            }
            if (marker == 0xDA || marker == 0xD9) {
                return false;   // Scan data reached without a frame header
            }
            const size_t seg = (data[pos + 2] << 8) | data[pos + 3];
            if (marker >= 0xC0 && marker <= 0xC2) {
                data[pos + 5] = height >> 8;
                data[pos + 6] = height & 0xFF;
                data[pos + 7] = width >> 8;
                data[pos + 8] = width & 0xFF;
                return true;
            }
            pos += 2 + seg;
        }
        return false;
    }

    bool VideoPlayer::decodeFrameHardware(uint8_t *data, size_t len, uint16_t *width, uint16_t *height,
                                          uint16_t *stride, uint16_t *rows)
    {
        if (!hw_jpeg_handle || VIDEO_BYTES_PER_PIXEL != 2) {
            return false;
        }
        jpeg_decode_picture_info_t info = {};
        if (jpeg_decoder_get_info(data, len, &info) != ESP_OK || info.width == 0 || info.height == 0) {
            return false;
        }
        // The decoder writes whole MCUs: 16x16 for 4:2:0, 16x8 for 4:2:2, 8x8 otherwise
        const uint32_t mcu_w = (info.sample_method == JPEG_DOWN_SAMPLING_YUV420 ||
                                info.sample_method == JPEG_DOWN_SAMPLING_YUV422) ? 16 : 8;
        const uint32_t mcu_h = (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) ? 16 : 8;
        const uint32_t out_w = ALIGN_UP(info.width, mcu_w);
        const uint32_t out_h = ALIGN_UP(info.height, mcu_h);
        const bool resized = info.width % 8 || info.height % 8;
        if (resized && !alignFrameSize(data, len, out_w, out_h)) {
            return false;
        }
        const size_t need = ALIGN_UP(out_w * out_h * VIDEO_BYTES_PER_PIXEL, data_cache_line_size);
        if (hw_jpeg_buffer_size < need) {
            heap_caps_free(hw_jpeg_buffer);
            hw_jpeg_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(data_cache_line_size, need,
                                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            hw_jpeg_buffer_size = hw_jpeg_buffer ? need : 0;
        }
        const jpeg_decode_cfg_t config = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
        };
        uint32_t out_size = 0;
        if (!hw_jpeg_buffer ||
                jpeg_decoder_process(hw_jpeg_handle, &config, data, len, hw_jpeg_buffer, hw_jpeg_buffer_size, &out_size) !=
                ESP_OK) {
            if (resized) {
                alignFrameSize(data, len, info.width, info.height);  // The software decoder gets the real size
            }
            return false;
        }
        *width = info.width;
        *height = info.height;
        *stride = out_w;
        *rows = out_h;
        return true;
    }

    void VideoPlayer::renderFrame(uint8_t *data, size_t len)
    {
        int next_buf_idx = (current_buf_idx + 1) % BOARD_LCD_DPI_BUFFER_NUMS;

        if (initJpegDecoder() != ESP_OK || !lcd_buffers[next_buf_idx] || !dummy_enabled || !ppa_srm_handle)
            return;

        // Decode each MJPEG frame into a PSRAM buffer, then use PPA SRM to scale it
        // into the next LCD frame buffer without involving LVGL widgets. The hardware
        // decoder leaves the CPU free for audio; the software one handles what it rejects.
        uint16_t frame_w = 0;
        uint16_t frame_h = 0;
        uint16_t frame_stride = 0;
        uint16_t frame_rows = 0;
        const uint8_t *frame_pixels = nullptr;
        if (decodeFrameHardware(data, len, &frame_w, &frame_h, &frame_stride, &frame_rows)) {
            frame_pixels = hw_jpeg_buffer;
        } else {
            jpeg_dec_io_t io = {
                .inbuf = data,
                .inbuf_len = static_cast<int>(len),
                .outbuf = nullptr,
            };

            jpeg_dec_header_info_t info;
            if (jpeg_dec_parse_header(jpeg_handle, &io, &info) != JPEG_ERR_OK) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "JPEG header parse failed");
                return;
            }

            if (info.width == 0 || info.height == 0) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "Invalid JPEG frame size: %dx%d", info.width, info.height);
                return;
            }

            int out_len = 0;
            if (jpeg_dec_get_outbuf_len(jpeg_handle, &out_len) != JPEG_ERR_OK || ensureJpegOutputBuffer(out_len) != ESP_OK) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "Prepare JPEG output buffer failed");
                return;
            }

            io.outbuf = jpeg_output_buffer;
            if (jpeg_dec_process(jpeg_handle, &io) != JPEG_ERR_OK) {
                ESP_LOGE(ESP_UTILS_LOG_TAG, "JPEG decode process failed");
                return;
            }
            frame_w = frame_stride = info.width;
            frame_h = frame_rows = info.height;
            frame_pixels = jpeg_output_buffer;
        }

        uint32_t target_w = DISPLAY_WIDTH;
        uint32_t target_h = (static_cast<uint64_t>(frame_h) * DISPLAY_WIDTH) / frame_w;
        if (target_h > DISPLAY_HEIGHT) {
            target_h = DISPLAY_HEIGHT;
            target_w = (static_cast<uint64_t>(frame_w) * DISPLAY_HEIGHT) / frame_h;
        }
        if (target_w == 0) {
            target_w = 1;
        }
        if (target_h == 0) {
            target_h = 1;
        }

        uint32_t offset_x = (DISPLAY_WIDTH - target_w) / 2;
        uint32_t offset_y = (DISPLAY_HEIGHT - target_h) / 2;
        float scale_x = static_cast<float>(target_w) / static_cast<float>(frame_w);
        float scale_y = static_cast<float>(target_h) / static_cast<float>(frame_h);

        if (last_video_width != frame_w || last_video_height != frame_h) {
            ESP_LOGI(ESP_UTILS_LOG_TAG, "Scale video frame: %dx%d -> %ux%u, offset %u,%u",
                     frame_w, frame_h, target_w, target_h, offset_x, offset_y);
            last_video_width = frame_w;
            last_video_height = frame_h;
        }

        // Black bars only where the picture does not reach
        if (target_w < DISPLAY_WIDTH || target_h < DISPLAY_HEIGHT) {
            memset(lcd_buffers[next_buf_idx], 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * VIDEO_BYTES_PER_PIXEL);
        }

        ppa_srm_oper_config_t srm_config = {};
        srm_config.in.buffer = frame_pixels;
        srm_config.in.pic_w = frame_stride;
        srm_config.in.pic_h = frame_rows;
        srm_config.in.block_w = frame_w;
        srm_config.in.block_h = frame_h;
        srm_config.in.block_offset_x = 0;
        srm_config.in.block_offset_y = 0;
        srm_config.in.srm_cm = VIDEO_PPA_COLOR_MODE;

        srm_config.out.buffer = lcd_buffers[next_buf_idx];
        srm_config.out.buffer_size = ALIGN_UP(DISPLAY_WIDTH * DISPLAY_HEIGHT * VIDEO_BYTES_PER_PIXEL, data_cache_line_size);
        srm_config.out.pic_w = DISPLAY_WIDTH;
        srm_config.out.pic_h = DISPLAY_HEIGHT;
        srm_config.out.block_offset_x = offset_x;
        srm_config.out.block_offset_y = offset_y;
        srm_config.out.srm_cm = VIDEO_PPA_COLOR_MODE;

        srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        srm_config.scale_x = scale_x;
        srm_config.scale_y = scale_y;
        srm_config.mirror_x = 0;
        srm_config.mirror_y = 0;
        srm_config.rgb_swap = 0;
        srm_config.byte_swap = 0;
        srm_config.mode = PPA_TRANS_MODE_BLOCKING;

        esp_err_t ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_config);
        if (ret != ESP_OK) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "PPA scale failed: %s", esp_err_to_name(ret));
            return;
        }

        ret = esp_lv_adapter_dummy_draw_blit(
            display,
            0,
            0,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            lcd_buffers[next_buf_idx],
            true);
        if (ret != ESP_OK) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Dummy draw blit failed: %s", esp_err_to_name(ret));
            return;
        }
        current_buf_idx = next_buf_idx;
    }

    void VideoPlayer::setClock(int64_t media_ms)
    {
        portENTER_CRITICAL(&clock_lock);
        clock_media_ms = media_ms;
        clock_stamp_us = esp_timer_get_time();
        portEXIT_CRITICAL(&clock_lock);
    }

    int64_t VideoPlayer::clockMs()
    {
        portENTER_CRITICAL(&clock_lock);
        const int64_t ms = clock_media_ms + (esp_timer_get_time() - clock_stamp_us) / 1000;
        portEXIT_CRITICAL(&clock_lock);
        return ms;
    }

    void VideoPlayer::acquireAudio()
    {
        if (!audio_client || audio_preempted || audio_client_is_owner(audio_client)) {
            return;
        }
        // Music or Bluetooth playback pauses through its own preempt callback
        if (audio_client_acquire(audio_client) != ESP_OK) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "Take audio failed; playing without sound");
        }
    }

    void VideoPlayer::releaseAudio()
    {
        if (audio_client) {
            audio_client_release(audio_client);
        }
    }

    // Runs in the task of the app taking audio; the picture keeps playing without sound
    void VideoPlayer::onAudioPreempt(audio_client_handle_t client, void *user_ctx)
    {
        (void)client;
        static_cast<VideoPlayer *>(user_ctx)->audio_preempted = true;
    }

    void VideoPlayer::knobCallback(int steps, void *user_ctx)
    {
        (void)user_ctx;
        audio_service_set_volume(audio_service_get_volume() + steps * KNOB_VOLUME_STEP);
    }

    void VideoPlayer::startAudioTask(media_file_t *file, const media_reader_t *video, uint32_t start_ms)
    {
        if (audio_task_handle || !audio_client_is_owner(audio_client)) {
            return;
        }
        audio_ring = xRingbufferCreateWithCaps(AUDIO_QUEUE_BYTES, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
        if (!audio_ring) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "Audio queue unavailable; playing without sound");
            return;
        }
        audio_file = file;
        audio_peer = video;
        audio_start_ms = start_ms;
        audio_rate = 0;
        audio_channels = 0;
        audio_base_ms = -1;
        audio_reader_done = false;
        audio_started = false;
        audio_task_run = true;
        xSemaphoreTake(audio_task_done, 0);
        xSemaphoreTake(audio_task_done, 0);
        if (xTaskCreatePinnedToCore(audioTask, "video_audio", 6144, this, 6, &audio_task_handle, 1) != pdPASS) {
            audio_task_handle = nullptr;
        } else if (xTaskCreatePinnedToCore(audioOutTask, "video_audio_out", 3072, this, 8, &audio_out_task_handle, 1) !=
                   pdPASS) {
            audio_out_task_handle = nullptr;
            audio_task_run = false;
        }
        if (!audio_task_handle || !audio_out_task_handle) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "Audio tasks unavailable; playing without sound");
            stopAudioTask();
            audio_started = true;
        }
    }

    void VideoPlayer::stopAudioTask()
    {
        audio_task_run = false;
        if (audio_task_handle) {
            xSemaphoreTake(audio_task_done, pdMS_TO_TICKS(2000));
            audio_task_handle = nullptr;
        }
        if (audio_out_task_handle) {
            xSemaphoreTake(audio_task_done, pdMS_TO_TICKS(2000));
            audio_out_task_handle = nullptr;
        }
        if (audio_ring) {
            vRingbufferDeleteWithCaps(audio_ring);
            audio_ring = nullptr;
        }
    }

    /* Reads its own copy of the audio stream, decodes it and queues the PCM */
    void VideoPlayer::audioTask(void *arg)
    {
        VideoPlayer *self = static_cast<VideoPlayer *>(arg);
        media_reader_t *reader = nullptr;
        audio_track_t *track = nullptr;
        esp_err_t ret = media_reader_open(self->audio_file, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, AUDIO_POOL_BYTES, &reader);
        if (ret == ESP_OK) {
            ret = audio_track_open(&media_reader_info(reader)->audio_info, &track);
            if (ret == ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGW(ESP_UTILS_LOG_TAG, "Audio format not supported; playing without sound");
            }
        }
        if (ret == ESP_OK && self->audio_start_ms) {
            media_reader_seek(reader, self->audio_start_ms);
        }

        // Milliseconds of sound waiting in the queue
        auto queued_ms = [self]() -> uint32_t {
            const uint32_t bytes_per_s = self->audio_rate * self->audio_channels * sizeof(int16_t);
            const size_t queued = AUDIO_QUEUE_BYTES - xRingbufferGetCurFreeSize(self->audio_ring);
            return bytes_per_s ? static_cast<uint32_t>(queued * 1000ULL / bytes_per_s) : 0;
        };
        esp_extractor_frame_info_t frame = {};
        while (ret == ESP_OK && self->audio_task_run && !self->audio_preempted) {
            // Sound is queued ahead of the picture, so this reader leads the video one through the
            // file. Past the shared cache that would read the card twice, so wait for the video
            // reader there, unless the queue runs low.
            while (self->audio_task_run && media_reader_too_far_ahead(reader, self->audio_peer) &&
                    queued_ms() > AUDIO_MIN_QUEUED_MS) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (!self->audio_task_run || media_reader_read(reader, &frame) != ESP_OK) {
                break;
            }
            int16_t *pcm = nullptr;
            uint32_t bytes = 0;
            if (audio_track_decode(track, frame.frame_buffer, frame.frame_size, &pcm, &bytes) == ESP_OK && bytes > 0) {
                uint32_t rate = 0;
                uint8_t channels = 0;
                audio_track_format(track, &rate, &channels);
                if (rate != self->audio_rate || channels != self->audio_channels) {
                    // Only at the start of a file in practice, before anything is queued
                    if (audio_client_set_format(self->audio_client, rate, 16, channels) != ESP_OK) {
                        ret = ESP_FAIL;     // Not the owner any more
                    } else {
                        ESP_LOGI(ESP_UTILS_LOG_TAG, "Audio track: %lu Hz, %u ch", (unsigned long)rate, channels);
                        self->audio_rate = rate;
                        self->audio_channels = channels;
                    }
                }
                if (ret == ESP_OK) {
                    if (self->audio_base_ms < 0) {
                        self->audio_base_ms = frame.pts;
                    }
                    if (channels == 2) {
                        // The speaker is mono, so both channels carry the mix
                        for (uint32_t i = 0; i + 1 < bytes / sizeof(int16_t); i += 2) {
                            const int16_t mixed = static_cast<int16_t>((static_cast<int32_t>(pcm[i]) + pcm[i + 1]) / 2);
                            pcm[i] = mixed;
                            pcm[i + 1] = mixed;
                        }
                    }
                    // Waits while the queue is full, which keeps this reader close to the video one
                    while (self->audio_task_run && xRingbufferSend(self->audio_ring, pcm, bytes, pdMS_TO_TICKS(20)) != pdTRUE) {
                    }
                }
            }
            media_reader_release(reader, &frame);
        }

        audio_track_close(track);
        media_reader_close(reader);
        self->audio_reader_done = true;
        xSemaphoreGive(self->audio_task_done);
        vTaskDelete(nullptr);
    }

    /* Feeds the codec, which paces it. Every write moves the playback clock to what is heard. */
    void VideoPlayer::audioOutTask(void *arg)
    {
        VideoPlayer *self = static_cast<VideoPlayer *>(arg);
        uint64_t frames_written = 0;
        bool writing = true;
        while (self->audio_task_run) {
            size_t size = 0;
            void *data = xRingbufferReceiveUpTo(self->audio_ring, &size, pdMS_TO_TICKS(20), AUDIO_WRITE_BYTES);
            if (!data) {
                if (self->audio_reader_done) {
                    break;  // All sound played
                }
                continue;
            }
            const uint32_t rate = self->audio_rate;
            const uint8_t channels = self->audio_channels;
            // A failure means another app took audio; the rest is dropped
            if (writing && rate && channels && audio_client_write(self->audio_client, data, size) == ESP_OK) {
                frames_written += size / (sizeof(int16_t) * channels);
                self->setClock(self->audio_base_ms + static_cast<int64_t>(frames_written * 1000 / rate) - AUDIO_LATENCY_MS);
                self->audio_started = true;
            } else {
                writing = false;
            }
            vRingbufferReturnItem(self->audio_ring, data);
        }
        self->audio_started = true;     // Without sound the picture goes on by itself
        xSemaphoreGive(self->audio_task_done);
        vTaskDelete(nullptr);
    }

    bool VideoPlayer::shouldExitBySwipe()
    {
        if (!touch_handle) {
            return false;
        }

        esp_lcd_touch_read_data(touch_handle);

        esp_lcd_touch_point_data_t points[1] = {};
        uint8_t point_num = 0;
        esp_err_t ret = esp_lcd_touch_get_data(touch_handle, points, &point_num, 1);

        if (ret != ESP_OK || point_num == 0) {
            touch_active = false;
            return false;
        }

        if (!touch_active) {
            touch_active = true;
            touch_start_x = points[0].x;
            touch_start_y = points[0].y;
            return false;
        }

        int dx = abs((int)points[0].x - (int)touch_start_x);
        int dy = abs((int)points[0].y - (int)touch_start_y);
        return dx > SWIPE_EXIT_THRESHOLD || dy > SWIPE_EXIT_THRESHOLD;
    }

    bool VideoPlayer::shouldStop(PlayResult *result, bool *request_close)
    {
        if (shouldExitBySwipe()) {
            *request_close = true;
            loop_playback = false;
        }
        if (!loop_playback || is_paused) {
            *result = PlayResult::STOPPED;
            return true;
        }
        return false;
    }

    VideoPlayer::PlayResult VideoPlayer::playFile(const std::string &path, uint32_t start_ms, bool *request_close)
    {
        media_file_t *file = nullptr;
        media_reader_t *video = nullptr;
        if (media_file_open(path.c_str(), &file) != ESP_OK ||
                media_reader_open(file, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, VIDEO_POOL_BYTES, &video) != ESP_OK) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "Can't open video in %s", path.c_str());
            media_file_close(file);
            return PlayResult::FAILED;
        }
        const esp_extractor_stream_info_t *info = media_reader_info(video);
        if (info->video_info.format != ESP_EXTRACTOR_VIDEO_FORMAT_MJPEG) {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "%s: only MJPEG video plays here; convert it first", path.c_str());
            media_reader_close(video);
            media_file_close(file);
            return PlayResult::FAILED;
        }
        if (start_ms && media_reader_seek(video, start_ms) != ESP_OK) {
            start_ms = 0;
        }
        const uint32_t fps = info->video_info.fps ? info->video_info.fps : 25;
        const int64_t frame_ms = 1000 / fps;
        ESP_LOGI(ESP_UTILS_LOG_TAG, "Playing %s: %ux%u, %lu fps, %lu s, from %lu s", path.c_str(), info->video_info.width,
                 info->video_info.height, (unsigned long)fps, (unsigned long)(info->duration / 1000),
                 (unsigned long)(start_ms / 1000));

        acquireAudio();
        setClock(start_ms);
        audio_started = true;   // Stays so unless the audio task starts below
        startAudioTask(file, video, start_ms);

        PlayResult result = PlayResult::DONE;
        // Picture and sound start together; a file's audio reader can take a while to index
        const int64_t wait_until = esp_timer_get_time() + AUDIO_START_TIMEOUT_MS * 1000LL;
        while (!audio_started && esp_timer_get_time() < wait_until && !shouldStop(&result, request_close)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!audio_started) {
            setClock(start_ms);
        }
        uint32_t shown_pts = start_ms;
        int drops_in_row = 0;
        uint32_t dropped = 0;
        uint32_t jumps = 0;
        esp_extractor_frame_info_t frame = {};
        while (!shouldStop(&result, request_close)) {
            const esp_err_t ret = media_reader_read(video, &frame);
            if (ret != ESP_OK) {
                result = (ret == ESP_ERR_NOT_FOUND) ? PlayResult::DONE : PlayResult::FAILED;
                break;
            }
            const int64_t pts = frame.pts;
            if (clockMs() - pts > CATCH_UP_MS) {
                media_reader_release(video, &frame);
                jumps++;
                if (media_reader_seek(video, static_cast<uint32_t>(clockMs() + CATCH_UP_MS / 4)) != ESP_OK) {
                    result = PlayResult::DONE;  // Past the end
                    break;
                }
                continue;
            }
            // Behind the sound: skip frames to catch up, but keep the picture moving
            if (clockMs() - pts > LATE_FRAMES * frame_ms && drops_in_row < MAX_DROPS_IN_ROW) {
                drops_in_row++;
                dropped++;
                media_reader_release(video, &frame);
                continue;
            }
            bool stop = false;
            for (int64_t wait = pts - clockMs(); wait > 1; wait = pts - clockMs()) {
                vTaskDelay(pdMS_TO_TICKS(std::min<int64_t>(wait, MAX_WAIT_MS)));
                if (shouldStop(&result, request_close)) {
                    stop = true;
                    break;
                }
            }
            if (!stop) {
                renderFrame(frame.frame_buffer, frame.frame_size);
                shown_pts = static_cast<uint32_t>(pts);
                drops_in_row = 0;
            }
            media_reader_release(video, &frame);
            if (stop) {
                break;
            }
        }

        stopAudioTask();
        media_reader_close(video);
        media_file_close(file);
        resume_ms = (result == PlayResult::STOPPED) ? shown_pts : 0;
        if (dropped || jumps) {
            ESP_LOGI(ESP_UTILS_LOG_TAG, "Skipped %lu late frames, jumped ahead %lu times", (unsigned long)dropped,
                     (unsigned long)jumps);
        }
        return result;
    }

    void VideoPlayer::playTask(void *arg)
    {
        VideoPlayer *self = static_cast<VideoPlayer *>(arg);
        bool request_close = false;

        if (self->initDisplayBypass() != ESP_OK)
        {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "initDisplayBypass failed");
            self->deinitDisplayBypass();
            self->loop_playback = false;
        }

        // Plays every video in the folder in turn until the app closes; a paused app picks up
        // where it stopped.
        int failures_in_row = 0;
        while (self->loop_playback && !self->files.empty() && failures_in_row < static_cast<int>(self->files.size()))
        {
            if (self->is_paused)
            {
                // In the background the codec is free for other apps
                self->releaseAudio();
                while (self->is_paused && self->loop_playback)
                {
                    if (self->shouldExitBySwipe()) {
                        request_close = true;
                        self->loop_playback = false;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                continue;
            }

            const PlayResult result = self->playFile(self->files[self->file_index], self->resume_ms, &request_close);
            if (result != PlayResult::STOPPED)
            {
                failures_in_row = (result == PlayResult::FAILED) ? failures_in_row + 1 : 0;
                self->file_index = (self->file_index + 1) % self->files.size();
                self->resume_ms = 0;
            }
        }
        if (failures_in_row > 0 && self->loop_playback)
        {
            ESP_LOGW(ESP_UTILS_LOG_TAG, "No playable video in /videos");
        }

        self->releaseAudio();
        if (!self->stop_requested) {
            self->deinitDisplayBypass();
            self->exit_requested = request_close;
        }
        xSemaphoreGive(self->play_task_done);

        vTaskDelete(NULL);
    }

} // namespace esp_brookesia::apps
