/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "haptic_service.h"
#include "pomodoro_chime.h"
#include "pomodoro_timer.h"

#define NVS_NAMESPACE   "pomodoro"
/* Turning the knob changes the length many times a second; it is stored once the knob rests */
#define SAVE_DELAY_US   (1500 * 1000)

static const char *TAG = "pomodoro";

static const char *const NVS_KEYS[POMO_PHASE_MAX] = {"focus_s", "short_s", "long_s"};
static const char *const NVS_MINUTE_KEYS[POMO_PHASE_MAX] = {"focus", "short", "long"};   // Before seconds
static const int DEFAULT_SECONDS[POMO_PHASE_MAX] = {25 * 60, 5 * 60, 15 * 60};
static const int MAX_SECONDS[POMO_PHASE_MAX] = {120 * 60, 60 * 60, 60 * 60};
// Three firm pulses: noticeable on a desk, short enough not to rattle
static const uint16_t FINISH_PATTERN[] = {220, 140, 220, 140, 220};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_timer;
static esp_timer_handle_t s_save_timer;
static int s_seconds[POMO_PHASE_MAX];
static bool s_unsaved[POMO_PHASE_MAX];
static pomo_state_t s_state;
static int64_t s_deadline_us;       // While running: when the phase ends

static uint32_t phase_ms(pomo_phase_t phase)
{
    return (uint32_t)s_seconds[phase] * 1000;
}

static void set_phase_locked(pomo_phase_t phase)
{
    s_state.phase = phase;
    s_state.running = false;
    s_state.total_ms = phase_ms(phase);
    s_state.remaining_ms = s_state.total_ms;
}

static pomo_phase_t next_phase_locked(void)
{
    if (s_state.phase != POMO_FOCUS) {
        return POMO_FOCUS;
    }
    s_state.focus_done++;
    if (s_state.focus_done >= POMO_LONG_BREAK_EVERY) {
        s_state.focus_done = 0;
        return POMO_LONG_BREAK;
    }
    return POMO_SHORT_BREAK;
}

static void expire_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    if (!s_state.running) {
        taskEXIT_CRITICAL(&s_lock);
        return;
    }
    const pomo_phase_t finished = s_state.phase;
    set_phase_locked(next_phase_locked());
    s_state.finished = finished;
    s_state.finished_seq++;
    taskEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "%s finished", finished == POMO_FOCUS ? "Focus" : "Break");
    haptic_play(FINISH_PATTERN, sizeof(FINISH_PATTERN) / sizeof(FINISH_PATTERN[0]));
    pomodoro_chime_play();
}

static void load_lengths(void)
{
    nvs_handle_t nvs;
    const bool opened = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK;
    for (int i = 0; i < POMO_PHASE_MAX; i++) {
        int32_t seconds = 0;
        int32_t minutes = 0;
        if (opened && nvs_get_i32(nvs, NVS_KEYS[i], &seconds) != ESP_OK &&
                nvs_get_i32(nvs, NVS_MINUTE_KEYS[i], &minutes) == ESP_OK) {
            seconds = minutes * 60;     // Set before lengths had seconds
        }
        s_seconds[i] = (seconds >= POMO_MIN_SECONDS && seconds <= MAX_SECONDS[i]) ? (int)seconds : DEFAULT_SECONDS[i];
    }
    if (opened) {
        nvs_close(nvs);
    }
}

static void save_cb(void *arg)
{
    (void)arg;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    for (int i = 0; i < POMO_PHASE_MAX; i++) {
        taskENTER_CRITICAL(&s_lock);
        const bool unsaved = s_unsaved[i];
        const int seconds = s_seconds[i];
        s_unsaved[i] = false;
        taskEXIT_CRITICAL(&s_lock);
        if (unsaved) {
            nvs_set_i32(nvs, NVS_KEYS[i], seconds);
        }
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

esp_err_t pomo_init(void)
{
    if (s_timer) {
        return ESP_OK;
    }
    load_lengths();
    set_phase_locked(POMO_FOCUS);
    const esp_timer_create_args_t save_args = {
        .callback = save_cb,
        .name = "pomodoro_save",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&save_args, &s_save_timer), TAG, "Create save timer failed");
    const esp_timer_create_args_t args = {
        .callback = expire_cb,
        .name = "pomodoro",
    };
    return esp_timer_create(&args, &s_timer);
}

void pomo_get_state(pomo_state_t *state)
{
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    *state = s_state;
    if (s_state.running) {
        const int64_t left_us = s_deadline_us - now;
        state->remaining_ms = left_us > 0 ? (uint32_t)(left_us / 1000) : 0;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_state.running || s_state.remaining_ms == 0) {
        taskEXIT_CRITICAL(&s_lock);
        return;
    }
    s_state.running = true;
    s_deadline_us = esp_timer_get_time() + (int64_t)s_state.remaining_ms * 1000;
    const uint64_t timeout_us = (uint64_t)s_state.remaining_ms * 1000;
    taskEXIT_CRITICAL(&s_lock);
    esp_timer_stop(s_timer);
    esp_timer_start_once(s_timer, timeout_us);
}

void pomo_pause(void)
{
    esp_timer_stop(s_timer);
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    if (s_state.running) {
        const int64_t left_us = s_deadline_us - now;
        s_state.remaining_ms = left_us > 0 ? (uint32_t)(left_us / 1000) : 0;
        s_state.running = false;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_reset(void)
{
    esp_timer_stop(s_timer);
    taskENTER_CRITICAL(&s_lock);
    set_phase_locked(s_state.phase);
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_skip(void)
{
    esp_timer_stop(s_timer);
    taskENTER_CRITICAL(&s_lock);
    set_phase_locked(next_phase_locked());
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_select(pomo_phase_t phase)
{
    if (phase >= POMO_PHASE_MAX) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    if (!s_state.running) {
        set_phase_locked(phase);
    }
    taskEXIT_CRITICAL(&s_lock);
}

int pomo_get_seconds(pomo_phase_t phase)
{
    return (phase < POMO_PHASE_MAX) ? s_seconds[phase] : 0;
}

int pomo_max_seconds(pomo_phase_t phase)
{
    return (phase < POMO_PHASE_MAX) ? MAX_SECONDS[phase] : 0;
}

void pomo_set_seconds(pomo_phase_t phase, int seconds)
{
    if (phase >= POMO_PHASE_MAX) {
        return;
    }
    if (seconds < POMO_MIN_SECONDS) {
        seconds = POMO_MIN_SECONDS;
    } else if (seconds > MAX_SECONDS[phase]) {
        seconds = MAX_SECONDS[phase];
    }
    taskENTER_CRITICAL(&s_lock);
    const bool changed = !s_state.running && seconds != s_seconds[phase];
    if (changed) {
        s_seconds[phase] = seconds;
        s_unsaved[phase] = true;
        if (s_state.phase == phase) {
            set_phase_locked(phase);
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    if (changed) {
        esp_timer_stop(s_save_timer);
        esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
    }
}
