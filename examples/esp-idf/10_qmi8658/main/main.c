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
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"

static const char *TAG = "10_qmi8658";

#define BOARD_I2C_FAST_CLK_HZ 400000U
#define BOARD_QMI8658_INT1_GPIO GPIO_NUM_4
#define BOARD_QMI8658_INT2_GPIO GPIO_NUM_5
#define BOARD_QMI8658_ADDR_PRIMARY_7BIT 0x6B
#define BOARD_QMI8658_ADDR_ALT_7BIT 0x6A

#define QMI8658_REG_WHO_AM_I 0x00
#define QMI8658_REG_CTRL1 0x02
#define QMI8658_REG_CTRL2 0x03
#define QMI8658_REG_CTRL7 0x08
#define QMI8658_REG_AX_L 0x35
#define QMI8658_WHO_AM_I_EXPECTED 0x05
#define QMI8658_CTRL1_AUTO_INCREMENT_LE 0x40
#define QMI8658_CTRL2_ACC_ODR_250HZ_RANGE_8G 0x23
#define QMI8658_CTRL7_ENABLE_ACCEL 0x01
#define QMI8658_ACC_LSB_PER_G 4096.0f
#define IMU_SAMPLE_PERIOD_MS 25
#define UI_UPDATE_PERIOD_US 100000LL

typedef struct {
    bool online;
    esp_err_t error;
    uint8_t addr;
    uint8_t who_am_i;
    float ax_g;
    float ay_g;
    float az_g;
    int int1_level;
    int int2_level;
    uint32_t sample_count;
} imu_sample_t;

static lv_display_t *g_disp;
static QueueHandle_t g_sample_queue;
static lv_obj_t *g_status_label;
static lv_obj_t *g_value_label;
static lv_obj_t *g_detail_label;
static lv_obj_t *g_chart;
static lv_chart_series_t *g_ser_x;
static lv_chart_series_t *g_ser_y;
static lv_chart_series_t *g_ser_z;

static int16_t le16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

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

static esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

static void close_i2c(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev)
{
    if (*dev) {
        (void)i2c_master_bus_rm_device(*dev);
        *dev = NULL;
    }
    *bus = NULL;
}

static esp_err_t open_qmi8658(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev, uint8_t *addr, uint8_t *who)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "acquire I2C");
    *bus = bsp_i2c_get_handle();

    uint8_t selected = BOARD_QMI8658_ADDR_PRIMARY_7BIT;
    esp_err_t ret = i2c_master_probe(*bus, selected, 100);
    if (ret != ESP_OK) {
        selected = BOARD_QMI8658_ADDR_ALT_7BIT;
        ret = i2c_master_probe(*bus, selected, 100);
    }
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = selected,
        .scl_speed_hz = BOARD_I2C_FAST_CLK_HZ,
    };
    ret = i2c_master_bus_add_device(*bus, &dev_cfg, dev);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
        return ret;
    }

    ret = i2c_read_reg(*dev, QMI8658_REG_WHO_AM_I, who, 1);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
        return ret;
    }
    if (*who != QMI8658_WHO_AM_I_EXPECTED) {
        ESP_LOGE(TAG, "unexpected WHO_AM_I=0x%02x at 0x%02x", *who, selected);
        close_i2c(bus, dev);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_GOTO_ON_ERROR(i2c_write_reg(*dev, QMI8658_REG_CTRL1, QMI8658_CTRL1_AUTO_INCREMENT_LE), fail, TAG, "set CTRL1");
    ESP_GOTO_ON_ERROR(i2c_write_reg(*dev, QMI8658_REG_CTRL2, QMI8658_CTRL2_ACC_ODR_250HZ_RANGE_8G), fail, TAG, "set CTRL2");
    ESP_GOTO_ON_ERROR(i2c_write_reg(*dev, QMI8658_REG_CTRL7, QMI8658_CTRL7_ENABLE_ACCEL), fail, TAG, "set CTRL7");

    *addr = selected;
    ESP_LOGI(TAG, "QMI8658 online addr=0x%02x WHO_AM_I=0x%02x accel=+/-8g auto-increment little-endian", selected, *who);
    return ESP_OK;

fail:
    close_i2c(bus, dev);
    return ret;
}

