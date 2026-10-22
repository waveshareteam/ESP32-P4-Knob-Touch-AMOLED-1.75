# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-brookesia)

# Keep esp-boost 0.3.x on C++23 until its Proto sources support C++26.
idf_component_get_property(ESP_BOOST_LIB espressif__esp-boost COMPONENT_LIB)
target_compile_options(${ESP_BOOST_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>"
)
