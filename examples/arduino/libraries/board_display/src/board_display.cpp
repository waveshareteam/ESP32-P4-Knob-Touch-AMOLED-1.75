#include "board_display.h"

#include <cstring>

#include <Wire.h>

extern "C" {
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "driver/esp_lcd_co5300.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp32-hal-ldo.h"
}

#include <pin_config.h>

namespace {

constexpr uint8_t kTouchAddress = 0x5A;

bool i2cWrite(const uint8_t *data, size_t size)
{
    Wire.beginTransmission(kTouchAddress);
    Wire.write(data, size);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool KnobBoardDisplay::begin()
{
    if (panel_ != nullptr) {
        return true;
    }

    const esp_err_t ldoResult = ldoAcquireChannel(3, 2500, false, &dsiLdo_);
    if (ldoResult != ESP_OK) {
        lastError_ = "MIPI DSI LDO3 acquisition failed";
        return false;
    }

    const esp_lcd_dsi_bus_config_t busConfig = {
        .bus_id = 0,
        .num_data_lanes = 1,
        .phy_clk_src = static_cast<mipi_dsi_phy_pllref_clock_source_t>(0),
        .lane_bit_rate_mbps = 480,
    };
    if (esp_lcd_new_dsi_bus(&busConfig, &dsiBus_) != ESP_OK) {
        lastError_ = "MIPI DSI bus initialization failed";
        return false;
    }

    const esp_lcd_dbi_io_config_t dbiConfig = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_dbi(dsiBus_, &dbiConfig, &io_) != ESP_OK) {
        lastError_ = "MIPI DSI command channel initialization failed";
        return false;
    }

    const esp_lcd_dpi_panel_config_t dpiConfig = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 16,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 3,
        .video_timing = {
            .h_size = kWidth,
            .v_size = kHeight,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 32,
            .hsync_front_porch = 32,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 12,
            .vsync_front_porch = 18,
        },
        // Arduino's partial LVGL buffer is reused immediately after the flush callback.
        // Keep the IDF 5.5 panel copy synchronous so lv_display_flush_ready() is safe.
        .flags = {.use_dma2d = false, .disable_lp = false},
    };
    co5300_vendor_config_t vendorConfig = {
        .mipi_config = {.dsi_bus = dsiBus_, .dpi_config = &dpiConfig},
        .flags = {.use_mipi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panelConfig = {
        .reset_gpio_num = KNOB_PIN_LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendorConfig,
    };
    if (esp_lcd_new_panel_co5300(io_, &panelConfig, &panel_) != ESP_OK ||
        esp_lcd_panel_reset(panel_) != ESP_OK ||
        esp_lcd_panel_init(panel_) != ESP_OK) {
        lastError_ = "CO5300 panel initialization failed";
        return false;
    }

    void *frameBuffer = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(panel_, 1, &frameBuffer) == ESP_OK && frameBuffer != nullptr) {
        std::memset(frameBuffer, 0, kWidth * kHeight * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(panel_, 0, 0, kWidth, kHeight, frameBuffer);
    }

    Wire.begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, 400000);
    if (!initializeTouch()) {
        lastError_ = "CST9217 touch initialization failed";
        return false;
    }
    lastError_ = "";
    return true;
}

bool KnobBoardDisplay::beginLvgl(uint16_t viewportSize)
{
    if (panel_ == nullptr && !begin()) {
        return false;
    }
    if (display_ != nullptr) {
        return viewportSize_ == viewportSize;
    }
    if (viewportSize == 0 || viewportSize > kWidth) {
        lastError_ = "invalid LVGL viewport size";
        return false;
    }

    viewportSize_ = viewportSize;
    viewportOffset_ = static_cast<int16_t>((kWidth - viewportSize_) / 2);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    const size_t drawBufferPixels = viewportSize_ * 24;
    drawBuffer_ = heap_caps_malloc(drawBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (drawBuffer_ == nullptr) {
        lastError_ = "LVGL draw buffer allocation failed";
        return false;
    }
    display_ = lv_display_create(viewportSize_, viewportSize_);
    if (display_ == nullptr) {
        lastError_ = "LVGL display creation failed";
        return false;
    }
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display_, drawBuffer_, nullptr, drawBufferPixels * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_, flushCallback);

    touchInput_ = lv_indev_create();
    if (touchInput_ == nullptr) {
        lastError_ = "LVGL touch input creation failed";
        return false;
    }
    lv_indev_set_type(touchInput_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touchInput_, display_);
    lv_indev_set_user_data(touchInput_, this);
    lv_indev_set_read_cb(touchInput_, touchCallback);
    return true;
}

void KnobBoardDisplay::tick()
{
    if (display_ != nullptr) {
        lv_timer_handler();
    }
}

const char *KnobBoardDisplay::lastError() const
{
    return lastError_;
}

struct esp_lcd_panel_t *KnobBoardDisplay::rawPanel() const
{
    return panel_;
}

bool KnobBoardDisplay::initializeTouch()
{
    pinMode(KNOB_PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(KNOB_PIN_TOUCH_RESET, OUTPUT);
    digitalWrite(KNOB_PIN_TOUCH_RESET, LOW);
    delay(10);
    digitalWrite(KNOB_PIN_TOUCH_RESET, HIGH);
    delay(50);
    const uint8_t enterCommandMode[] = {0xD1, 0x01, 0xD1, 0x01};
    return i2cWrite(enterCommandMode, sizeof(enterCommandMode));
}

bool KnobBoardDisplay::readTouch()
{
    const uint8_t address[] = {0xD0, 0x00};
    if (!i2cWrite(address, sizeof(address))) {
        return false;
    }
    const size_t count = Wire.requestFrom(kTouchAddress, static_cast<uint8_t>(10));
    if (count != 10) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    uint8_t data[10];
    for (uint8_t &value : data) {
        value = Wire.read();
    }
    if (data[6] != 0xAB || (data[5] & 0x7F) == 0 || (data[0] & 0x0F) != 0x06) {
        touchPressed_ = false;
        return true;
    }
    const uint16_t x = (static_cast<uint16_t>(data[1]) << 4) | (data[3] >> 4);
    const uint16_t y = (static_cast<uint16_t>(data[2]) << 4) | (data[3] & 0x0F);
    const int16_t physicalX = static_cast<int16_t>(kWidth - 1 - (x < kWidth ? x : kWidth - 1));
    const int16_t physicalY = static_cast<int16_t>(kHeight - 1 - (y < kHeight ? y : kHeight - 1));
    touchX_ = physicalX - viewportOffset_;
    touchY_ = physicalY - viewportOffset_;
    touchPressed_ = touchX_ >= 0 && touchY_ >= 0 && touchX_ < viewportSize_ && touchY_ < viewportSize_;
    return true;
}

void KnobBoardDisplay::flushCallback(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    auto *self = static_cast<KnobBoardDisplay *>(lv_display_get_user_data(display));
    if (self != nullptr && self->panel_ != nullptr) {
        esp_lcd_panel_draw_bitmap(self->panel_, area->x1 + self->viewportOffset_, area->y1 + self->viewportOffset_,
                                  area->x2 + 1 + self->viewportOffset_, area->y2 + 1 + self->viewportOffset_, pixels);
    }
    lv_display_flush_ready(display);
}

void KnobBoardDisplay::touchCallback(lv_indev_t *input, lv_indev_data_t *data)
{
    auto *self = static_cast<KnobBoardDisplay *>(lv_indev_get_user_data(input));
    if (self != nullptr) {
        if (!self->readTouch()) {
            self->touchPressed_ = false;
        }
        data->point.x = self->touchX_;
        data->point.y = self->touchY_;
        data->state = self->touchPressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
}
