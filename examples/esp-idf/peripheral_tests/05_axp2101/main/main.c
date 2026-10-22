#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"

static const char *TAG = "05_axp2101";

#define AXP2101_ADDR_7BIT 0x34
#define AXP2101_I2C_CLK_HZ 400000U
#define AXP2101_SAMPLE_PERIOD_MS 1000
#define AXP2101_CHIP_ID_A 0x47
#define AXP2101_CHIP_ID_B 0x4A
#define AXP2101_REG_STATUS1 0x00
#define AXP2101_REG_STATUS2 0x01
#define AXP2101_REG_IC_TYPE 0x03
#define AXP2101_REG_BATT_ADC_H 0x34
#define AXP2101_REG_BATT_ADC_L 0x35
#define AXP2101_REG_VBUS_ADC_H 0x38
#define AXP2101_REG_VBUS_ADC_L 0x39
#define AXP2101_REG_VSYS_ADC_H 0x3A
#define AXP2101_REG_VSYS_ADC_L 0x3B
#define AXP2101_REG_TEMP_ADC_H 0x3C
#define AXP2101_REG_TEMP_ADC_L 0x3D
#define AXP2101_REG_BAT_PERCENT 0xA4

typedef struct {
    bool online;
    esp_err_t error;
    uint8_t chip_id;
    uint8_t status1;
    uint8_t status2;
    bool vbus_in;
    bool vbus_good;
    bool battery_connected;
    bool charging;
    bool discharging;
    bool standby;
    uint8_t charger_status;
    uint16_t vbus_mv;
    uint16_t batt_mv;
    uint16_t sys_mv;
    int battery_percent;
    float temperature_c;
    bool vbus_mv_valid;
    bool batt_mv_valid;
    bool sys_mv_valid;
    bool percent_valid;
    bool temperature_valid;
} pmu_sample_t;

static lv_display_t *g_disp;
static QueueHandle_t g_sample_queue;
static lv_obj_t *g_status_label;
static lv_obj_t *g_chip_label;
static lv_obj_t *g_vbus_label;
static lv_obj_t *g_batt_label;
static lv_obj_t *g_sys_label;
static lv_obj_t *g_charge_label;
static lv_obj_t *g_temp_label;
static lv_obj_t *g_percent_label;
static lv_obj_t *g_note_label;

static esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 100);
}

static esp_err_t read_u8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_read_reg(dev, reg, value, 1);
}

static esp_err_t read_h6l8(i2c_master_dev_handle_t dev, uint8_t high_reg, uint8_t low_reg, uint16_t *value)
{
    uint8_t high = 0;
    uint8_t low = 0;
    ESP_RETURN_ON_ERROR(read_u8(dev, high_reg, &high), TAG, "read high ADC byte");
    ESP_RETURN_ON_ERROR(read_u8(dev, low_reg, &low), TAG, "read low ADC byte");
    *value = (uint16_t)(((uint16_t)(high & 0x3F) << 8) | low);
    return ESP_OK;
}

static esp_err_t read_h5l8(i2c_master_dev_handle_t dev, uint8_t high_reg, uint8_t low_reg, uint16_t *value)
{
    uint8_t high = 0;
    uint8_t low = 0;
    ESP_RETURN_ON_ERROR(read_u8(dev, high_reg, &high), TAG, "read high ADC byte");
    ESP_RETURN_ON_ERROR(read_u8(dev, low_reg, &low), TAG, "read low ADC byte");
    *value = (uint16_t)(((uint16_t)(high & 0x1F) << 8) | low);
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
    esp_err_t ret = i2c_master_probe(*bus, AXP2101_ADDR_7BIT, 100);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
        return ret;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR_7BIT,
        .scl_speed_hz = AXP2101_I2C_CLK_HZ,
    };
    ret = i2c_master_bus_add_device(*bus, &dev_cfg, dev);
    if (ret != ESP_OK) {
        close_i2c(bus, dev);
    }
    return ret;
}

