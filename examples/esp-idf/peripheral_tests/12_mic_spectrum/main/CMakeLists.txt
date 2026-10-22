idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_p4_knob_touch_amoled_1_75 esp_codec_dev esp_driver_gpio esp-dsp esp_timer freertos lvgl)
