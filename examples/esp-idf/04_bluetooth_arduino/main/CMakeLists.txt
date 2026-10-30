set(IDF_EXAMPLE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../04_bluetooth/main/main.c")

idf_component_register(SRCS "main.cpp" ${IDF_EXAMPLE_SOURCE}
                       INCLUDE_DIRS "."
                       REQUIRES espressif__arduino-esp32 knob_board knob_wireless bt esp_wifi nvs_flash esp_timer)

set_source_files_properties(${IDF_EXAMPLE_SOURCE}
                            PROPERTIES COMPILE_DEFINITIONS "app_main=idf_example_app_main")