static esp_err_t read_pmu(i2c_master_dev_handle_t dev, pmu_sample_t *sample)
{
    sample->online = false;
    sample->error = ESP_OK;

    ESP_RETURN_ON_ERROR(read_u8(dev, AXP2101_REG_IC_TYPE, &sample->chip_id), TAG, "read chip id");
    if (sample->chip_id != AXP2101_CHIP_ID_A && sample->chip_id != AXP2101_CHIP_ID_B) {
        sample->error = ESP_ERR_INVALID_RESPONSE;
        return sample->error;
    }

    ESP_RETURN_ON_ERROR(read_u8(dev, AXP2101_REG_STATUS1, &sample->status1), TAG, "read status1");
    ESP_RETURN_ON_ERROR(read_u8(dev, AXP2101_REG_STATUS2, &sample->status2), TAG, "read status2");

    sample->vbus_good = (sample->status1 & (1U << 5)) != 0;
    sample->battery_connected = (sample->status1 & (1U << 3)) != 0;
    sample->vbus_in = ((sample->status2 & (1U << 3)) == 0) && sample->vbus_good;
    uint8_t charge_mode = (sample->status2 >> 5) & 0x07;
    sample->charging = charge_mode == 1;
    sample->discharging = charge_mode == 2;
    sample->standby = charge_mode == 0;
    sample->charger_status = sample->status2 & 0x07;

    ESP_RETURN_ON_ERROR(read_h6l8(dev, AXP2101_REG_VSYS_ADC_H, AXP2101_REG_VSYS_ADC_L, &sample->sys_mv), TAG, "read system voltage");
    sample->sys_mv_valid = sample->sys_mv > 0;

    ESP_RETURN_ON_ERROR(read_h6l8(dev, AXP2101_REG_TEMP_ADC_H, AXP2101_REG_TEMP_ADC_L, &sample->vbus_mv), TAG, "read temperature raw");
    uint16_t temp_raw = sample->vbus_mv;
    sample->temperature_c = 22.0f + (7274.0f - (float)temp_raw) / 20.0f;
    sample->temperature_valid = temp_raw <= 16383 && isfinite(sample->temperature_c) &&
                                sample->temperature_c > -40.0f && sample->temperature_c < 125.0f;

    if (sample->vbus_in) {
        ESP_RETURN_ON_ERROR(read_h6l8(dev, AXP2101_REG_VBUS_ADC_H, AXP2101_REG_VBUS_ADC_L, &sample->vbus_mv), TAG, "read VBUS voltage");
        sample->vbus_mv_valid = sample->vbus_mv > 0;
    }
    if (sample->battery_connected) {
        ESP_RETURN_ON_ERROR(read_h5l8(dev, AXP2101_REG_BATT_ADC_H, AXP2101_REG_BATT_ADC_L, &sample->batt_mv), TAG, "read battery voltage");
        sample->batt_mv_valid = sample->batt_mv > 0;
        uint8_t percent = 0;
        ESP_RETURN_ON_ERROR(read_u8(dev, AXP2101_REG_BAT_PERCENT, &percent), TAG, "read battery percent");
        sample->battery_percent = percent;
        sample->percent_valid = percent <= 100;
    }

    sample->online = true;
    return ESP_OK;
}

static const char *charge_state_text(const pmu_sample_t *sample)
{
    if (sample->charging) {
        return "charging";
    }
    if (sample->discharging) {
        return "discharging";
    }
    if (sample->standby) {
        return "standby";
    }
    return "not charging";
}

static const char *charger_status_text(uint8_t status)
{
    switch (status) {
    case 0: return "tri";
    case 1: return "pre";
    case 2: return "cc";
    case 3: return "cv";
    case 4: return "done";
    case 5: return "stop";
    default: return "N/A";
    }
}

static void pmu_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;

    while (true) {
        pmu_sample_t sample = {
            .online = false,
            .error = ESP_OK,
            .battery_percent = -1,
            .temperature_c = NAN,
        };

        if (!dev) {
            esp_err_t ret = open_i2c(&bus, &dev);
            if (ret != ESP_OK) {
                sample.error = ret;
                ESP_LOGW(TAG, "AXP2101 open failed: %s", esp_err_to_name(ret));
                xQueueOverwrite(g_sample_queue, &sample);
                vTaskDelay(pdMS_TO_TICKS(AXP2101_SAMPLE_PERIOD_MS));
                continue;
            }
            ESP_LOGI(TAG, "AXP2101 I2C online at 0x%02x", AXP2101_ADDR_7BIT);
        }

        esp_err_t ret = read_pmu(dev, &sample);
        if (ret != ESP_OK) {
            sample.error = ret;
            ESP_LOGW(TAG, "AXP2101 read failed: %s", esp_err_to_name(ret));
            close_i2c(&bus, &dev);
        }
        xQueueOverwrite(g_sample_queue, &sample);
        vTaskDelay(pdMS_TO_TICKS(AXP2101_SAMPLE_PERIOD_MS));
    }
}

