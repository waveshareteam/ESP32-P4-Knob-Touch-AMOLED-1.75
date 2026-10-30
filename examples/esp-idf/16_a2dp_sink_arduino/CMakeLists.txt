cmake_minimum_required(VERSION 3.16)

set(PARTITION_TABLE_CUSTOM_FILENAME "${CMAKE_CURRENT_LIST_DIR}/../16_a2dp_sink/partitions.csv")
include("${CMAKE_CURRENT_LIST_DIR}/../common/knob_board/project.cmake")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../common/knob_wireless")
list(REMOVE_ITEM SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(APPEND SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../common/knob_wireless/sdkconfig.defaults.wireless"
    "${CMAKE_CURRENT_LIST_DIR}/../16_a2dp_sink/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
project(a2dp_sink_arduino)
