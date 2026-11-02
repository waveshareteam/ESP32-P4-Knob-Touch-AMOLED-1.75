/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNOB_BOARD_LCD_H_RES  466
#define KNOB_BOARD_LCD_V_RES  466

/* Names are the single board-definition contract used by every example. */
#define KNOB_BOARD_PERIPH_I2C             "i2c_master"
#define KNOB_BOARD_PERIPH_KNOB_CW         "gpio_knob_cw"
#define KNOB_BOARD_PERIPH_KNOB_CCW        "gpio_knob_ccw"
#define KNOB_BOARD_PERIPH_HAPTIC_IN1      "gpio_haptic_in1"
#define KNOB_BOARD_PERIPH_HAPTIC_IN2      "gpio_haptic_in2"
#define KNOB_BOARD_PERIPH_BOOT            "gpio_boot"
#define KNOB_BOARD_PERIPH_POWER           "gpio_power"
#define KNOB_BOARD_PERIPH_RTC_INT         "gpio_rtc_int"
#define KNOB_BOARD_PERIPH_IMU_INT1        "gpio_imu_int1"
#define KNOB_BOARD_PERIPH_IMU_INT2        "gpio_imu_int2"

#define KNOB_BOARD_DEVICE_LCD             "display_lcd"
#define KNOB_BOARD_DEVICE_TOUCH           "lcd_touch"
#define KNOB_BOARD_DEVICE_AUDIO_DAC       "audio_dac"
#define KNOB_BOARD_DEVICE_AUDIO_ADC       "audio_adc"
#define KNOB_BOARD_DEVICE_SDCARD          "fs_sdcard"
#define KNOB_BOARD_DEVICE_WIRELESS         "wireless"

typedef struct {
    esp_lv_adapter_config_t adapter_config;
    esp_lv_adapter_rotation_t rotation;
    esp_lv_adapter_tear_avoid_mode_t tear_avoid_mode;
    bool enable_touch;
} knob_board_display_config_t;

/** Prepare Board Manager tables without initializing unrelated devices. */
esp_err_t knob_board_prepare(void);

/** Return the generated board metadata without creating hardware handles. */
esp_err_t knob_board_get_info(const char **name, const char **chip, const char **version);

/** Initialize one generated GPIO peripheral and return its configured pin. */
esp_err_t knob_board_gpio_init(const char *name, int *gpio_num);

/** Initialize and return the shared I2C bus. */
esp_err_t knob_board_i2c_init(i2c_master_bus_handle_t *bus);

/** Initialize LCD, then optionally touch, and start the LVGL adapter. */
lv_display_t *knob_board_display_start(const knob_board_display_config_t *config);
bool knob_board_display_lock(int timeout_ms);
void knob_board_display_unlock(void);
esp_lcd_panel_io_handle_t knob_board_display_get_io(void);
esp_lcd_touch_handle_t knob_board_touch_get_handle(void);
lv_indev_t *knob_board_display_get_input(void);
esp_err_t knob_board_display_brightness_set(int percent);

/** Initialize a Board Manager codec device and return its codec-dev handle. */
esp_err_t knob_board_audio_dac_init(esp_codec_dev_handle_t *codec);
esp_err_t knob_board_audio_adc_init(esp_codec_dev_handle_t *codec);
esp_err_t knob_board_audio_open(esp_codec_dev_handle_t codec, esp_codec_dev_sample_info_t *format);
esp_err_t knob_board_audio_close(esp_codec_dev_handle_t codec);
esp_err_t knob_board_audio_set_volume(esp_codec_dev_handle_t codec, int volume);
esp_err_t knob_board_audio_set_mute(esp_codec_dev_handle_t codec, bool mute);
esp_err_t knob_board_audio_write(esp_codec_dev_handle_t codec, const void *data, size_t len, size_t *written);

/**
 * Mount the Board Manager SD device. When independent_power is true, the
 * caller owns LDO4 instead of relying on an esp-extconn user of that rail.
 */
esp_err_t knob_board_sdcard_mount(bool independent_power, const char **mount_point);
esp_err_t knob_board_sdcard_get_card(sdmmc_card_t **card);
esp_err_t knob_board_sdcard_unmount(void);

#ifdef __cplusplus
}
#endif
