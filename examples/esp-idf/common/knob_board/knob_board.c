/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "periph_gpio.h"
#include "dev_audio_codec.h"
#include "dev_display_lcd.h"
#include "dev_fs_fat.h"
#include "dev_lcd_touch.h"

#include "knob_board.h"

static const char *TAG = "knob_board";

typedef struct {
    const char *name;
    periph_gpio_handle_t *handle;
} gpio_entry_t;

static gpio_entry_t s_gpio_entries[] = {
    {KNOB_BOARD_PERIPH_KNOB_CW, NULL},
    {KNOB_BOARD_PERIPH_KNOB_CCW, NULL},
    {KNOB_BOARD_PERIPH_HAPTIC_IN1, NULL},
    {KNOB_BOARD_PERIPH_HAPTIC_IN2, NULL},
    {KNOB_BOARD_PERIPH_BOOT, NULL},
    {KNOB_BOARD_PERIPH_POWER, NULL},
    {KNOB_BOARD_PERIPH_RTC_INT, NULL},
    {KNOB_BOARD_PERIPH_IMU_INT1, NULL},
    {KNOB_BOARD_PERIPH_IMU_INT2, NULL},
};

static i2c_master_bus_handle_t s_i2c_bus;
static dev_display_lcd_handles_t *s_lcd;
static dev_lcd_touch_handles_t *s_touch;
static lv_display_t *s_display;
static lv_indev_t *s_input;
static uint8_t s_brightness;
static dev_fs_fat_handle_t *s_sdcard;
static dev_fs_fat_config_t s_sdcard_override;
static esp_codec_dev_handle_t s_dac;
static esp_codec_dev_handle_t s_adc;

esp_err_t knob_board_prepare(void)
{
    return esp_board_manager_prepare();
}

esp_err_t knob_board_get_info(const char **name, const char **chip, const char **version)
{
    esp_board_info_t info = {0};
    ESP_RETURN_ON_ERROR(esp_board_manager_get_board_info(&info), TAG, "get board info failed");
    if (name) {
        *name = info.name;
    }
    if (chip) {
        *chip = info.chip;
    }
    if (version) {
        *version = info.version;
    }
    return ESP_OK;
}

static gpio_entry_t *find_gpio(const char *name)
{
    for (size_t i = 0; i < sizeof(s_gpio_entries) / sizeof(s_gpio_entries[0]); ++i) {
        if (strcmp(s_gpio_entries[i].name, name) == 0) {
            return &s_gpio_entries[i];
        }
    }
    return NULL;
}

esp_err_t knob_board_gpio_init(const char *name, int *gpio_num)
{
    ESP_RETURN_ON_FALSE(name && gpio_num, ESP_ERR_INVALID_ARG, TAG, "invalid GPIO request");
    gpio_entry_t *entry = find_gpio(name);
    ESP_RETURN_ON_FALSE(entry, ESP_ERR_NOT_FOUND, TAG, "unknown board GPIO: %s", name);

    if (!entry->handle) {
        ESP_RETURN_ON_ERROR(esp_board_periph_init(name), TAG, "init %s failed", name);
        ESP_RETURN_ON_ERROR(esp_board_manager_get_periph_handle(name, (void **)&entry->handle), TAG,
                            "get %s failed", name);
    }
    *gpio_num = entry->handle->gpio_num;
    return ESP_OK;
}

esp_err_t knob_board_i2c_init(i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "bus is null");
    if (!s_i2c_bus) {
        ESP_RETURN_ON_ERROR(esp_board_periph_init(KNOB_BOARD_PERIPH_I2C), TAG, "init I2C failed");
        ESP_RETURN_ON_ERROR(esp_board_manager_get_periph_handle(KNOB_BOARD_PERIPH_I2C, (void **)&s_i2c_bus), TAG,
                            "get I2C failed");
    }
    *bus = s_i2c_bus;
    return ESP_OK;
}

static esp_err_t init_device(const char *name, void **handle)
{
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(name), TAG, "init %s failed", name);
    return esp_board_manager_get_device_handle(name, handle);
}

lv_display_t *knob_board_display_start(const knob_board_display_config_t *config)
{
    if (s_display) {
        return s_display;
    }

    const esp_lv_adapter_config_t default_adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    const knob_board_display_config_t default_config = {
        .adapter_config = default_adapter,
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .enable_touch = true,
    };
    if (!config) {
        config = &default_config;
    }

    if (init_device(KNOB_BOARD_DEVICE_LCD, (void **)&s_lcd) != ESP_OK || !s_lcd) {
        ESP_LOGE(TAG, "LCD Board Manager device unavailable");
        return NULL;
    }

    dev_display_lcd_config_t *lcd_config = NULL;
    if (esp_board_manager_get_device_config(KNOB_BOARD_DEVICE_LCD, (void **)&lcd_config) != ESP_OK ||
        !lcd_config || lcd_config->lcd_width != KNOB_BOARD_LCD_H_RES ||
        lcd_config->lcd_height != KNOB_BOARD_LCD_V_RES) {
        ESP_LOGE(TAG, "unexpected LCD board configuration");
        return NULL;
    }

    if (esp_lv_adapter_init(&config->adapter_config) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed");
        return NULL;
    }
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        s_lcd->panel_handle, s_lcd->io_handle, KNOB_BOARD_LCD_H_RES, KNOB_BOARD_LCD_V_RES, config->rotation);
    display_config.profile.use_psram = true;
    display_config.tear_avoid_mode = config->tear_avoid_mode;
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    if (!display) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return NULL;
    }

    if (config->enable_touch) {
        if (init_device(KNOB_BOARD_DEVICE_TOUCH, (void **)&s_touch) != ESP_OK || !s_touch) {
            ESP_LOGE(TAG, "touch Board Manager device unavailable");
            return NULL;
        }
        const esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_touch->touch_handle);
        s_input = esp_lv_adapter_register_touch(&touch_config);
        if (!s_input) {
            ESP_LOGE(TAG, "LVGL touch registration failed");
            return NULL;
        }
    }
    if (esp_lv_adapter_start() != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed");
        return NULL;
    }
    s_display = display;
    return s_display;
}

