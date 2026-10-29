idf_component_register(SRCS "main.c" "usb_descriptors.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board esp_driver_jpeg esp_driver_ppa esp_hw_support esp_mm
                       PRIV_REQUIRES tinyusb lvgl)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})

# ESP-IDF v5.5 provides usb_phy through the usb component. It moved to
# esp_hw_support in v6, which is listed above; optional_requires keeps the
# older public header available without naming a removed v6 component.
idf_component_optional_requires(PRIVATE usb)
