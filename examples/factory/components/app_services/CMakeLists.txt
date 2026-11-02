idf_component_register(
    SRCS "audio_service.c" "bt_service.c" "haptic_service.c" "imu_service.c" "knob_service.c" "power_service.c" "storage_service.c"
         "time_service.c" "usb_display.c" "usb_service.c" "wifi_service.c" "usb/usb_descriptors.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "usb"
    REQUIRES esp_driver_sdmmc sdmmc
    PRIV_REQUIRES board_app bt esp_board_manager esp_driver_gpio esp_driver_i2c esp_event esp_netif esp_wifi lwip esp_driver_gptimer esp_hal_systimer esp_ringbuf fatfs nvs_flash esp_hw_support esp_timer tinyusb esp_driver_jpeg usb_device_uac
)

# TinyUSB takes its configuration from this component and calls back into it
# for descriptors, mass-storage and display requests. The audio class settings
# come from usb_device_uac, which serves the screen's speaker and microphone.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uac_dir usb_device_uac COMPONENT_DIR)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/usb" "${uac_dir}/tusb_uac")
cmake_policy(SET CMP0079 NEW)
target_link_libraries(${tusb_lib} PRIVATE ${COMPONENT_LIB})
