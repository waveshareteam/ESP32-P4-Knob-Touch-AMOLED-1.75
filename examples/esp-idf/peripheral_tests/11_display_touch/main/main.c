#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "sdkconfig.h"

static const char *TAG = "11_display_touch";
static const int DOT_SIZE = 12;

#define BOARD_BOOT_GPIO GPIO_NUM_35

static lv_display_t *s_disp;
static lv_obj_t *s_count_label;
static lv_obj_t *s_dots[CONFIG_DISPLAY11_DOT_MAX];
static uint32_t s_dot_count;

static volatile bool s_color_stage_active;
static volatile bool s_color_advance_requested;

static esp_err_t board_gpio_input(gpio_num_t gpio, bool pullup)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static bool boot_pressed(void)
{
    return gpio_get_level(BOARD_BOOT_GPIO) == 0;
}

static void color_stage_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (s_color_stage_active && (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED)) {
        s_color_advance_requested = true;
    }
}

static void wait_color_stage(const char *name)
{
    ESP_LOGI(TAG, "phase %s", name);
    const TickType_t start = xTaskGetTickCount();
    const TickType_t duration = pdMS_TO_TICKS(CONFIG_DISPLAY11_COLOR_STAGE_MS);
    s_color_advance_requested = false;
    s_color_stage_active = true;
    while (xTaskGetTickCount() - start < duration) {
        if (s_color_advance_requested || boot_pressed()) {
            ESP_LOGI(TAG, "advance requested during %s", name);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    s_color_stage_active = false;
}

static esp_err_t lvgl_color_stage(const char *name, lv_color_t color)
{
    ESP_LOGI(TAG, "LVGL full-screen %s", name);
    if (!bsp_display_lock(-1)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, color, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, color_stage_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, color_stage_event_cb, LV_EVENT_CLICKED, NULL);
    bsp_display_unlock();
    wait_color_stage(name);
    return ESP_OK;
}

static int clamp_coord(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void update_count_label(void)
{
    if (s_count_label) {
        lv_label_set_text_fmt(s_count_label, "Dots: %lu", (unsigned long)s_dot_count);
    }
}

static void clear_dots(void)
{
    for (int i = 0; i < CONFIG_DISPLAY11_DOT_MAX; ++i) {
        if (s_dots[i]) {
            lv_obj_delete(s_dots[i]);
            s_dots[i] = NULL;
        }
    }
    s_dot_count = 0;
    update_count_label();
    ESP_LOGI(TAG, "touch dots cleared");
}

static void add_dot(lv_point_t point)
{
    if (s_dot_count >= CONFIG_DISPLAY11_DOT_MAX) {
        lv_obj_delete(s_dots[0]);
        for (int i = 1; i < CONFIG_DISPLAY11_DOT_MAX; ++i) {
            s_dots[i - 1] = s_dots[i];
        }
        s_dots[CONFIG_DISPLAY11_DOT_MAX - 1] = NULL;
        s_dot_count--;
    }

    int x = clamp_coord(point.x - DOT_SIZE / 2, 0, BSP_LCD_H_RES - DOT_SIZE);
    int y = clamp_coord(point.y - DOT_SIZE / 2, 0, BSP_LCD_V_RES - DOT_SIZE);
    lv_obj_t *dot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x111827), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    s_dots[s_dot_count++] = dot;
    update_count_label();
    ESP_LOGI(TAG, "touch dot %lu x=%d y=%d", (unsigned long)s_dot_count, point.x, point.y);
}

static void screen_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    add_dot(point);
}

static void clear_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        clear_dots();
    }
}

static void create_touch_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    s_count_label = NULL;
    for (int i = 0; i < CONFIG_DISPLAY11_DOT_MAX; ++i) {
        s_dots[i] = NULL;
    }
    s_dot_count = 0;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_PRESSING, NULL);

    s_count_label = lv_label_create(scr);
    lv_label_set_text(s_count_label, "Dots: 0");
    lv_obj_set_width(s_count_label, 170);
    lv_obj_set_pos(s_count_label, 16, 16);
    lv_obj_set_style_text_color(s_count_label, lv_color_hex(0x111827), 0);

    lv_obj_t *button = lv_button_create(scr);
    lv_obj_set_size(button, 96, 44);
    lv_obj_set_pos(button, BSP_LCD_H_RES - 112, 12);
    lv_obj_add_event_cb(button, clear_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Clear");
    lv_obj_center(label);
}

void app_main(void)
{
    ESP_LOGI(TAG, "START single BSP LVGL display/touch test LCD_RST=GPIO%d TOUCH_RST=GPIO%d",
             BSP_LCD_RST, BSP_LCD_TOUCH_RST);
    esp_err_t ret = board_gpio_input(BOARD_BOOT_GPIO, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BOOT GPIO input setup failed: %s", esp_err_to_name(ret));
    }

    s_disp = bsp_display_start();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "FAIL BSP LVGL init");
        return;
    }

    ret = lvgl_color_stage("red", lv_color_hex(0xFF0000));
    ESP_ERROR_CHECK(ret);
    ret = lvgl_color_stage("green", lv_color_hex(0x00FF00));
    ESP_ERROR_CHECK(ret);
    ret = lvgl_color_stage("blue", lv_color_hex(0x0000FF));
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "color stages complete; keeping the same BSP LVGL display for touch drawing page");

    if (bsp_display_lock(-1)) {
        create_touch_ui();
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "READY white touch drawing page");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
