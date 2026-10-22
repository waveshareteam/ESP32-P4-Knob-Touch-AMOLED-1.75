idf_component_register(
    SRCS "esp32_p4_knob_touch_amoled_1_75.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_sdmmc esp_driver_i2s
    PRIV_REQUIRES esp_lcd usb spiffs fatfs esp_driver_ledc
)
