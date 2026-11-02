/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "nvs.h"
#include "hal/systimer_ll.h"
#include "esp_board_periph.h"
#include "periph_gpio.h"
#include "haptic_service.h"

#define HAPTIC_PERIPH_IN1       "gpio_haptic_in1"
#define HAPTIC_PERIPH_IN2       "gpio_haptic_in2"
/* The motor keeps spinning down, and disturbing the board, for a moment after it is cut */
#define HAPTIC_GUARD_TAIL_MS    200
#define HAPTIC_GUARD_PERIOD_US  500
/* SYSTIMER counts the 40 MHz XTAL through its fixed 2.5 divider */
#define SYSTIMER_TICKS_PER_US   16
#define NVS_NAMESPACE           "haptic"
#define NVS_KEY_KNOB            "knob"
#define NVS_KEY_STRENGTH        "knob_lvl"

/* A knob click is a kick, then braking stops the motor before it hums. The motor needs some
 * 30 ms to spin up, so a stronger click mostly means a longer kick. */
static const uint16_t KNOB_CLICKS[HAPTIC_KNOB_STRENGTHS][2] = {
    {22, 12},   // Light
    {35, 15},   // Medium
    {50, 18},   // Strong
};

static const char *TAG = "haptic";

static gpio_num_t s_in1 = GPIO_NUM_NC;
static gpio_num_t s_in2 = GPIO_NUM_NC;
static esp_timer_handle_t s_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_pattern[HAPTIC_MAX_STEPS];
static size_t s_count;
static size_t s_step;
static bool s_brake;                // Pauses brake the motor instead of letting it coast
static bool s_tick;                 // The pattern is a knob click, which the next one may replace
static bool s_knob_enabled = true;
static int s_knob_strength = 2;

/* A spinning motor disturbs this board enough that both SYSTIMER counters now and then jump
 * back by seconds. The FreeRTOS tick alarms then wait for a time that is seconds away and the
 * interrupt watchdog resets the chip after 800 ms. While the motor runs, a GPTimer checks every
 * 500 us that the counters only move forward and puts one that went back where it belongs. */
static gptimer_handle_t s_guard;
static SemaphoreHandle_t s_guard_mutex;
static bool s_guard_running;
static uint64_t s_guard_last[SYSTIMER_LL_COUNTER_NUM];
static uint32_t s_guard_last_cycles;
static uint32_t s_cpu_cycles_per_us;
static volatile uint32_t s_guard_repairs;

static uint64_t IRAM_ATTR systimer_counter_read(uint32_t unit)
{
    systimer_ll_counter_snapshot(&SYSTIMER, unit);
    while (!systimer_ll_is_counter_value_valid(&SYSTIMER, unit)) {
    }
    // LO, HI, then LO again: another reader may take a new snapshot in between
    uint32_t lo;
    uint32_t hi;
    uint32_t lo_start = systimer_ll_get_counter_value_low(&SYSTIMER, unit);
    do {
        lo = lo_start;
        hi = systimer_ll_get_counter_value_high(&SYSTIMER, unit);
        lo_start = systimer_ll_get_counter_value_low(&SYSTIMER, unit);
    } while (lo_start != lo);
    return ((uint64_t)hi << 32) | lo;
}

static void guard_sample(void)
{
    s_guard_last_cycles = esp_cpu_get_cycle_count();
    for (uint32_t unit = 0; unit < SYSTIMER_LL_COUNTER_NUM; unit++) {
        s_guard_last[unit] = systimer_counter_read(unit);
    }
}

static bool IRAM_ATTR guard_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;
    // The CPU cycle counter runs from the CPU PLL and keeps the true elapsed time
    const uint32_t cycles = esp_cpu_get_cycle_count();
    const uint64_t elapsed = (uint64_t)(cycles - s_guard_last_cycles) * SYSTIMER_TICKS_PER_US / s_cpu_cycles_per_us;
    s_guard_last_cycles = cycles;
    for (uint32_t unit = 0; unit < SYSTIMER_LL_COUNTER_NUM; unit++) {
        uint64_t value = systimer_counter_read(unit);
        if (value < s_guard_last[unit]) {
            // A missed alarm fires as soon as the counter passes it again
            value = s_guard_last[unit] + elapsed;
            systimer_ll_set_counter_value(&SYSTIMER, unit, value);
            systimer_ll_apply_counter_value(&SYSTIMER, unit);
            s_guard_repairs++;
        }
        s_guard_last[unit] = value;
    }
    return false;
}

