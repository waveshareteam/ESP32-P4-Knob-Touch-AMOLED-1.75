#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#define BOARD_BOOT_GPIO GPIO_NUM_35
#define BOARD_PWR_GPIO GPIO_NUM_2

static const char *TAG = "02_buttons";
static const uint32_t DEBOUNCE_MS = 30;

typedef struct {
    gpio_num_t gpio;
    uint32_t debounce_ms;
    int stable_level;
    int last_level;
    uint32_t last_change_ms;
    bool initialized;
} board_gpio_debounce_t;

static esp_err_t validate_gpio(gpio_num_t gpio)
{
    return gpio >= 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t board_gpio_input_ex(gpio_num_t gpio, bool pullup, bool pulldown)
{
    ESP_RETURN_ON_ERROR(validate_gpio(gpio), "board_gpio", "invalid gpio");
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t board_gpio_input(gpio_num_t gpio, bool pullup)
{
    return board_gpio_input_ex(gpio, pullup, false);
}

static uint32_t board_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t board_gpio_debounce_init(board_gpio_debounce_t *state, gpio_num_t gpio, bool pullup, uint32_t debounce_ms)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(board_gpio_input(gpio, pullup), "board_gpio", "input config failed");
    int level = gpio_get_level(gpio);
    state->gpio = gpio;
    state->debounce_ms = debounce_ms;
    state->stable_level = level;
    state->last_level = level;
    state->last_change_ms = board_millis();
    state->initialized = true;
    return ESP_OK;
}

static esp_err_t board_gpio_debounce_update(board_gpio_debounce_t *state, bool *changed, int *stable_level)
{
    if (!state || !state->initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    int raw = gpio_get_level(state->gpio);
    uint32_t now = board_millis();
    bool did_change = false;

    if (raw != state->last_level) {
        state->last_level = raw;
        state->last_change_ms = now;
    }
    if (raw != state->stable_level && now - state->last_change_ms >= state->debounce_ms) {
        state->stable_level = raw;
        did_change = true;
    }
    if (changed) {
        *changed = did_change;
    }
    if (stable_level) {
        *stable_level = state->stable_level;
    }
    return ESP_OK;
}

static const char *board_gpio_level_name(int level)
{
    return level ? "HIGH" : "LOW";
}

typedef struct {
    const char *name;
    gpio_num_t gpio;
    int active_level;
    board_gpio_debounce_t debounce;
    uint32_t press_count;
} button_state_t;

static const char *event_name(const button_state_t *button, int level)
{
    return level == button->active_level ? "PRESS" : "RELEASE";
}

static bool button_init(button_state_t *button)
{
    esp_err_t ret = board_gpio_debounce_init(&button->debounce, button->gpio, true, DEBOUNCE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL configure %s GPIO%d input: %s", button->name, button->gpio, esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "%s GPIO%d initial level=%s state=%s count=%lu",
             button->name, button->gpio,
             board_gpio_level_name(button->debounce.stable_level),
             event_name(button, button->debounce.stable_level),
             (unsigned long)button->press_count);
    return true;
}

void app_main(void)
{
    button_state_t buttons[] = {
        {.name = "BOOT", .gpio = BOARD_BOOT_GPIO, .active_level = 0},
        {.name = "PWR/GPIO2", .gpio = BOARD_PWR_GPIO, .active_level = 0},
    };

    ESP_LOGI(TAG, "START read-only active-low button test; inputs use the validated pull-up baseline");
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        if (!button_init(&buttons[i])) {
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }
    }
    ESP_LOGI(TAG, "READY press BOOT and PWR/GPIO2 independently");

    while (true) {
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
            bool changed = false;
            int level = 0;
            esp_err_t ret = board_gpio_debounce_update(&buttons[i].debounce, &changed, &level);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "FAIL poll %s GPIO%d: %s", buttons[i].name, buttons[i].gpio, esp_err_to_name(ret));
                continue;
            }
            if (changed) {
                if (level == buttons[i].active_level) {
                    buttons[i].press_count++;
                }
                ESP_LOGI(TAG, "%s GPIO%d %s level=%s count=%lu",
                         buttons[i].name, buttons[i].gpio, event_name(&buttons[i], level),
                         board_gpio_level_name(level), (unsigned long)buttons[i].press_count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
