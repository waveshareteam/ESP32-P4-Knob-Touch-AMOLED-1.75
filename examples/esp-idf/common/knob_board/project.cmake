# Shared Board Manager setup for ordinary ESP-IDF examples.
# Include this before ESP-IDF's project.cmake.
get_filename_component(KNOB_BOARD_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
list(APPEND EXTRA_COMPONENT_DIRS
    "${KNOB_BOARD_COMMON_DIR}/knob_board"
    "${KNOB_BOARD_COMMON_DIR}/waveshare_boards")

set(SDKCONFIG_DEFAULTS
    "${KNOB_BOARD_COMMON_DIR}/waveshare_boards/boards/esp32_p4_knob_touch_amoled_1_75/sdkconfig.defaults.board"
    "${KNOB_BOARD_COMMON_DIR}/knob_board/sdkconfig.defaults"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
