#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "14_usb_display";

void app_main(void)
{
    ESP_LOGW(TAG, "RESERVED");
    ESP_LOGW(TAG, "NOT IMPLEMENTED");
    ESP_LOGW(TAG, "UNTESTED");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}