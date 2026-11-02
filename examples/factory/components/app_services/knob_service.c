/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_board_periph.h"
#include "periph_gpio.h"
#include "board_app.h"
#include "haptic_service.h"
#include "knob_service.h"

#define KNOB_PERIPH_CW      "gpio_knob_cw"
#define KNOB_PERIPH_CCW     "gpio_knob_ccw"
/* Measured on the knob: a detent holds its switch closed for 5 ms or more and detents are
 * at least 12 ms apart, while contact bounce and the brief touch of the opposite switch
 * when a detent releases last well under 1 ms. A level that holds for 2 ms is real. */
#define KNOB_SETTLE_US      (2 * 1000)
#define KNOB_MAX_LISTENERS  4
#define KNOB_TASK_STACK     3072
#define KNOB_TASK_PRIORITY  4

typedef struct {
    const char *periph_name;
    int8_t step;
    gpio_num_t gpio;
    uint32_t last_edge_us;          // Time of the latest edge; guarded by s_pin_lock
    bool settling;                  // An edge has not been confirmed yet; guarded by s_pin_lock
    bool pressed;                   // Confirmed state, owned by the knob task
} knob_pin_t;

typedef struct {
    knob_step_cb_t cb;
    void *user_ctx;
} knob_listener_t;

static const char *TAG = "knob";

static knob_pin_t s_pins[] = {
    {.periph_name = KNOB_PERIPH_CW, .step = 1, .gpio = GPIO_NUM_NC},
    {.periph_name = KNOB_PERIPH_CCW, .step = -1, .gpio = GPIO_NUM_NC},
};
#define KNOB_PIN_COUNT (sizeof(s_pins) / sizeof(s_pins[0]))

static TaskHandle_t s_task;
static portMUX_TYPE s_pin_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_listeners_lock = portMUX_INITIALIZER_UNLOCKED;
static knob_listener_t s_listeners[KNOB_MAX_LISTENERS];
static int s_listener_count;

/* Every edge only restarts the settle time; the knob task decides once the level holds */
static void IRAM_ATTR knob_isr(void *arg)
{
    knob_pin_t *pin = (knob_pin_t *)arg;
    const uint32_t now = (uint32_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_pin_lock);
    pin->last_edge_us = now;
    pin->settling = true;
    portEXIT_CRITICAL_ISR(&s_pin_lock);

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static bool top_listener(knob_listener_t *out)
{
    taskENTER_CRITICAL(&s_listeners_lock);
    bool found = s_listener_count > 0;
    if (found) {
        *out = s_listeners[s_listener_count - 1];
    }
    taskEXIT_CRITICAL(&s_listeners_lock);
    return found;
}

static void deliver(int steps)
{
    ESP_LOGI(TAG, "Knob %+d", steps);
    if (!board_display_lock(-1)) {
        return;
    }
    knob_listener_t listener;
    if (top_listener(&listener)) {
        listener.cb(steps, listener.user_ctx);
    }
    board_display_unlock();
}

/* Confirms pins whose level held for the settle time; returns the ticks until the next check */
static TickType_t confirm_pins(int *steps)
{
    uint32_t wait_us = UINT32_MAX;
    for (size_t i = 0; i < KNOB_PIN_COUNT; i++) {
        knob_pin_t *pin = &s_pins[i];
        taskENTER_CRITICAL(&s_pin_lock);
        const bool settling = pin->settling;
        const uint32_t now = (uint32_t)esp_timer_get_time();
        const uint32_t held_us = now - pin->last_edge_us;  // Wraps safely every 71 minutes
        const bool ready = settling && held_us >= KNOB_SETTLE_US;
        bool pressed = false;
        if (ready) {
            pin->settling = false;
            // Keep the stability decision and GPIO confirmation in the same
            // critical section as the ISR's latest-edge update.
            pressed = gpio_get_level(pin->gpio) == 0;
        }
        taskEXIT_CRITICAL(&s_pin_lock);

        if (settling && !ready) {
            const uint32_t left = KNOB_SETTLE_US - held_us;
            wait_us = (left < wait_us) ? left : wait_us;
            continue;
        }
        if (!ready) {
            continue;
        }
        // A detent pulls its switch low; it counts once, when it is first held down
        if (pressed && !pin->pressed) {
            *steps += pin->step;
        }
        pin->pressed = pressed;
    }
    if (wait_us == UINT32_MAX) {
        return portMAX_DELAY;
    }
    const TickType_t ticks = pdMS_TO_TICKS((wait_us + 999) / 1000);
    return ticks > 0 ? ticks : 1;
}

static void knob_task(void *arg)
{
    (void)arg;
    TickType_t wait = portMAX_DELAY;
    while (true) {
        ulTaskNotifyTake(pdTRUE, wait);
        int steps = 0;
        wait = confirm_pins(&steps);
        // Detents confirmed together, or while the UI was busy, arrive as one movement
        if (steps != 0) {
            // Clicks for every detent, whoever listens, before waiting for the UI
            haptic_knob_tick();
            deliver(steps);
        }
    }
}

esp_err_t knob_service_init(void)
{
    if (s_task) {
        return ESP_OK;
    }

    for (size_t i = 0; i < KNOB_PIN_COUNT; i++) {
        // The Board Manager configured the pin as a pulled-up input interrupting on both edges
        periph_gpio_handle_t *handle = NULL;
        ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(s_pins[i].periph_name, (void **)&handle), TAG,
                            "%s missing", s_pins[i].periph_name);
        s_pins[i].gpio = handle->gpio_num;
        s_pins[i].pressed = gpio_get_level(s_pins[i].gpio) == 0;
    }

    ESP_RETURN_ON_FALSE(xTaskCreate(knob_task, "knob", KNOB_TASK_STACK, NULL, KNOB_TASK_PRIORITY, &s_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Create task failed");

    for (size_t i = 0; i < KNOB_PIN_COUNT; i++) {
        // The touch driver normally installed the shared GPIO ISR service already
        esp_err_t ret = gpio_isr_handler_add(s_pins[i].gpio, knob_isr, &s_pins[i]);
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Install GPIO ISR service failed");
            ret = gpio_isr_handler_add(s_pins[i].gpio, knob_isr, &s_pins[i]);
        }
        ESP_RETURN_ON_ERROR(ret, TAG, "Add GPIO%d handler failed", s_pins[i].gpio);
    }
    ESP_LOGI(TAG, "Knob on GPIO%d (clockwise) and GPIO%d (counter-clockwise)", s_pins[0].gpio, s_pins[1].gpio);
    return ESP_OK;
}

esp_err_t knob_service_attach(knob_step_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "Invalid callback");

    esp_err_t ret = ESP_ERR_NO_MEM;
    taskENTER_CRITICAL(&s_listeners_lock);
    if (s_listener_count < KNOB_MAX_LISTENERS) {
        s_listeners[s_listener_count++] = (knob_listener_t) {
            .cb = cb, .user_ctx = user_ctx
        };
        ret = ESP_OK;
    }
    taskEXIT_CRITICAL(&s_listeners_lock);
    return ret;
}

void knob_service_detach(knob_step_cb_t cb, void *user_ctx)
{
    taskENTER_CRITICAL(&s_listeners_lock);
    for (int i = s_listener_count - 1; i >= 0; i--) {
        if (s_listeners[i].cb == cb && s_listeners[i].user_ctx == user_ctx) {
            memmove(&s_listeners[i], &s_listeners[i + 1], (s_listener_count - i - 1) * sizeof(s_listeners[0]));
            s_listener_count--;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_listeners_lock);
}
