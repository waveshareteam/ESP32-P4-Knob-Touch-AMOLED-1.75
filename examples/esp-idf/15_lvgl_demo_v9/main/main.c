#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "knob_board.h"
#include "lv_demos.h"

void app_main(void)
{
    knob_board_display_config_t cfg = {
        .adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .enable_touch = true,
    };
    ESP_ERROR_CHECK(knob_board_prepare());
    ESP_ERROR_CHECK(knob_board_display_start(&cfg) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(knob_board_display_brightness_set(100));

    ESP_ERROR_CHECK(knob_board_display_lock(-1) ? ESP_OK : ESP_ERR_TIMEOUT);

    // lv_demo_music();
    lv_demo_benchmark();
    // lv_demo_widgets();

    knob_board_display_unlock();
}
