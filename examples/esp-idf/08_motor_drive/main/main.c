#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "sdkconfig.h"

static const char *TAG = "08_motor_drive";

#define HAPTIC_IN1_GPIO GPIO_NUM_22
#define HAPTIC_IN2_GPIO GPIO_NUM_23

typedef enum {
    MOTOR_CMD_START,
    MOTOR_CMD_STOP,
    MOTOR_CMD_TIMEOUT,
} motor_cmd_t;

typedef enum {
    MOTOR_STATE_IDLE,
    MOTOR_STATE_VIBRATING,
    MOTOR_STATE_FAULT,
} motor_state_t;

typedef struct {
    motor_state_t state;
    uint32_t remaining_ms;
    char error[64];
} motor_ui_update_t;

static QueueHandle_t s_motor_cmd_queue;
static QueueHandle_t s_ui_queue;
static esp_timer_handle_t s_guard_timer;
static lv_display_t *s_disp;

static const char *state_name(motor_state_t state)
{
    switch (state) {
    case MOTOR_STATE_IDLE:
        return "IDLE";
    case MOTOR_STATE_VIBRATING:
        return "VIBRATING";
    case MOTOR_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

static void post_ui(motor_state_t state, uint32_t remaining_ms, const char *error)
{
    if (!s_ui_queue) {
        return;
    }
    motor_ui_update_t update = {
        .state = state,
        .remaining_ms = remaining_ms,
    };
    if (error) {
        snprintf(update.error, sizeof(update.error), "%s", error);
    } else {
        snprintf(update.error, sizeof(update.error), "-");
    }
    (void)xQueueOverwrite(s_ui_queue, &update);
}

static void enqueue_cmd(motor_cmd_t cmd)
{
    if (s_motor_cmd_queue) {
        (void)xQueueSend(s_motor_cmd_queue, &cmd, 0);
    }
}

static void guard_timer_cb(void *arg)
{
    (void)arg;
    enqueue_cmd(MOTOR_CMD_TIMEOUT);
}

static esp_err_t haptic_gpio_output_level(gpio_num_t gpio, int level)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    return gpio_set_level(gpio, level ? 1 : 0);
}

static esp_err_t force_idle_gpio(void)
{
    esp_err_t first = gpio_set_level(HAPTIC_IN1_GPIO, 0);
    esp_err_t second = gpio_set_level(HAPTIC_IN2_GPIO, 0);
    return first != ESP_OK ? first : second;
}

static esp_err_t motor_forward(void)
{
    esp_err_t ret = gpio_set_level(HAPTIC_IN2_GPIO, 0);
    if (ret != ESP_OK) {
        (void)force_idle_gpio();
        return ret;
    }
    ret = gpio_set_level(HAPTIC_IN1_GPIO, 1);
    if (ret != ESP_OK) {
        (void)force_idle_gpio();
    }
    return ret;
}

static void motor_task(void *arg)
{
    (void)arg;
    motor_state_t state = MOTOR_STATE_IDLE;
    int64_t deadline_us = 0;
    char last_error[64] = "-";

    esp_err_t ret = haptic_gpio_output_level(HAPTIC_IN1_GPIO, 0);
    if (ret == ESP_OK) {
        ret = haptic_gpio_output_level(HAPTIC_IN2_GPIO, 0);
    }
    if (ret != ESP_OK) {
        snprintf(last_error, sizeof(last_error), "gpio init: %s", esp_err_to_name(ret));
        state = MOTOR_STATE_FAULT;
        (void)force_idle_gpio();
        ESP_LOGE(TAG, "FAULT %s", last_error);
    } else {
        ESP_LOGI(TAG, "DRV8837 ready IN1=GPIO%d IN2=GPIO%d idle=low/low",
                 HAPTIC_IN1_GPIO, HAPTIC_IN2_GPIO);
    }
    post_ui(state, 0, last_error);

    while (1) {
        motor_cmd_t cmd;
        TickType_t wait_ticks = state == MOTOR_STATE_VIBRATING ? pdMS_TO_TICKS(50) : portMAX_DELAY;
        if (xQueueReceive(s_motor_cmd_queue, &cmd, wait_ticks) == pdTRUE) {
            if (state != MOTOR_STATE_FAULT && cmd == MOTOR_CMD_START) {
                ret = motor_forward();
                if (ret != ESP_OK) {
                    snprintf(last_error, sizeof(last_error), "gpio drive: %s", esp_err_to_name(ret));
                    state = MOTOR_STATE_FAULT;
                    ESP_LOGE(TAG, "FAULT %s", last_error);
                    (void)force_idle_gpio();
                    (void)esp_timer_stop(s_guard_timer);
                    post_ui(state, 0, last_error);
                    continue;
                }
                ret = esp_timer_stop(s_guard_timer);
                if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                    snprintf(last_error, sizeof(last_error), "timer stop: %s", esp_err_to_name(ret));
                    state = MOTOR_STATE_FAULT;
                    (void)force_idle_gpio();
                    post_ui(state, 0, last_error);
                    continue;
                }
                ret = esp_timer_start_once(s_guard_timer, CONFIG_MOTOR08_MAX_ON_MS * 1000ULL);
                if (ret != ESP_OK) {
                    snprintf(last_error, sizeof(last_error), "timer start: %s", esp_err_to_name(ret));
                    state = MOTOR_STATE_FAULT;
                    (void)force_idle_gpio();
                    post_ui(state, 0, last_error);
                    continue;
                }
                deadline_us = esp_timer_get_time() + CONFIG_MOTOR08_MAX_ON_MS * 1000LL;
                state = MOTOR_STATE_VIBRATING;
                snprintf(last_error, sizeof(last_error), "-");
                ESP_LOGI(TAG, "VIBRATING forward guard=%d ms", CONFIG_MOTOR08_MAX_ON_MS);
            } else if (cmd == MOTOR_CMD_STOP || cmd == MOTOR_CMD_TIMEOUT) {
                if (state == MOTOR_STATE_VIBRATING) {
                    if (cmd == MOTOR_CMD_TIMEOUT) {
                        ESP_LOGI(TAG, "guard timeout stop");
                    }
                    (void)esp_timer_stop(s_guard_timer);
                    ret = force_idle_gpio();
                    if (ret != ESP_OK) {
                        snprintf(last_error, sizeof(last_error), "idle: %s", esp_err_to_name(ret));
                        state = MOTOR_STATE_FAULT;
                    } else {
                        state = MOTOR_STATE_IDLE;
                        deadline_us = 0;
                        snprintf(last_error, sizeof(last_error), "-");
                    }
                }
            }
        }

        uint32_t remaining_ms = 0;
        if (state == MOTOR_STATE_VIBRATING) {
            int64_t now = esp_timer_get_time();
            if (now >= deadline_us) {
                enqueue_cmd(MOTOR_CMD_TIMEOUT);
            } else {
                remaining_ms = (uint32_t)((deadline_us - now + 999) / 1000);
            }
        }
        post_ui(state, remaining_ms, last_error);
    }
}

