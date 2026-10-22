idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_p4_knob_touch_amoled_1_75 fatfs sdmmc esp_driver_sdmmc esp_driver_gpio esp_timer lvgl)
