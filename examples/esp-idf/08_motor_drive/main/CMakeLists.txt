idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board esp_driver_gpio esp_timer lvgl)
