/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:UsbScreen"
#include "esp_lib_utils.h"
#include "esp_cache.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "app_fonts.h"
#include "audio_service.h"
#include "knob_service.h"
#include "UsbScreen.hpp"

#define APP_NAME "USB Screen"

LV_IMG_DECLARE(img_app_usbscreen);

namespace esp_brookesia::apps {

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

static constexpr int SCREEN = BOARD_LCD_H_RES;
static constexpr int BYTES_PER_PIXEL = 2;
static constexpr int JPEG_MCU_MAX = 16;
#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
static constexpr size_t CACHE_LINE = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
#else
static constexpr size_t CACHE_LINE = 64;
#endif
static constexpr uint32_t REFRESH_MS = 500;
static constexpr uint32_t IDLE_WAIT_MS = 100;
static constexpr uint32_t TOUCH_PERIOD_MS = 10;
static constexpr uint32_t DISPLAY_LOCK_WAIT_MS = 20;
/* The PC only sends a frame when the picture changes, so the last one stays until it leaves;
 * frames that keep failing bring the help back so the problem shows */
static constexpr uint32_t MAX_FAILURES = 10;
/* A touch starting this close to the bottom edge and moving this far up leaves the app */
static constexpr int EXIT_EDGE = 40;
static constexpr int EXIT_DISTANCE = 110;
static constexpr int KNOB_VOLUME_STEP = 5;
static constexpr uint32_t WORKER_STACK = 6 * 1024;
static constexpr UBaseType_t WORKER_PRIORITY = 6;
static constexpr uint32_t ACCENT_COLOR = 0x37D2C4;
static constexpr uint32_t HINT_COLOR = 0xA0A3B1;

UsbScreen *UsbScreen::_instance = nullptr;

UsbScreen *UsbScreen::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new UsbScreen(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

UsbScreen::UsbScreen(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_usbscreen, true, use_status_bar, use_navigation_bar)
{
    _worker_done = xSemaphoreCreateBinary();
    _handoff_done = xSemaphoreCreateBinary();
    xSemaphoreGive(_worker_done);
}

UsbScreen::~UsbScreen()
{
}

bool UsbScreen::run(void)
{
    ESP_UTILS_LOGD("Run");
    buildUi();
    startUsb();
    refresh();
    _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
    knob_service_attach(knob_cb, this);
    return true;
}

bool UsbScreen::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool UsbScreen::close(void)
{
    ESP_UTILS_LOGD("Close");
    knob_service_detach(knob_cb, this);
    stopUsb();
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _status = _hint = nullptr;
    return true;
}

/* The port is only held while the app is on screen */
bool UsbScreen::pause(void)
{
    ESP_UTILS_LOGD("Pause");
    knob_service_detach(knob_cb, this);
    stopUsb();
    return true;
}

bool UsbScreen::resume(void)
{
    ESP_UTILS_LOGD("Resume");
    startUsb();
    refresh();
    knob_service_attach(knob_cb, this);
    return true;
}

void UsbScreen::buildUi()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_image_create(screen);
    lv_image_set_src(icon, &img_app_usbscreen);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -96);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(title, APP_NAME);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -14);

    _status = lv_label_create(screen);
    lv_obj_set_style_text_font(_status, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(_status, lv_color_hex(ACCENT_COLOR), LV_PART_MAIN);
    lv_label_set_text(_status, "");
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, 24);

    // Narrow enough for the lower half of the circle
    _hint = lv_label_create(screen);
    lv_obj_set_width(_hint, 320);
    lv_label_set_long_mode(_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(_hint, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_hint, lv_color_hex(HINT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(_hint, "");
    lv_obj_align(_hint, LV_ALIGN_TOP_MID, 0, SCREEN / 2 + 48);
}

void UsbScreen::refresh()
{
    if (!_status || !_hint) {
        return;
    }
    const char *status;
    const char *hint;
    if (_usb_busy) {
        status = "USB in use";
        hint = "The USB drive has the port. Turn it off in Settings > Storage, then open this again.";
    } else {
        switch (usb_display_get_status()) {
        case USB_DISPLAY_WAITING_HOST:
            status = "Waiting for a PC";
            hint = "Connect the USB-C port to a Windows 10 or 11 PC. Nothing? Flip the plug.";
            break;
        case USB_DISPLAY_CONNECTED:
        case USB_DISPLAY_STREAMING:
            status = "Connected";
            hint = "Install the ESP USB display driver once; Windows then shows a new display. Swipe up from "
                   "the bottom edge to leave.";
            break;
        case USB_DISPLAY_OFF:
        default:
            status = "Starting";
            hint = "";
            break;
        }
    }
    if (strcmp(lv_label_get_text(_status), status) != 0) {
        lv_label_set_text(_status, status);
    }
    if (strcmp(lv_label_get_text(_hint), hint) != 0) {
        lv_label_set_text(_hint, hint);
    }
}

void UsbScreen::startUsb()
{
    if (_worker) {
        return;
    }
    const esp_err_t ret = usb_display_start();
    _usb_busy = ret == ESP_ERR_INVALID_STATE;
    if (ret != ESP_OK) {
        ESP_UTILS_LOGW("Start USB screen failed: %s", esp_err_to_name(ret));
        return;
    }
    // A worker that requested the close itself may still be returning to its task loop.
    xSemaphoreTake(_worker_done, portMAX_DELAY);
    xSemaphoreTake(_handoff_done, 0);
    _stop = false;
    _exit_requested = false;
    _shown = _failures = 0;
    _last_error = nullptr;
    if (xTaskCreatePinnedToCore(workerTask, "usb_screen", WORKER_STACK, this, WORKER_PRIORITY, &_worker, 0) != pdPASS) {
        _worker = nullptr;
        xSemaphoreGive(_worker_done);
        usb_display_stop();
        ESP_UTILS_LOGE("Create worker failed");
    }
}

void UsbScreen::stopUsb()
{
    _stop = true;
    usb_display_stop();
    if (_worker) {
        // The worker only takes the LVGL lock with a timeout and never waits for it after stop.
        xSemaphoreTake(_worker_done, portMAX_DELAY);
        xSemaphoreGive(_worker_done);
        _worker = nullptr;
    }
    lv_async_call_cancel(enterBypass_cb, this);
    _handoff_pending = false;
    // close() and pause() hold the LVGL lock, so the display is returned only after the worker has stopped.
    if (_dummy_draw) {
        esp_lv_adapter_set_dummy_draw(_display, false);
        _dummy_draw = false;
    }
    if (_lvgl_paused) {
        esp_lv_adapter_resume();
        _lvgl_paused = false;
    }
    _bypass = false;
}

void UsbScreen::workerTask(void *arg)
{
    UsbScreen *self = static_cast<UsbScreen *>(arg);
    self->workerLoop();
    if (!self->_stop) {
        self->leaveBypass();
        if (!self->_stop) {
            self->_exit_requested = true;
        }
    }
    xSemaphoreGive(self->_worker_done);
    vTaskDelete(nullptr);
}

/* Returns when the app stops the worker, or after the swipe that leaves */
void UsbScreen::workerLoop()
{
    while (!_stop) {
        usb_display_frame_t *frame = usb_display_get_frame(_bypass ? TOUCH_PERIOD_MS : IDLE_WAIT_MS);
        const int64_t now = esp_timer_get_time();
        if (frame) {
            // Prepared before taking the display, so a stream that cannot be shown leaves the help up
            Picture picture;
            const char *error = prepareDecoder() ? preparePicture(frame, &picture) : "no decoder";
            if (!error && !_bypass && !enterBypass()) {
                error = "no display";
            }
            if (!error) {
                error = drawPicture(frame, picture);
            }
            // Uncompressed pictures are copied straight out of the frame
            usb_display_return_frame(frame);
            if (error) {
                _last_error = error;
                if (++_failures >= MAX_FAILURES && _bypass) {
                    ESP_UTILS_LOGW("PC frames keep failing: %s", error);
                    leaveBypass();
                }
            } else {
                _failures = 0;
                _shown++;
            }
        }
        if (!_bypass) {
            continue;
        }
        if (now - _last_touch_us >= TOUCH_PERIOD_MS * 1000) {
            _last_touch_us = now;
            if (pollTouch()) {
                return;
            }
        }
        if (usb_display_get_status() == USB_DISPLAY_WAITING_HOST) {
            ESP_UTILS_LOGI("PC left");
            leaveBypass();
        }
    }
}

bool UsbScreen::prepareDecoder()
{
    if (!_jpeg) {
        const jpeg_decode_engine_cfg_t engine_cfg = {
            .intr_priority = 0,
            .timeout_ms = 100,
        };
        ESP_UTILS_CHECK_FALSE_RETURN(jpeg_new_decoder_engine(&engine_cfg, &_jpeg) == ESP_OK, false,
                                     "Create JPEG decoder failed");
    }
    if (!_decoded) {
        // The decoder pads rows and columns to whole MCUs
        const jpeg_decode_memory_alloc_cfg_t mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        const size_t size = ALIGN_UP(ALIGN_UP(SCREEN, JPEG_MCU_MAX) * ALIGN_UP(SCREEN, JPEG_MCU_MAX) * BYTES_PER_PIXEL,
                                     CACHE_LINE);
        _decoded = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(size, &mem_cfg, &_decoded_size));
        ESP_UTILS_CHECK_NULL_RETURN(_decoded, false, "No memory for decoded frames");
    }
    if (!_ppa) {
        const ppa_client_config_t ppa_cfg = {
            .oper_type = PPA_OPERATION_SRM,
        };
        ESP_UTILS_CHECK_FALSE_RETURN(ppa_register_client(&ppa_cfg, &_ppa) == ESP_OK, false, "Register PPA failed");
    }
    return true;
}

/* Keep a close/pause callback from waiting on a worker that is blocked behind its LVGL lock. */
bool UsbScreen::takeDisplayLock()
{
    while (!_stop) {
        if (board_display_lock(DISPLAY_LOCK_WAIT_MS)) {
            return true;
        }
    }
    return false;
}

bool UsbScreen::enterBypass()
{
    if (!takeDisplayLock()) {
        return false;
    }
    if (_stop) {
        board_display_unlock();
        return false;
    }
    xSemaphoreTake(_handoff_done, 0);
    _handoff_pending = true;
    const lv_result_t queued = lv_async_call(enterBypass_cb, this);
    board_display_unlock();
    if (queued != LV_RESULT_OK) {
        _handoff_pending = false;
        ESP_UTILS_LOGE("Queue display handoff failed");
        return false;
    }
    // The callback runs on the LVGL worker, where pause() acknowledges without waiting.
    while (!_stop) {
        if (xSemaphoreTake(_handoff_done, pdMS_TO_TICKS(DISPLAY_LOCK_WAIT_MS)) == pdTRUE) {
            return _bypass;
        }
    }
    return false;
}

void UsbScreen::enterBypass_cb(void *arg)
{
    UsbScreen *self = static_cast<UsbScreen *>(arg);
    self->_handoff_pending = false;
    if (!self->_stop) {
        self->_display = lv_display_get_default();
        self->_panel = board_display_get_panel_handle();
        self->_touch = board_display_get_touch_handle();
        if (self->_display && self->_panel && self->_touch) {
#if BOARD_LCD_DPI_BUFFER_NUMS >= 3
            const esp_err_t fb_ret = esp_lcd_dpi_panel_get_frame_buffer(self->_panel, 3, &self->_fbs[0],
                                                                          &self->_fbs[1], &self->_fbs[2]);
#else
            const esp_err_t fb_ret = esp_lcd_dpi_panel_get_frame_buffer(self->_panel, 2, &self->_fbs[0],
                                                                          &self->_fbs[1]);
#endif
            if (fb_ret == ESP_OK && esp_lv_adapter_set_dummy_draw(self->_display, true) == ESP_OK) {
                self->_dummy_draw = true;
                if (!self->_stop && esp_lv_adapter_pause(-1) == ESP_OK) {
                    self->_lvgl_paused = true;
                    self->_bypass = true;
                    self->_fb_valid = false;
                    self->_touching = self->_edge_touch = false;
                    self->_last_touch_us = 0;
                    ESP_UTILS_LOGI("Showing the PC screen");
                } else {
                    esp_lv_adapter_set_dummy_draw(self->_display, false);
                    self->_dummy_draw = false;
                }
            }
        }
    }
    xSemaphoreGive(self->_handoff_done);
}

void UsbScreen::leaveBypass()
{
    if (!_bypass) {
        return;
    }
    if (_touching) {
        usb_display_report_touch(nullptr, 0);
        _touching = false;
    }
    if (!takeDisplayLock()) {
        return;
    }
    esp_lv_adapter_set_dummy_draw(_display, false);
    _dummy_draw = false;
    esp_lv_adapter_resume();
    _lvgl_paused = false;
    board_display_unlock();
    _bypass = false;
}

/* The hardware decoder only takes sizes in whole 8 px blocks, and 466 is not. The coded data always covers
 * whole MCUs, so declaring the MCU-aligned size in the frame header decodes the same picture plus a margin on
 * the right and bottom, which the PPA copy leaves out. Same as in the video player. */
static bool align_frame_size(uint8_t *data, size_t len, uint16_t width, uint16_t height)
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

const char *UsbScreen::preparePicture(usb_display_frame_t *frame, Picture *picture)
{
    if (frame->format == USB_DISPLAY_FORMAT_RGB565 || frame->format == USB_DISPLAY_FORMAT_RGB888) {
        picture->buf = frame->data;
        picture->pic_w = picture->width = frame->width;
        picture->pic_h = picture->height = frame->height;
        picture->color = frame->format == USB_DISPLAY_FORMAT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 :
                         PPA_SRM_COLOR_MODE_RGB888;
        return nullptr;
    }

    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(frame->data, frame->len, &info) != ESP_OK) {
        return "not a JPEG";
    }
    uint32_t mcu_w;
    uint32_t mcu_h;
    switch (info.sample_method) {
    case JPEG_DOWN_SAMPLING_YUV444:
        mcu_w = 8;
        mcu_h = 8;
        break;
    case JPEG_DOWN_SAMPLING_YUV422:
        mcu_w = 16;
        mcu_h = 8;
        break;
    case JPEG_DOWN_SAMPLING_YUV420:
        mcu_w = 16;
        mcu_h = 16;
        break;
    default:
        return "JPEG sampling";
    }
    const uint32_t out_w = ALIGN_UP(info.width, mcu_w);
    const uint32_t out_h = ALIGN_UP(info.height, mcu_h);
    if (out_w * out_h * BYTES_PER_PIXEL > _decoded_size) {
        return "JPEG size";
    }
    if ((info.width % 8 || info.height % 8) && !align_frame_size(frame->data, frame->len, out_w, out_h)) {
        return "JPEG header";
    }
    // The decoder's own RGB output treats the JPEG as limited range and stretches the contrast. PC JPEGs are
    // full range, so take YUV and let the PPA convert it while copying; it reads YUV420 in even sizes only.
    const uint32_t width = std::min<uint32_t>(info.width, SCREEN - frame->x);
    const uint32_t height = std::min<uint32_t>(info.height, SCREEN - frame->y);
    const bool yuv = width % 2 == 0 && height % 2 == 0;
    const jpeg_decode_cfg_t decode_cfg = {
        .output_format = yuv ? JPEG_DECODE_OUT_FORMAT_YUV420 : JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_size = 0;
    if (jpeg_decoder_process(_jpeg, &decode_cfg, frame->data, frame->len, _decoded, _decoded_size, &out_size) !=
            ESP_OK) {
        return "JPEG decode";
    }
    picture->buf = _decoded;
    picture->pic_w = out_w;
    picture->pic_h = out_h;
    picture->width = info.width;
    picture->height = info.height;
    picture->color = yuv ? PPA_SRM_COLOR_MODE_YUV420 : PPA_SRM_COLOR_MODE_RGB565;
    return nullptr;
}

/* Puts the picture at the frame's place in the next frame buffer and shows it. A frame for part of the
 * panel goes on a copy of the picture shown now, as the PC only sends what changed. */
const char *UsbScreen::drawPicture(const usb_display_frame_t *frame, const Picture &picture)
{
    const uint32_t x = frame->x;
    const uint32_t y = frame->y;
    const uint32_t width = std::min<uint32_t>(picture.width, SCREEN - x);
    const uint32_t height = std::min<uint32_t>(picture.height, SCREEN - y);
    const bool whole = x == 0 && y == 0 && width == SCREEN && height == SCREEN;
    const int next = (_fb_index + 1) % BOARD_LCD_DPI_BUFFER_NUMS;
    const size_t fb_len = SCREEN * SCREEN * BYTES_PER_PIXEL;

    ppa_srm_oper_config_t srm = {};
    srm.out.buffer = _fbs[next];
    srm.out.buffer_size = ALIGN_UP(fb_len, CACHE_LINE);
    srm.out.pic_w = SCREEN;
    srm.out.pic_h = SCREEN;
    srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x = 1.0f;
    srm.scale_y = 1.0f;
    srm.mode = PPA_TRANS_MODE_BLOCKING;
    if (!whole) {
        if (_fb_valid) {
            srm.in.buffer = _fbs[_fb_index];
            srm.in.pic_w = SCREEN;
            srm.in.pic_h = SCREEN;
            srm.in.block_w = SCREEN;
            srm.in.block_h = SCREEN;
            srm.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
            if (ppa_do_scale_rotate_mirror(_ppa, &srm) != ESP_OK) {
                return "PPA copy";
            }
        } else {
            // Nothing of the PC's yet: the rest of the panel stays black until it arrives
            memset(_fbs[next], 0, fb_len);
            esp_cache_msync(_fbs[next], fb_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
    }
    srm.in.buffer = picture.buf;
    srm.in.pic_w = picture.pic_w;
    srm.in.pic_h = picture.pic_h;
    srm.in.block_w = width;
    srm.in.block_h = height;
    srm.in.srm_cm = picture.color;
    srm.in.yuv_range = PPA_COLOR_RANGE_FULL;
    srm.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    srm.out.block_offset_x = x;
    srm.out.block_offset_y = y;
    if (ppa_do_scale_rotate_mirror(_ppa, &srm) != ESP_OK) {
        return "PPA copy";
    }
    if (esp_lv_adapter_dummy_draw_blit(_display, 0, 0, SCREEN, SCREEN, _fbs[next], true) != ESP_OK) {
        return "panel blit";
    }
    _fb_index = next;
    _fb_valid = true;
    return nullptr;
}

/* Sends the fingers to the PC; returns true once the swipe from the bottom edge has gone far enough */
bool UsbScreen::pollTouch()
{
    esp_lcd_touch_point_data_t points[USB_DISPLAY_TOUCH_POINTS_MAX] = {};
    uint8_t count = 0;
    if (esp_lcd_touch_read_data(_touch) != ESP_OK ||
            esp_lcd_touch_get_data(_touch, points, &count, USB_DISPLAY_TOUCH_POINTS_MAX) != ESP_OK) {
        count = 0;
    }

    if (count == 0) {
        _edge_touch = false;
    } else if (!_touching && !_edge_touch && points[0].y >= SCREEN - EXIT_EDGE) {
        // Kept from the PC until it is clear whether this is the swipe that leaves
        _edge_touch = true;
        _edge_start_y = points[0].y;
    }
    if (_edge_touch) {
        return _edge_start_y - points[0].y >= EXIT_DISTANCE;
    }

    usb_display_touch_t touches[USB_DISPLAY_TOUCH_POINTS_MAX];
    for (uint8_t i = 0; i < count; i++) {
        touches[i] = {.id = points[i].track_id, .x = points[i].x, .y = points[i].y};
    }
    if ((count > 0 || _touching) && usb_display_report_touch(touches, count) == ESP_OK) {
        _touching = count > 0;
    }
    return false;
}

void UsbScreen::timer_cb(lv_timer_t *timer)
{
    UsbScreen *self = static_cast<UsbScreen *>(lv_timer_get_user_data(timer));
    if (self->_exit_requested) {
        self->_exit_requested = false;
        self->notifyCoreClosed();
        return;
    }
    self->refresh();
}

void UsbScreen::knob_cb(int steps, void *user_ctx)
{
    (void)user_ctx;
    audio_service_set_volume(audio_service_get_volume() + steps * KNOB_VOLUME_STEP);
}

} // namespace esp_brookesia::apps
