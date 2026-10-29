# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Board hardware defaults come first so this example's sdkconfig.defaults can override them.
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/components/waveshare_boards/boards/esp32_p4_knob_touch_amoled_1_75/sdkconfig.defaults.board"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-brookesia)

# Keep esp-boost 0.3.x on C++23 until its Proto sources support C++26.
idf_component_get_property(ESP_BOOST_LIB espressif__esp-boost COMPONENT_LIB)
target_compile_options(${ESP_BOOST_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>"
)

# Bluedroid's bt_types.h trips GCC's stringop-truncation check at -O2; keep it a warning.
idf_component_get_property(BT_LIB bt COMPONENT_LIB)
target_compile_options(${BT_LIB} PRIVATE "-Wno-error=stringop-truncation")

# mcp-c-sdk 2.0.1 (Xiaozhi) has the same strncpy pattern; keep it a warning there too.
idf_component_get_property(MCP_LIB espressif__mcp-c-sdk COMPONENT_LIB)
target_compile_options(${MCP_LIB} PRIVATE "-Wno-error=stringop-truncation")
