cmake_minimum_required(VERSION 3.16)
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/examples/esp-idf/16_a2dp_sink/components/esp32_p4_knob_touch_amoled_1_75"
    "${PROJECT_ROOT}/examples/esp-idf/16_a2dp_sink/components/esp_lcd_co5300"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(05_axp2101)
