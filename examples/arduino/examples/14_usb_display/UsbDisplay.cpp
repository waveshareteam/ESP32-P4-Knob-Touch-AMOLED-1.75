/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 *
 * Frame protocol, JPEG conversion, and touch-report layout are adapted from
 * the ESP-IDF 14_usb_display example in this repository.
 */

#include "UsbDisplay.h"

#include <cstring>

#include <Arduino.h>
#include <Wire.h>
#include <board_display.h>

extern "C" {
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tusb.h"
}

namespace {

constexpr uint16_t kWidth = KnobBoardDisplay::kWidth;
constexpr uint16_t kHeight = KnobBoardDisplay::kHeight;
constexpr uint8_t kTouchAddress = 0x5A;
constexpr size_t kFrameLimit = 300 * 1024;
constexpr size_t kMcuSize = 16;
constexpr size_t kCanvasBytes = kWidth * kHeight * sizeof(uint16_t);
constexpr size_t kUsbChunkBytes = 512;
constexpr uint8_t kUsbHsPort = 1;
constexpr uint8_t kTouchPoints = 5;

struct __attribute__((packed)) FrameHeader {
    uint16_t crc16;
    uint8_t type;
    uint8_t cmd;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t frameId : 10;
    uint32_t payloadTotal : 22;
};

struct __attribute__((packed)) TouchFinger {
    uint8_t tip;
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

struct __attribute__((packed)) TouchReport {
    TouchFinger fingers[kTouchPoints];
    uint8_t count;
};

constexpr uint8_t kFormatJpeg = 3;
constexpr uint8_t kReportTouch = 1;
constexpr uint8_t kReportMaxCount = 2;

KnobBoardDisplay s_display;
usb_phy_handle_t s_phy = nullptr;
jpeg_decoder_handle_t s_jpeg = nullptr;
ppa_client_handle_t s_ppa = nullptr;
uint8_t *s_frame = nullptr;
uint8_t *s_jpegOutput = nullptr;
uint16_t *s_frameBuffer = nullptr;
SemaphoreHandle_t s_frameReady = nullptr;
SemaphoreHandle_t s_frameAvailable = nullptr;
SemaphoreHandle_t s_touchLock = nullptr;
TaskHandle_t s_frameTask = nullptr;
uint8_t s_headerBytes[sizeof(FrameHeader)] = {};
size_t s_headerLength = 0;
size_t s_frameLength = 0;
uint32_t s_payloadTotal = 0;
uint32_t s_dropRemaining = 0;
uint16_t s_frameX = 0;
uint16_t s_frameY = 0;
uint16_t s_frameWidth = 0;
uint16_t s_frameHeight = 0;
bool s_receiving = false;
TouchReport s_touchReport = {};
bool s_touchPending = false;
bool s_touchWasDown = false;
uint16_t s_lastTouchX = 0;
uint16_t s_lastTouchY = 0;
const char *s_error = "not initialized";

size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

bool writeTouch(const uint8_t *data, size_t length)
{
    Wire.beginTransmission(kTouchAddress);
    Wire.write(data, length);
    return Wire.endTransmission() == 0;
}

bool readTouch(uint16_t &x, uint16_t &y, bool &pressed)
{
    const uint8_t address[] = {0xD0, 0x00};
    if (!writeTouch(address, sizeof(address)) || Wire.requestFrom(kTouchAddress, static_cast<uint8_t>(10)) != 10) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    uint8_t data[10];
    for (uint8_t &value : data) {
        value = Wire.read();
    }
    pressed = data[6] == 0xAB && (data[5] & 0x7F) != 0 && (data[0] & 0x0F) == 0x06;
    if (pressed) {
        const uint16_t rawX = (static_cast<uint16_t>(data[1]) << 4) | (data[3] >> 4);
        const uint16_t rawY = (static_cast<uint16_t>(data[2]) << 4) | (data[3] & 0x0F);
        x = kWidth - 1 - (rawX < kWidth ? rawX : kWidth - 1);
        y = kHeight - 1 - (rawY < kHeight ? rawY : kHeight - 1);
    }
    return true;
}

void queueTouch(bool down, uint16_t x, uint16_t y)
{
    TouchReport report = {};
    report.fingers[0] = {.tip = static_cast<uint8_t>(down), .id = 0, .x = x, .y = y, .width = 8, .height = 8};
    report.count = 1;
    xSemaphoreTake(s_touchLock, portMAX_DELAY);
    s_touchReport = report;
    s_touchPending = true;
    xSemaphoreGive(s_touchLock);
}

bool validHeader(const FrameHeader &header)
{
    return header.type == kFormatJpeg && header.width != 0 && header.height != 0 &&
           header.x < kWidth && header.y < kHeight && header.width <= kWidth - header.x &&
           header.height <= kHeight - header.y && header.payloadTotal != 0 && header.payloadTotal <= kFrameLimit;
}

bool patchJpegDimensions(uint8_t *data, size_t length, uint16_t width, uint16_t height)
{
    for (size_t offset = 2; offset + 9 <= length && data[offset] == 0xFF;) {
        const uint8_t marker = data[offset + 1];
        if (marker == 0xFF) {
            ++offset;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            return false;
        }
        const size_t segmentLength = (static_cast<size_t>(data[offset + 2]) << 8) | data[offset + 3];
        if (segmentLength < 2 || offset + 2 + segmentLength > length) {
            return false;
        }
        if (marker >= 0xC0 && marker <= 0xC2) {
            data[offset + 5] = height >> 8;
            data[offset + 6] = height;
            data[offset + 7] = width >> 8;
            data[offset + 8] = width;
            return true;
        }
        offset += 2 + segmentLength;
    }
    return false;
}

esp_err_t renderFrame()
{
    jpeg_decode_picture_info_t info = {};
    esp_err_t result = jpeg_decoder_get_info(s_frame, s_frameLength, &info);
    if (result != ESP_OK || info.width != s_frameWidth || info.height != s_frameHeight) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint16_t mcuWidth = 0;
    uint16_t mcuHeight = 0;
    switch (info.sample_method) {
    case JPEG_DOWN_SAMPLING_YUV444: mcuWidth = 8; mcuHeight = 8; break;
    case JPEG_DOWN_SAMPLING_YUV422: mcuWidth = 16; mcuHeight = 8; break;
    case JPEG_DOWN_SAMPLING_YUV420: mcuWidth = 16; mcuHeight = 16; break;
    default: return ESP_ERR_NOT_SUPPORTED;
    }
    const uint16_t decodedWidth = alignUp(info.width, mcuWidth);
    const uint16_t decodedHeight = alignUp(info.height, mcuHeight);
    if (!patchJpegDimensions(s_frame, s_frameLength, decodedWidth, decodedHeight)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const jpeg_decode_cfg_t decode = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t outputLength = 0;
    result = jpeg_decoder_process(s_jpeg, &decode, s_frame, s_frameLength, s_jpegOutput,
                                  decodedWidth * decodedHeight * sizeof(uint16_t), &outputLength);
    if (result != ESP_OK) {
        return result;
    }
    const ppa_srm_oper_config_t copy = {
        .in = {.buffer = s_jpegOutput, .pic_w = decodedWidth, .pic_h = decodedHeight,
               .block_w = s_frameWidth, .block_h = s_frameHeight, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .out = {.buffer = s_frameBuffer, .buffer_size = kCanvasBytes, .pic_w = kWidth, .pic_h = kHeight,
                .block_offset_x = s_frameX, .block_offset_y = s_frameY, .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    result = ppa_do_scale_rotate_mirror(s_ppa, &copy);
    if (result == ESP_OK) {
        result = esp_cache_msync(s_frameBuffer, kCanvasBytes,
                                 ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    if (result == ESP_OK) {
        result = esp_lcd_panel_draw_bitmap(s_display.rawPanel(), s_frameX, s_frameY,
                                           s_frameX + s_frameWidth, s_frameY + s_frameHeight,
                                           s_frameBuffer);
    }
    return result;
}

void frameTask(void *)
{
    while (true) {
        if (xSemaphoreTake(s_frameReady, portMAX_DELAY) == pdTRUE) {
            if (renderFrame() != ESP_OK) {
                Serial.println("JPEG frame dropped.");
            }
            xSemaphoreGive(s_frameAvailable);
        }
    }
}

void consumeFrameData(const uint8_t *data, size_t length)
{
    while (length != 0) {
        if (s_dropRemaining != 0) {
            const size_t take = length < s_dropRemaining ? length : s_dropRemaining;
            data += take;
            length -= take;
            s_dropRemaining -= take;
            continue;
        }
        if (!s_receiving) {
            const size_t take = (sizeof(s_headerBytes) - s_headerLength) < length ?
                                (sizeof(s_headerBytes) - s_headerLength) : length;
            memcpy(s_headerBytes + s_headerLength, data, take);
            s_headerLength += take;
            data += take;
            length -= take;
            if (s_headerLength != sizeof(FrameHeader)) {
                continue;
            }
            FrameHeader header = {};
            memcpy(&header, s_headerBytes, sizeof(header));
            s_headerLength = 0;
            if (!validHeader(header)) {
                s_dropRemaining = header.payloadTotal;
                continue;
            }
            xSemaphoreTake(s_frameAvailable, portMAX_DELAY);
            s_frameX = header.x;
            s_frameY = header.y;
            s_frameWidth = header.width;
            s_frameHeight = header.height;
            s_payloadTotal = header.payloadTotal;
            s_frameLength = 0;
            s_receiving = true;
        }
        const size_t remaining = s_payloadTotal - s_frameLength;
        const size_t take = length < remaining ? length : remaining;
        memcpy(s_frame + s_frameLength, data, take);
        s_frameLength += take;
        data += take;
        length -= take;
        if (s_frameLength == s_payloadTotal) {
            s_receiving = false;
            xSemaphoreGive(s_frameReady);
        }
    }
}

void pollTouch()
{
    uint16_t x = s_lastTouchX;
    uint16_t y = s_lastTouchY;
    bool pressed = false;
    if (!readTouch(x, y, pressed)) {
        return;
    }
    if (pressed) {
        s_lastTouchX = x;
        s_lastTouchY = y;
        queueTouch(true, x, y);
    } else if (s_touchWasDown) {
        queueTouch(false, s_lastTouchX, s_lastTouchY);
    }
    s_touchWasDown = pressed;
}

void startUsbTask(void *)
{
    uint8_t packet[kUsbChunkBytes];
    bool wasMounted = false;
    while (true) {
        tud_task_ext(1, false);
        const bool mounted = tud_mounted();
        if (!mounted && wasMounted) {
            // Arduino's USB core owns the mount callbacks. Reset an unfinished
            // frame here so a new host session cannot inherit its JPEG bytes.
            if (s_receiving) {
                s_receiving = false;
                xSemaphoreGive(s_frameAvailable);
            }
            s_headerLength = 0;
            s_dropRemaining = 0;
            s_touchWasDown = false;
        }
        wasMounted = mounted;
        while (tud_vendor_available() != 0) {
            const uint32_t read = tud_vendor_read(packet, sizeof(packet));
            if (read == 0) {
                break;
            }
            consumeFrameData(packet, read);
        }
        pollTouch();
        if (mounted && tud_hid_ready()) {
            xSemaphoreTake(s_touchLock, portMAX_DELAY);
            const bool pending = s_touchPending;
            const TouchReport report = s_touchReport;
            if (pending && tud_hid_report(kReportTouch, &report, sizeof(report))) {
                s_touchPending = false;
            }
            xSemaphoreGive(s_touchLock);
        }
        vTaskDelay(1);
    }
}

bool startUsb()
{
    const usb_phy_config_t phyConfig = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_HIGH,
    };
    if (usb_new_phy(&phyConfig, &s_phy) != ESP_OK) {
        s_error = "USB UTMI PHY initialization failed";
        return false;
    }
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_HIGH};
    if (!tusb_init(kUsbHsPort, &init)) {
        s_error = "TinyUSB high-speed initialization failed";
        return false;
    }
    if (xTaskCreate(startUsbTask, "usb_display", 6144, nullptr, 5, nullptr) != pdPASS) {
        s_error = "USB task creation failed";
        return false;
    }
    return true;
}

}  // namespace

bool UsbDisplay::begin()
{
    if (!s_display.begin()) {
        s_error = s_display.lastError();
        return false;
    }
    void *frameBuffer = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(s_display.rawPanel(), 1, &frameBuffer) != ESP_OK || frameBuffer == nullptr) {
        s_error = "display framebuffer acquisition failed";
        return false;
    }
    s_frameBuffer = static_cast<uint16_t *>(frameBuffer);
    s_frameReady = xSemaphoreCreateBinary();
    s_frameAvailable = xSemaphoreCreateBinary();
    s_touchLock = xSemaphoreCreateMutex();
    s_frame = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kFrameLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_jpegOutput = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, alignUp(kWidth, kMcuSize) * alignUp(kHeight, kMcuSize) * 2,
                                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_frameReady || !s_frameAvailable || !s_touchLock || !s_frame || !s_jpegOutput) {
        s_error = "USB display memory allocation failed";
        return false;
    }
    const jpeg_decode_engine_cfg_t jpegConfig = {.intr_priority = 0, .timeout_ms = 100};
    const ppa_client_config_t ppaConfig = {.oper_type = PPA_OPERATION_SRM};
    if (jpeg_new_decoder_engine(&jpegConfig, &s_jpeg) != ESP_OK || ppa_register_client(&ppaConfig, &s_ppa) != ESP_OK) {
        s_error = "JPEG or PPA initialization failed";
        return false;
    }
    xSemaphoreGive(s_frameAvailable);
    if (xTaskCreate(frameTask, "jpeg_frame", 6144, nullptr, 6, &s_frameTask) != pdPASS) {
        s_error = "JPEG frame task creation failed";
        return false;
    }
    if (!startUsb()) {
        return false;
    }
    s_error = "";
    return true;
}

void UsbDisplay::tick()
{
    // USB and JPEG work run in dedicated tasks so bulk transfers can apply backpressure.
}

const char *UsbDisplay::lastError() const
{
    return s_error;
}

extern "C" void tud_vendor_rx_cb(uint8_t, const uint8_t *, uint32_t)
{
    // The Arduino core uses a 512-byte vendor RX FIFO. startUsbTask drains it.
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t reportId, hid_report_type_t,
                                            uint8_t *buffer, uint16_t requestLength)
{
    if (reportId == kReportMaxCount && requestLength >= 1) {
        buffer[0] = kTouchPoints;
        return 1;
    }
    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t *, uint16_t) {}
