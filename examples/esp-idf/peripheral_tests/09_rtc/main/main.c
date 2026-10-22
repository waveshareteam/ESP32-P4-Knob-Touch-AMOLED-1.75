#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"

static const char *TAG = "09_rtc";

#define PCF85063_REG_SECONDS 0x04
#define PCF85063_ADDR_7BIT 0x51
#define PCF85063_INT_GPIO GPIO_NUM_3
#define RTC_I2C_CLK_HZ 100000U
#define RTC_SAMPLE_PERIOD_MS 1000

typedef struct {
    bool online;
    bool bcd_valid;
    bool oscillator_stop;
    esp_err_t error;
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint8_t year;
    int int_level;
} rtc_sample_t;

static lv_display_t *g_disp;
static QueueHandle_t g_sample_queue;
static lv_obj_t *g_clock_label;
static lv_obj_t *g_date_label;
static lv_obj_t *g_status_label;
static lv_obj_t *g_detail_label;

static esp_err_t gpio_input(gpio_num_t gpio, bool pullup)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 100);
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static bool decode_bcd(uint8_t raw, uint8_t mask, uint8_t min, uint8_t max, uint8_t *out)
{
    uint8_t value = raw & mask;
    if ((value & 0x0F) > 9 || ((value >> 4) & 0x0F) > 9) {
        return false;
    }
    uint8_t decoded = from_bcd(value);
    if (decoded < min || decoded > max) {
        return false;
    }
    *out = decoded;
    return true;
}

static bool is_leap_year_20xx(uint8_t year)
{
    uint16_t full_year = 2000U + year;
    return ((full_year % 4U) == 0U && (full_year % 100U) != 0U) || ((full_year % 400U) == 0U);
}

static uint8_t days_in_month(uint8_t month, uint8_t year)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year_20xx(year)) {
        return 29;
    }
    return days[month - 1];
}

static esp_err_t read_rtc(i2c_master_dev_handle_t dev, rtc_sample_t *sample)
{
    uint8_t regs[7] = {0};
    esp_err_t ret = i2c_read_reg(dev, PCF85063_REG_SECONDS, regs, sizeof(regs));
    if (ret != ESP_OK) {
        sample->online = false;
        sample->bcd_valid = false;
        sample->error = ret;
        return ret;
    }

    sample->online = true;
    sample->error = ESP_OK;
    sample->oscillator_stop = (regs[0] & 0x80) != 0;
    sample->bcd_valid = decode_bcd(regs[0], 0x7F, 0, 59, &sample->sec) &&
                        decode_bcd(regs[1], 0x7F, 0, 59, &sample->min) &&
                        decode_bcd(regs[2], 0x3F, 0, 23, &sample->hour) &&
                        decode_bcd(regs[3], 0x3F, 1, 31, &sample->day) &&
                        decode_bcd(regs[4], 0x07, 0, 6, &sample->weekday) &&
                        decode_bcd(regs[5], 0x1F, 1, 12, &sample->month) &&
                        decode_bcd(regs[6], 0xFF, 0, 99, &sample->year);
    if (sample->bcd_valid && sample->day > days_in_month(sample->month, sample->year)) {
        sample->bcd_valid = false;
    }
    if (!sample->bcd_valid) {
        sample->error = ESP_ERR_INVALID_RESPONSE;
        return sample->error;
    }
    return ESP_OK;
}

static void close_i2c(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev)
{
    if (*dev) {
        (void)i2c_master_bus_rm_device(*dev);
        *dev = NULL;
    }
    *bus = NULL;
}

static esp_err_t open_i2c(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "acquire I2C");
    *bus = bsp_i2c_get_handle();
    esp_err_t ret = i2c_master_probe(*bus, PCF85063_ADDR_7BIT, 100);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
        return ret;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR_7BIT,
        .scl_speed_hz = RTC_I2C_CLK_HZ,
    };
    ret = i2c_master_bus_add_device(*bus, &dev_cfg, dev);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
    }
    return ret;
}

static void rtc_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;

    while (true) {
        rtc_sample_t sample = {
            .online = false,
            .bcd_valid = false,
            .error = ESP_OK,
            .int_level = gpio_get_level(PCF85063_INT_GPIO),
        };

        if (!dev) {
            esp_err_t ret = open_i2c(&bus, &dev);
            if (ret != ESP_OK) {
                sample.error = ret;
                ESP_LOGW(TAG, "PCF85063 open failed: %s", esp_err_to_name(ret));
                xQueueOverwrite(g_sample_queue, &sample);
                vTaskDelay(pdMS_TO_TICKS(RTC_SAMPLE_PERIOD_MS));
                continue;
            }
            ESP_LOGI(TAG, "PCF85063 online at 0x%02x", PCF85063_ADDR_7BIT);
        }

        esp_err_t ret = read_rtc(dev, &sample);
        sample.int_level = gpio_get_level(PCF85063_INT_GPIO);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "RTC read invalid/error: %s", esp_err_to_name(ret));
            close_i2c(&bus, &dev);
        }
        xQueueOverwrite(g_sample_queue, &sample);
        vTaskDelay(pdMS_TO_TICKS(RTC_SAMPLE_PERIOD_MS));
    }
}

