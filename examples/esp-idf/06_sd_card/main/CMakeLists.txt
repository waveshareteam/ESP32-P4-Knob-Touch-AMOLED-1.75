idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board fatfs sdmmc esp_driver_sdmmc esp_driver_gpio esp_timer lvgl)
