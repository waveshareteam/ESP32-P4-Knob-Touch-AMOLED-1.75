/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "board_app.h"
#include "usb_display.h"

namespace esp_brookesia::apps {

/**
 * @brief The board as an extra screen for a Windows PC, with touch and sound
 *
 * The app holds the USB port while it is open. Until the PC sends a picture it shows how to
 * connect; then a worker task draws the PC's frames straight to the panel with LVGL paused and
 * reports touches back, and a swipe up from the bottom edge leaves. The knob sets the volume.
 */
class UsbScreen: public systems::phone::App {
public:
    static UsbScreen *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);
    ~UsbScreen();

protected:
    UsbScreen(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static UsbScreen *_instance;

    lv_obj_t *_status = nullptr;
    lv_obj_t *_hint = nullptr;
    lv_timer_t *_timer = nullptr;
    bool _usb_busy = false;             // The port is taken, by the microSD drive

    TaskHandle_t _worker = nullptr;
    SemaphoreHandle_t _worker_done = nullptr;
    SemaphoreHandle_t _handoff_done = nullptr;
    volatile bool _stop = false;
    volatile bool _exit_requested = false;
    volatile bool _handoff_pending = false;

    // Owned by the worker
    bool _bypass = false;               // LVGL is paused and frames go straight to the panel
    bool _dummy_draw = false;
    bool _lvgl_paused = false;
    lv_display_t *_display = nullptr;
    esp_lcd_panel_handle_t _panel = nullptr;
    esp_lcd_touch_handle_t _touch = nullptr;
    void *_fbs[BOARD_LCD_DPI_BUFFER_NUMS] = {};
    int _fb_index = 0;
    bool _fb_valid = false;             // _fbs[_fb_index] holds the PC picture, so a partial update can go on it
    jpeg_decoder_handle_t _jpeg = nullptr;
    uint8_t *_decoded = nullptr;
    size_t _decoded_size = 0;
    ppa_client_handle_t _ppa = nullptr;
    int64_t _last_touch_us = 0;
    uint32_t _shown = 0;
    uint32_t _failures = 0;             // Frames in a row that could not be shown
    const char *_last_error = nullptr;
    bool _touching = false;
    bool _edge_touch = false;           // Started on the bottom edge: maybe the swipe that leaves
    uint16_t _edge_start_y = 0;

    void buildUi();
    void refresh();
    void startUsb();
    void stopUsb();

    void workerLoop();
    bool prepareDecoder();
    bool takeDisplayLock();
    bool enterBypass();
    void leaveBypass();
    /* Pixels of one frame, ready for the PPA: the picture is pic_w x pic_h, of which width x height
     * from the top left belong to the frame's rectangle */
    struct Picture {
        const void *buf = nullptr;
        uint32_t pic_w = 0;
        uint32_t pic_h = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        ppa_srm_color_mode_t color = PPA_SRM_COLOR_MODE_RGB565;
    };

    const char *preparePicture(usb_display_frame_t *frame, Picture *picture);
    const char *drawPicture(const usb_display_frame_t *frame, const Picture &picture);
    bool pollTouch();

    static void workerTask(void *arg);
    static void enterBypass_cb(void *arg);
    static void timer_cb(lv_timer_t *timer);
    static void knob_cb(int steps, void *user_ctx);
};

} // namespace esp_brookesia::apps
