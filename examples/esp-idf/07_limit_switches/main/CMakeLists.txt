idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_p4_knob_touch_amoled_1_75 lvgl esp_timer esp_driver_gpio esp_driver_pcnt esp_system)