static esp_err_t read_accel(i2c_master_dev_handle_t dev, imu_sample_t *sample)
{
    uint8_t raw[6] = {0};
    esp_err_t ret = i2c_read_reg(dev, QMI8658_REG_AX_L, raw, sizeof(raw));
    if (ret != ESP_OK) {
        sample->online = false;
        sample->error = ret;
        return ret;
    }
    sample->online = true;
    sample->error = ESP_OK;
    sample->ax_g = (float)le16(&raw[0]) / QMI8658_ACC_LSB_PER_G;
    sample->ay_g = (float)le16(&raw[2]) / QMI8658_ACC_LSB_PER_G;
    sample->az_g = (float)le16(&raw[4]) / QMI8658_ACC_LSB_PER_G;
    sample->int1_level = gpio_get_level(BOARD_QMI8658_INT1_GPIO);
    sample->int2_level = gpio_get_level(BOARD_QMI8658_INT2_GPIO);
    return ESP_OK;
}

static void imu_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;
    uint8_t addr = 0;
    uint8_t who = 0;
    uint32_t sample_count = 0;

    while (true) {
        imu_sample_t sample = {
            .online = false,
            .error = ESP_OK,
            .addr = addr,
            .who_am_i = who,
            .int1_level = gpio_get_level(BOARD_QMI8658_INT1_GPIO),
            .int2_level = gpio_get_level(BOARD_QMI8658_INT2_GPIO),
        };

        if (!dev) {
            esp_err_t ret = open_qmi8658(&bus, &dev, &addr, &who);
            sample.addr = addr;
            sample.who_am_i = who;
            if (ret != ESP_OK) {
                sample.error = ret;
                ESP_LOGW(TAG, "QMI8658 open/config failed: %s", esp_err_to_name(ret));
                xQueueOverwrite(g_sample_queue, &sample);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        sample.addr = addr;
        sample.who_am_i = who;
        sample.sample_count = ++sample_count;
        esp_err_t ret = read_accel(dev, &sample);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "QMI8658 accel read failed: %s", esp_err_to_name(ret));
            close_i2c(&bus, &dev);
        }
        xQueueOverwrite(g_sample_queue, &sample);
        vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    }
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF5F7FA), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "QMI8658 Accel");
    lv_obj_set_style_text_color(title, lv_color_hex(0x16202A), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 12);

    g_status_label = lv_label_create(screen);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_status_label, 426);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x425466), LV_PART_MAIN);
    lv_label_set_text(g_status_label, "Waiting for QMI8658");
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 20, 44);

    g_chart = lv_chart_create(screen);
    lv_obj_set_size(g_chart, 426, 250);
    lv_obj_align(g_chart, LV_ALIGN_TOP_LEFT, 20, 78);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, 80);
    lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, -12000, 12000);
    lv_obj_set_style_bg_color(g_chart, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(g_chart, lv_color_hex(0xD4DCE5), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_chart, 4, LV_PART_MAIN);
    g_ser_x = lv_chart_add_series(g_chart, lv_color_hex(0xD13B3B), LV_CHART_AXIS_PRIMARY_Y);
    g_ser_y = lv_chart_add_series(g_chart, lv_color_hex(0x1A8E51), LV_CHART_AXIS_PRIMARY_Y);
    g_ser_z = lv_chart_add_series(g_chart, lv_color_hex(0x2868B8), LV_CHART_AXIS_PRIMARY_Y);

    g_value_label = lv_label_create(screen);
    lv_label_set_long_mode(g_value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_value_label, 426);
    lv_obj_set_style_text_color(g_value_label, lv_color_hex(0x17212B), LV_PART_MAIN);
    lv_label_set_text(g_value_label, "X N/A   Y N/A   Z N/A");
    lv_obj_align(g_value_label, LV_ALIGN_TOP_LEFT, 20, 340);

    g_detail_label = lv_label_create(screen);
    lv_label_set_long_mode(g_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_detail_label, 426);
    lv_obj_set_style_text_color(g_detail_label, lv_color_hex(0x425466), LV_PART_MAIN);
    lv_label_set_text(g_detail_label, "INT1/INT2: N/A");
    lv_obj_align(g_detail_label, LV_ALIGN_BOTTOM_LEFT, 20, -24);
}

