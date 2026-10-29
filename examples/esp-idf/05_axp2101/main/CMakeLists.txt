idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board lvgl esp_driver_i2c)
