set(IDF_EXAMPLE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../03_wifi/main/main.c")

idf_component_register(SRCS "main.cpp" ${IDF_EXAMPLE_SOURCE}
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32 knob_board knob_wireless esp_wifi esp_event esp_netif nvs_flash)

# The original source keeps its own app_main. Rename only that translation unit
# and invoke it from Arduino setup() in a separate FreeRTOS task.
set_source_files_properties(${IDF_EXAMPLE_SOURCE}
                            PROPERTIES COMPILE_DEFINITIONS "app_main=idf_example_app_main")