static void guard_start(void)
{
    if (!s_guard) {
        return;
    }
    xSemaphoreTake(s_guard_mutex, portMAX_DELAY);
    if (!s_guard_running) {
        guard_sample();
        s_guard_running = gptimer_start(s_guard) == ESP_OK;
    }
    xSemaphoreGive(s_guard_mutex);
}

static void guard_stop(void)
{
    if (!s_guard) {
        return;
    }
    xSemaphoreTake(s_guard_mutex, portMAX_DELAY);
    if (s_guard_running) {
        gptimer_stop(s_guard);
        s_guard_running = false;
    }
    const uint32_t repairs = s_guard_repairs;
    s_guard_repairs = 0;
    xSemaphoreGive(s_guard_mutex);
    if (repairs) {
        ESP_LOGW(TAG, "Put the system timer back %" PRIu32 " time(s) while vibrating", repairs);
    }
}

static esp_err_t guard_init(void)
{
    s_cpu_cycles_per_us = esp_rom_get_cpu_ticks_per_us();
    s_guard_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_guard_mutex, ESP_ERR_NO_MEM, TAG, "No memory for guard mutex");

    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&config, &s_guard), TAG, "Create guard timer failed");
    const gptimer_alarm_config_t alarm = {
        .alarm_count = HAPTIC_GUARD_PERIOD_US,
        .flags.auto_reload_on_alarm = true,
    };
    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = guard_cb,
    };
    esp_err_t ret = gptimer_set_alarm_action(s_guard, &alarm);
    if (ret == ESP_OK) {
        ret = gptimer_register_event_callbacks(s_guard, &callbacks, NULL);
    }
    if (ret == ESP_OK) {
        ret = gptimer_enable(s_guard);
    }
    if (ret != ESP_OK) {
        gptimer_del_timer(s_guard);
        s_guard = NULL;
    }
    return ret;
}

/* IN1 high with IN2 low spins the motor; both low lets it stop */
static void motor_set(bool on)
{
    gpio_set_level(s_in2, 0);
    gpio_set_level(s_in1, on ? 1 : 0);
}

/* Both high shorts the motor through the low sides of the DRV8837, which stops it quickly */
static void motor_brake(void)
{
    gpio_set_level(s_in1, 1);
    gpio_set_level(s_in2, 1);
}

static void step_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    const size_t step = s_step++;
    const size_t count = s_count;
    const uint16_t duration = step < count ? s_pattern[step] : 0;
    const bool brake = s_brake;
    taskEXIT_CRITICAL(&s_lock);

    if (step < count) {
        // Even steps are on times, odd steps are pauses
        if ((step % 2) == 0) {
            motor_set(true);
        } else if (brake) {
            motor_brake();
        } else {
            motor_set(false);
        }
        esp_timer_start_once(s_timer, (uint64_t)duration * 1000);
    } else if (step == count) {
        motor_set(false);
        esp_timer_start_once(s_timer, HAPTIC_GUARD_TAIL_MS * 1000);
    } else {
        guard_stop();
    }
}

