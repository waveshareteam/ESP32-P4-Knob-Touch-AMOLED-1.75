# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../common/knob_board/project.cmake")
list(APPEND EXTRA_COMPONENT_DIRS "${KNOB_BOARD_COMMON_DIR}/knob_wireless")
list(REMOVE_ITEM SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(APPEND SDKCONFIG_DEFAULTS
    "${KNOB_BOARD_COMMON_DIR}/knob_wireless/sdkconfig.defaults.wireless"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
project(a2dp_sink)
