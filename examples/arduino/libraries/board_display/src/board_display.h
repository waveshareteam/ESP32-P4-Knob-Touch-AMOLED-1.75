#pragma once

#include <Arduino.h>
#include <lvgl.h>

class KnobBoardDisplay {
public:
    static constexpr uint16_t kWidth = 466;
    static constexpr uint16_t kHeight = 466;

    bool begin();
    bool beginLvgl(uint16_t viewportSize = kWidth);
    void tick();
    const char *lastError() const;
    struct esp_lcd_panel_t *rawPanel() const;

private:
    struct esp_lcd_panel_t *panel_ = nullptr;
    struct esp_lcd_panel_io_t *io_ = nullptr;
    struct esp_lcd_dsi_bus_t *dsiBus_ = nullptr;
    struct ldo_regulator_channel_t *dsiLdo_ = nullptr;
    lv_display_t *display_ = nullptr;
    lv_indev_t *touchInput_ = nullptr;
    void *drawBuffer_ = nullptr;
    uint16_t viewportSize_ = kWidth;
    int16_t viewportOffset_ = 0;
    bool touchPressed_ = false;
    int16_t touchX_ = 0;
    int16_t touchY_ = 0;
    const char *lastError_ = "not initialized";

    bool initializeTouch();
    bool readTouch();
    static void flushCallback(lv_display_t *display, const lv_area_t *area, uint8_t *pixels);
    static void touchCallback(lv_indev_t *input, lv_indev_data_t *data);
};