static void button_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        enqueue_cmd(MOTOR_CMD_START);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DELETE) {
        enqueue_cmd(MOTOR_CMD_STOP);
    }
}

static lv_obj_t *new_label(lv_obj_t *parent, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1F2933), 0);
    return label;
}

static void ui_task(void *arg)
{
    (void)arg;
    s_disp = bsp_display_start();
    if (!s_disp) {
        ESP_LOGE(TAG, "LVGL init failed");
        vTaskDelete(NULL);
    }

    lv_obj_t *status_label = NULL;
    lv_obj_t *remaining_label = NULL;
    lv_obj_t *error_label = NULL;
    if (bsp_display_lock(-1)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0xF7F8FA), 0);
        lv_obj_set_style_pad_all(scr, 18, 0);
        lv_obj_set_style_pad_gap(scr, 10, 0);
        lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *title = new_label(scr, BSP_LCD_H_RES - 36);
        lv_label_set_text(title, "08 Motor Drive");
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        status_label = new_label(scr, BSP_LCD_H_RES - 36);
        remaining_label = new_label(scr, BSP_LCD_H_RES - 36);
        error_label = new_label(scr, BSP_LCD_H_RES - 36);
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(remaining_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(error_label, lv_color_hex(0x9A3412), 0);

        lv_obj_t *button = lv_button_create(scr);
        lv_obj_set_size(button, 220, 84);
        lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_t *button_label = lv_label_create(button);
        lv_label_set_text(button_label, "VIBRATE");
        lv_obj_center(button_label);

        lv_label_set_text(status_label, "State: IDLE");
        lv_label_set_text(remaining_label, "Remaining: 0 ms");
        lv_label_set_text(error_label, "Last error: -");
        bsp_display_unlock();
    }

    while (1) {
        motor_ui_update_t update;
        if (xQueueReceive(s_ui_queue, &update, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (bsp_display_lock(-1)) {
                lv_label_set_text_fmt(status_label, "State: %s", state_name(update.state));
                lv_label_set_text_fmt(remaining_label, "Remaining: %lu ms", (unsigned long)update.remaining_ms);
                lv_label_set_text_fmt(error_label, "Last error: %s", update.error);
                bsp_display_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    s_motor_cmd_queue = xQueueCreate(6, sizeof(motor_cmd_t));
    s_ui_queue = xQueueCreate(1, sizeof(motor_ui_update_t));
    if (!s_motor_cmd_queue || !s_ui_queue) {
        ESP_LOGE(TAG, "FAIL create queues");
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = guard_timer_cb,
        .name = "motor_guard",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_guard_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL create guard timer: %s", esp_err_to_name(ret));
        return;
    }

    xTaskCreate(motor_task, "motor_worker", 4096, NULL, 5, NULL);
    xTaskCreate(ui_task, "motor_ui", 8192, NULL, 4, NULL);
}
