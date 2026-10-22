#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

static const char *TAG = "01_system_info";

static bool require_nonzero_size(const char *name, size_t value)
{
    if (value == 0) {
        ESP_LOGE(TAG, "FAIL %s returned 0", name);
        return false;
    }
    return true;
}

static void idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    bool ok = true;
    uint32_t flash_size = 0;

    ESP_LOGI(TAG, "START read-only ESP32-P4 system information check");

    esp_err_t ret = esp_flash_get_size(NULL, &flash_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL esp_flash_get_size: %s", esp_err_to_name(ret));
        ok = false;
    } else {
        ESP_LOGI(TAG, "Flash total: %" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024U * 1024U));
        ok &= require_nonzero_size("flash total", flash_size);
    }

#if CONFIG_SPIRAM
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "FAIL CONFIG_SPIRAM is enabled but PSRAM is not initialized");
        ok = false;
    } else {
        size_t psram_size = esp_psram_get_size();
        size_t psram_heap_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t psram_heap_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "PSRAM configured: enabled");
        ESP_LOGI(TAG, "PSRAM available: %u bytes", (unsigned)psram_size);
        ESP_LOGI(TAG, "PSRAM heap total/free/min: %u / %u / %u bytes",
                 (unsigned)psram_heap_total, (unsigned)psram_heap_free, (unsigned)psram_heap_min);
        ok &= require_nonzero_size("esp_psram_get_size", psram_size);
        ok &= require_nonzero_size("SPIRAM heap total", psram_heap_total);
    }
#else
    ESP_LOGE(TAG, "FAIL PSRAM configured: disabled by CONFIG_SPIRAM");
    ESP_LOGE(TAG, "FAIL PSRAM available: N/A; zero capacity is not treated as a pass condition");
    ok = false;
#endif

    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Internal heap/SRAM total/free/min: %u / %u / %u bytes",
             (unsigned)internal_total, (unsigned)internal_free, (unsigned)internal_min);
    ok &= require_nonzero_size("internal heap total", internal_total);
    ok &= require_nonzero_size("internal heap free", internal_free);
    ok &= require_nonzero_size("internal heap minimum free", internal_min);

    if (ok) {
        ESP_LOGI(TAG, "PASS system information read completed with valid non-zero required values");
    }
    idle_forever();
}