static lv_obj_t *make_line_label(lv_obj_t *screen, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 426);
    lv_obj_set_style_text_color(label, lv_color_hex(0x25313D), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, y);
    return label;
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF6F8FB), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AXP2101 PMU");
    lv_obj_set_style_text_color(title, lv_color_hex(0x17202A), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 14);

    g_status_label = make_line_label(screen, 50, "Waiting for PMU sample");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x425466), LV_PART_MAIN);
    g_chip_label = make_line_label(screen, 88, "Chip: N/A");
    g_vbus_label = make_line_label(screen, 126, "VBUS: N/A");
    g_batt_label = make_line_label(screen, 164, "Battery: N/A");
    g_sys_label = make_line_label(screen, 202, "System: N/A");
    g_charge_label = make_line_label(screen, 240, "Charge: N/A");
    g_temp_label = make_line_label(screen, 278, "Temperature: N/A");
    g_percent_label = make_line_label(screen, 316, "Percent: N/A");
    g_note_label = make_line_label(screen, 384, "Read-only: PMU configuration writes are not used");
    lv_obj_set_style_text_color(g_note_label, lv_color_hex(0x596775), LV_PART_MAIN);
}

static void update_ui(const pmu_sample_t *sample)
{
    char text[128];
    if (!sample->online || sample->error != ESP_OK) {
        snprintf(text, sizeof(text), "ERROR AXP2101 0x%02X: %s", AXP2101_ADDR_7BIT, esp_err_to_name(sample->error));
        lv_label_set_text(g_status_label, text);
        lv_label_set_text(g_chip_label, "Chip: N/A");
        lv_label_set_text(g_vbus_label, "VBUS: N/A");
        lv_label_set_text(g_batt_label, "Battery: N/A");
        lv_label_set_text(g_sys_label, "System: N/A");
        lv_label_set_text(g_charge_label, "Charge: N/A");
        lv_label_set_text(g_temp_label, "Temperature: N/A");
        lv_label_set_text(g_percent_label, "Percent: N/A");
        return;
    }

    snprintf(text, sizeof(text), "ONLINE AXP2101 0x%02X | read-only", AXP2101_ADDR_7BIT);
    lv_label_set_text(g_status_label, text);
    snprintf(text, sizeof(text), "Chip: ID 0x%02X", sample->chip_id);
    lv_label_set_text(g_chip_label, text);
    if (sample->vbus_mv_valid) {
        snprintf(text, sizeof(text), "VBUS: %u mV | in=%d good=%d", sample->vbus_mv, sample->vbus_in, sample->vbus_good);
    } else {
        snprintf(text, sizeof(text), "VBUS: N/A | in=%d good=%d", sample->vbus_in, sample->vbus_good);
    }
    lv_label_set_text(g_vbus_label, text);
    if (sample->batt_mv_valid) {
        snprintf(text, sizeof(text), "Battery: %u mV | present=%d", sample->batt_mv, sample->battery_connected);
    } else {
        snprintf(text, sizeof(text), "Battery: N/A | present=%d", sample->battery_connected);
    }
    lv_label_set_text(g_batt_label, text);
    if (sample->sys_mv_valid) {
        snprintf(text, sizeof(text), "System: %u mV", sample->sys_mv);
    } else {
        snprintf(text, sizeof(text), "System: N/A");
    }
    lv_label_set_text(g_sys_label, text);
    snprintf(text, sizeof(text), "Charge: %s | status=%s", charge_state_text(sample), charger_status_text(sample->charger_status));
    lv_label_set_text(g_charge_label, text);
    if (sample->temperature_valid) {
        snprintf(text, sizeof(text), "Temperature: %.1f C", sample->temperature_c);
    } else {
        snprintf(text, sizeof(text), "Temperature: N/A");
    }
    lv_label_set_text(g_temp_label, text);
    if (sample->percent_valid) {
        snprintf(text, sizeof(text), "Percent: %d%%", sample->battery_percent);
    } else {
        snprintf(text, sizeof(text), "Percent: N/A");
    }
    lv_label_set_text(g_percent_label, text);
}

static void fail_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "START AXP2101 read-only LVGL status test");

    g_sample_queue = xQueueCreate(1, sizeof(pmu_sample_t));
    if (!g_sample_queue) {
        ESP_LOGE(TAG, "FAIL create PMU queue");
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

    xTaskCreate(pmu_task, "axp2101_sample", 4096, NULL, 5, NULL);

    while (true) {
        pmu_sample_t sample;
        if (xQueueReceive(g_sample_queue, &sample, 0) == pdTRUE) {
            if (bsp_display_lock(-1)) {
                update_ui(&sample);
                bsp_display_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}