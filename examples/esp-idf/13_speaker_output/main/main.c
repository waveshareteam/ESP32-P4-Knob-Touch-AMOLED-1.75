#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define AUDIO_PA_GPIO GPIO_NUM_53

static const char *TAG = "13_speaker_output";

void app_main(void)
{
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << AUDIO_PA_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&pa_cfg);
    if (ret == ESP_OK) {
        ret = gpio_set_level(AUDIO_PA_GPIO, 0);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PA force-off failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "RESERVED");
    ESP_LOGW(TAG, "NOT IMPLEMENTED");
    ESP_LOGW(TAG, "UNTESTED");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
