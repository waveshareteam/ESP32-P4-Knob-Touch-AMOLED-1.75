idf_component_register(
    SRCS "UsbScreen.cpp" "assets/img_app_usbscreen.c"
    INCLUDE_DIRS "."
    REQUIRES app_services board_app esp_driver_jpeg esp_driver_ppa esp_lcd esp_lcd_touch
    PRIV_REQUIRES app_fonts esp_lvgl_adapter esp_mm esp_timer)

set_source_files_properties(
    "UsbScreen.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