bool knob_board_display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

void knob_board_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

esp_lcd_panel_io_handle_t knob_board_display_get_io(void)
{
    return s_lcd ? s_lcd->io_handle : NULL;
}

esp_lcd_touch_handle_t knob_board_touch_get_handle(void)
{
    return s_touch ? s_touch->touch_handle : NULL;
}

lv_indev_t *knob_board_display_get_input(void)
{
    return s_input;
}

esp_err_t knob_board_display_brightness_set(int percent)
{
    ESP_RETURN_ON_FALSE(s_lcd, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    ESP_RETURN_ON_FALSE(percent >= 0 && percent <= 100, ESP_ERR_INVALID_ARG, TAG, "invalid brightness");
    s_brightness = (uint8_t)(percent * 255 / 100);
    return esp_lcd_panel_io_tx_param(s_lcd->io_handle, 0x51, &s_brightness, 1);
}

static esp_err_t get_codec(const char *name, esp_codec_dev_handle_t *cached, esp_codec_dev_handle_t *codec)
{
    ESP_RETURN_ON_FALSE(codec, ESP_ERR_INVALID_ARG, TAG, "codec is null");
    if (*cached) {
        *codec = *cached;
        return ESP_OK;
    }
    dev_audio_codec_handles_t *handles = NULL;
    ESP_RETURN_ON_ERROR(init_device(name, (void **)&handles), TAG, "get %s failed", name);
    ESP_RETURN_ON_FALSE(handles && handles->codec_dev, ESP_ERR_INVALID_STATE, TAG, "%s has no codec", name);
    *cached = handles->codec_dev;
    *codec = *cached;
    return ESP_OK;
}

esp_err_t knob_board_audio_dac_init(esp_codec_dev_handle_t *codec)
{
    return get_codec(KNOB_BOARD_DEVICE_AUDIO_DAC, &s_dac, codec);
}

esp_err_t knob_board_audio_adc_init(esp_codec_dev_handle_t *codec)
{
    return get_codec(KNOB_BOARD_DEVICE_AUDIO_ADC, &s_adc, codec);
}

esp_err_t knob_board_audio_open(esp_codec_dev_handle_t codec, esp_codec_dev_sample_info_t *format)
{
    ESP_RETURN_ON_FALSE(codec && format, ESP_ERR_INVALID_ARG, TAG, "invalid audio format");
    return esp_codec_dev_open(codec, format) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t knob_board_audio_close(esp_codec_dev_handle_t codec)
{
    ESP_RETURN_ON_FALSE(codec, ESP_ERR_INVALID_ARG, TAG, "codec is null");
    return esp_codec_dev_close(codec) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t knob_board_audio_set_volume(esp_codec_dev_handle_t codec, int volume)
{
    ESP_RETURN_ON_FALSE(codec, ESP_ERR_INVALID_ARG, TAG, "codec is null");
    return esp_codec_dev_set_out_vol(codec, volume) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t knob_board_audio_set_mute(esp_codec_dev_handle_t codec, bool mute)
{
    ESP_RETURN_ON_FALSE(codec, ESP_ERR_INVALID_ARG, TAG, "codec is null");
    return esp_codec_dev_set_out_mute(codec, mute) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t knob_board_audio_write(esp_codec_dev_handle_t codec, const void *data, size_t len, size_t *written)
{
    ESP_RETURN_ON_FALSE(codec && data, ESP_ERR_INVALID_ARG, TAG, "invalid audio buffer");
    int ret = esp_codec_dev_write(codec, (void *)data, len);
    if (written) {
        *written = ret == ESP_CODEC_DEV_OK ? len : 0;
    }
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t knob_board_sdcard_mount(bool independent_power, const char **mount_point)
{
    if (!s_sdcard && independent_power) {
        dev_fs_fat_config_t *config = NULL;
        ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(KNOB_BOARD_DEVICE_SDCARD, (void **)&config), TAG,
                            "get SD configuration failed");
        ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_STATE, TAG, "SD configuration missing");
        s_sdcard_override = *config;
        s_sdcard_override.sub_cfg.sdmmc.ldo_chan_id = 4;
        ESP_RETURN_ON_ERROR(esp_board_device_override_config(KNOB_BOARD_DEVICE_SDCARD, &s_sdcard_override,
                                                              sizeof(s_sdcard_override)),
                            TAG, "override independent SD power failed");
    }
    if (!s_sdcard) {
        ESP_RETURN_ON_ERROR(init_device(KNOB_BOARD_DEVICE_SDCARD, (void **)&s_sdcard), TAG, "mount SD failed");
    }
    if (mount_point) {
        *mount_point = s_sdcard->mount_point;
    }
    return ESP_OK;
}

esp_err_t knob_board_sdcard_get_card(sdmmc_card_t **card)
{
    ESP_RETURN_ON_FALSE(card, ESP_ERR_INVALID_ARG, TAG, "card is null");
    ESP_RETURN_ON_FALSE(s_sdcard && s_sdcard->card, ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");
    *card = s_sdcard->card;
    return ESP_OK;
}

esp_err_t knob_board_sdcard_unmount(void)
{
    if (!s_sdcard) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_board_manager_deinit_device_by_name(KNOB_BOARD_DEVICE_SDCARD), TAG, "unmount SD failed");
    s_sdcard = NULL;
    return ESP_OK;
}
