/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time display geometry used by applications. board_display_start()
 * checks these values against the Board Manager display_lcd configuration. */
#define BOARD_LCD_H_RES             (466)
#define BOARD_LCD_V_RES             (466)
#define BOARD_LCD_BITS_PER_PIXEL    (16)
#define BOARD_LCD_DPI_BUFFER_NUMS   (3)

/* Shared I2C bus speed, matching the board's touch and codec devices */
#define BOARD_I2C_CLK_SPEED_HZ      (400000)

#define BOARD_SPIFFS_MOUNT_POINT    "/spiffs"

#define BOARD_AUDIO_DEFAULT_SAMPLE_RATE  (16000)
#define BOARD_AUDIO_DEFAULT_BIT_WIDTH    (16)
#define BOARD_AUDIO_DEFAULT_CHANNEL      (2)
#define BOARD_AUDIO_DEFAULT_VOLUME       (80)
#define BOARD_AUDIO_DEFAULT_ADC_GAIN     (30.0)    // dB; as xiaozhi-esp32 uses for ES7210 boards, so far voices reach speech recognition

/**
 * @brief  Initialize every Board Manager peripheral and device that is not marked init_skip
 *
 *         This mounts SPIFFS, powers the MIPI-DSI PHY, initializes the CO5300 panel,
 *         CST9217 touch, ES8389 playback and ES7210 capture. The microSD card is handled by the
 *         application storage service.
 */
esp_err_t board_app_init(void);

i2c_master_bus_handle_t board_i2c_get_handle(void);

/* Display */
lv_display_t *board_display_start(const esp_lv_adapter_config_t *cfg);
bool board_display_lock(int timeout_ms);
void board_display_unlock(void);
esp_err_t board_display_brightness_set(int brightness_percent);
int board_display_brightness_get(void);
esp_err_t board_display_backlight_on(void);
esp_err_t board_display_backlight_off(void);
esp_lcd_panel_handle_t board_display_get_panel_handle(void);
esp_lcd_touch_handle_t board_display_get_touch_handle(void);
lv_indev_t *board_display_get_input_dev(void);

/* Audio codec primitives; applications go through the audio service instead */
esp_err_t board_audio_init(void);
esp_err_t board_audio_mute_set(bool enable);
esp_err_t board_audio_volume_set(int volume, int *volume_set);
int board_audio_volume_get(void);
esp_err_t board_audio_stop(void);
esp_err_t board_audio_resume(void);
esp_err_t board_audio_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);
esp_err_t board_audio_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t board_audio_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