static const char *weekday_name(uint8_t weekday)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return weekday < 7 ? names[weekday] : "N/A";
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF6F8FB), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "PCF85063 RTC");
    lv_obj_set_style_text_color(title, lv_color_hex(0x18212B), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    g_status_label = lv_label_create(screen);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_status_label, 426);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x425466), LV_PART_MAIN);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 20, 50);
    lv_label_set_text(g_status_label, "Waiting for RTC sample");

    g_clock_label = lv_label_create(screen);
    lv_obj_set_style_text_color(g_clock_label, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_clock_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_label_set_text(g_clock_label, "--:--:--");
    lv_obj_align(g_clock_label, LV_ALIGN_CENTER, 0, -36);

    g_date_label = lv_label_create(screen);
    lv_label_set_long_mode(g_date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_date_label, 426);
    lv_obj_set_style_text_color(g_date_label, lv_color_hex(0x27313D), LV_PART_MAIN);
    lv_label_set_text(g_date_label, "No valid RTC read");
    lv_obj_align(g_date_label, LV_ALIGN_CENTER, 0, 10);

    g_detail_label = lv_label_create(screen);
    lv_label_set_long_mode(g_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_detail_label, 426);
    lv_obj_set_style_text_color(g_detail_label, lv_color_hex(0x425466), LV_PART_MAIN);
    lv_label_set_text(g_detail_label, "INT: N/A");
    lv_obj_align(g_detail_label, LV_ALIGN_BOTTOM_LEFT, 20, -28);
}

static void update_ui(const rtc_sample_t *sample)
{
    char text[96];
    char detail[128];

    if (!sample->online || sample->error != ESP_OK || !sample->bcd_valid) {
        lv_label_set_text(g_clock_label, "--:--:--");
        lv_label_set_text(g_date_label, "No valid RTC read");
        snprintf(text, sizeof(text), "ERROR PCF85063 0x%02X: %s",
                 PCF85063_ADDR_7BIT, esp_err_to_name(sample->error));
        lv_label_set_text(g_status_label, text);
        snprintf(detail, sizeof(detail), "INT GPIO%d: %d | invalid data hidden",
                 PCF85063_INT_GPIO, sample->int_level);
        lv_label_set_text(g_detail_label, detail);
        return;
    }

    snprintf(text, sizeof(text), "%02u:%02u:%02u", sample->hour, sample->min, sample->sec);
    lv_label_set_text(g_clock_label, text);
    snprintf(text, sizeof(text), "20%02u-%02u-%02u  %s",
             sample->year, sample->month, sample->day, weekday_name(sample->weekday));
    lv_label_set_text(g_date_label, text);
    snprintf(text, sizeof(text), "ONLINE PCF85063 0x%02X | %s",
             PCF85063_ADDR_7BIT, sample->oscillator_stop ? "OSC STOP / INVALID" : "OSC OK");
    lv_label_set_text(g_status_label, text);
    snprintf(detail, sizeof(detail), "INT GPIO%d: %d | BCD: valid | Write time: disabled",
             PCF85063_INT_GPIO, sample->int_level);
    lv_label_set_text(g_detail_label, detail);
}

static void fail_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "START PCF85063 read-only LVGL clock test");
    ESP_ERROR_CHECK(gpio_input(PCF85063_INT_GPIO, true));

    g_sample_queue = xQueueCreate(1, sizeof(rtc_sample_t));
    if (!g_sample_queue) {
        ESP_LOGE(TAG, "FAIL create RTC queue");
        fail_idle();
    }

    g_disp = bsp_display_start();
    if (!g_disp) {
        ESP_LOGE(TAG, "FAIL LVGL init: %s", esp_err_to_name(ESP_FAIL));
        fail_idle();
    }
    if (bsp_display_lock(-1)) {
        create_ui();
        bsp_display_unlock();
    }

    xTaskCreate(rtc_task, "rtc_sample", 4096, NULL, 5, NULL);

    while (true) {
        rtc_sample_t sample;
        if (xQueueReceive(g_sample_queue, &sample, 0) == pdTRUE) {
            if (bsp_display_lock(-1)) {
                update_ui(&sample);
                bsp_display_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