static void update_ui(const imu_sample_t *sample)
{
    char text[128];
    if (!sample->online || sample->error != ESP_OK) {
        snprintf(text, sizeof(text), "ERROR QMI8658: %s", esp_err_to_name(sample->error));
        lv_label_set_text(g_status_label, text);
        lv_label_set_text(g_value_label, "X N/A   Y N/A   Z N/A");
        snprintf(text, sizeof(text), "Probe 0x%02X/0x%02X | INT1 GPIO%d=%d INT2 GPIO%d=%d",
                 BOARD_QMI8658_ADDR_PRIMARY_7BIT, BOARD_QMI8658_ADDR_ALT_7BIT,
                 BOARD_QMI8658_INT1_GPIO, sample->int1_level,
                 BOARD_QMI8658_INT2_GPIO, sample->int2_level);
        lv_label_set_text(g_detail_label, text);
        return;
    }

    snprintf(text, sizeof(text), "ONLINE addr 0x%02X WHO 0x%02X | +/-8g | sample %lu",
             sample->addr, sample->who_am_i, (unsigned long)sample->sample_count);
    lv_label_set_text(g_status_label, text);
    snprintf(text, sizeof(text), "X %+0.2fg   Y %+0.2fg   Z %+0.2fg", sample->ax_g, sample->ay_g, sample->az_g);
    lv_label_set_text(g_value_label, text);
    snprintf(text, sizeof(text), "INT1 GPIO%d=%d | INT2 GPIO%d=%d | UI 10 Hz, I2C task 40 Hz",
             BOARD_QMI8658_INT1_GPIO, sample->int1_level,
             BOARD_QMI8658_INT2_GPIO, sample->int2_level);
    lv_label_set_text(g_detail_label, text);

    lv_chart_set_next_value(g_chart, g_ser_x, (int32_t)(sample->ax_g * 1000.0f));
    lv_chart_set_next_value(g_chart, g_ser_y, (int32_t)(sample->ay_g * 1000.0f));
    lv_chart_set_next_value(g_chart, g_ser_z, (int32_t)(sample->az_g * 1000.0f));
    lv_chart_refresh(g_chart);
}

static void fail_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "START QMI8658 LVGL accelerometer chart test");
    ESP_ERROR_CHECK(gpio_input(BOARD_QMI8658_INT1_GPIO, false));
    ESP_ERROR_CHECK(gpio_input(BOARD_QMI8658_INT2_GPIO, false));

    g_sample_queue = xQueueCreate(1, sizeof(imu_sample_t));
    if (!g_sample_queue) {
        ESP_LOGE(TAG, "FAIL create IMU queue");
        fail_idle();
    }

    g_disp = bsp_display_start();
    if (!g_disp) {
        ESP_LOGE(TAG, "FAIL LVGL init");
        fail_idle();
    }
    if (bsp_display_lock(-1)) {
        create_ui();
        bsp_display_unlock();
    }

    xTaskCreate(imu_task, "qmi8658_sample", 4096, NULL, 5, NULL);

    imu_sample_t latest = {0};
    bool have_latest = false;
    int64_t last_ui_update = 0;
    while (true) {
        imu_sample_t sample;
        while (xQueueReceive(g_sample_queue, &sample, 0) == pdTRUE) {
            latest = sample;
            have_latest = true;
        }
        int64_t now = esp_timer_get_time();
        if (have_latest && now - last_ui_update >= UI_UPDATE_PERIOD_US) {
            if (bsp_display_lock(-1)) {
                update_ui(&latest);
                bsp_display_unlock();
                last_ui_update = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