esp_err_t haptic_service_init(void)
{
    if (s_timer) {
        return ESP_OK;
    }
    periph_gpio_handle_t *in1 = NULL;
    periph_gpio_handle_t *in2 = NULL;
    ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(HAPTIC_PERIPH_IN1, (void **)&in1), TAG, "%s missing",
                        HAPTIC_PERIPH_IN1);
    ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(HAPTIC_PERIPH_IN2, (void **)&in2), TAG, "%s missing",
                        HAPTIC_PERIPH_IN2);
    s_in1 = in1->gpio_num;
    s_in2 = in2->gpio_num;
    motor_set(false);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t enabled = 1;
        nvs_get_u8(nvs, NVS_KEY_KNOB, &enabled);
        s_knob_enabled = enabled != 0;
        uint8_t strength = s_knob_strength;
        nvs_get_u8(nvs, NVS_KEY_STRENGTH, &strength);
        if (strength >= 1 && strength <= HAPTIC_KNOB_STRENGTHS) {
            s_knob_strength = strength;
        }
        nvs_close(nvs);
    }

    if (guard_init() != ESP_OK) {
        ESP_LOGW(TAG, "System timer guard unavailable; vibration may reset the board");
    }
    const esp_timer_create_args_t args = {
        .callback = step_cb,
        .name = "haptic",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_timer), TAG, "Create timer failed");
    ESP_LOGI(TAG, "Vibration motor on GPIO%d/GPIO%d, knob clicks %s, strength %d", s_in1, s_in2,
             s_knob_enabled ? "on" : "off", s_knob_strength);
    return ESP_OK;
}

static void start_pattern(const uint16_t *pattern_ms, size_t count, bool brake, bool tick)
{
    esp_timer_stop(s_timer);
    taskENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < count; i++) {
        // The motor is only rated for short pulses
        s_pattern[i] = ((i % 2) == 0 && pattern_ms[i] > HAPTIC_MAX_ON_MS) ? HAPTIC_MAX_ON_MS : pattern_ms[i];
    }
    s_count = count;
    s_step = 0;
    s_brake = brake;
    s_tick = tick;
    taskEXIT_CRITICAL(&s_lock);
    guard_start();
    step_cb(NULL);
}

esp_err_t haptic_play(const uint16_t *pattern_ms, size_t count)
{
    ESP_RETURN_ON_FALSE(s_timer, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    ESP_RETURN_ON_FALSE(pattern_ms && count > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid pattern");
    if (count > HAPTIC_MAX_STEPS) {
        count = HAPTIC_MAX_STEPS;
    }
    start_pattern(pattern_ms, count, false, false);
    return ESP_OK;
}

void haptic_stop(void)
{
    if (!s_timer) {
        return;
    }
    esp_timer_stop(s_timer);
    taskENTER_CRITICAL(&s_lock);
    s_count = 0;
    s_step = 0;
    taskEXIT_CRITICAL(&s_lock);
    // Cuts the motor and keeps the guard on until it has spun down
    step_cb(NULL);
}

bool haptic_knob_enabled(void)
{
    return s_knob_enabled;
}

static void save_u8(const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, key, value);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void haptic_set_knob_enabled(bool enabled)
{
    if (enabled == s_knob_enabled) {
        return;
    }
    s_knob_enabled = enabled;
    save_u8(NVS_KEY_KNOB, enabled);
    ESP_LOGI(TAG, "Knob clicks %s", enabled ? "on" : "off");
}

int haptic_knob_strength(void)
{
    return s_knob_strength;
}

void haptic_set_knob_strength(int strength)
{
    if (strength < 1 || strength > HAPTIC_KNOB_STRENGTHS || strength == s_knob_strength) {
        return;
    }
    s_knob_strength = strength;
    save_u8(NVS_KEY_STRENGTH, strength);
    ESP_LOGI(TAG, "Knob click strength %d", strength);
}

/* Plays a click unless a pattern is running: any pattern, or only an alert when `restart` */
static void knob_click(bool restart)
{
    if (!s_timer || !s_knob_enabled) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    // A pattern is still running until its last step has ended
    const bool running = s_count > 0 && s_step <= s_count;
    const bool busy = running && (!restart || !s_tick);
    taskEXIT_CRITICAL(&s_lock);
    if (!busy) {
        start_pattern(KNOB_CLICKS[s_knob_strength - 1], 2, true, true);
    }
}

void haptic_knob_tick(void)
{
    // Turning fast, detents come quicker than clicks; skipping some keeps them apart instead of one long buzz
    knob_click(false);
}

void haptic_knob_preview(void)
{
    knob_click(true);
}
