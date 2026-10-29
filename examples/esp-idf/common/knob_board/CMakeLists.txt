# Generate Board Manager tables at configure time so a standard idf.py build
# needs no separate board-selection command.
set(KNOB_BOARD_NAME "esp32_p4_knob_touch_amoled_1_75")
set(KNOB_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/../waveshare_boards/boards/${KNOB_BOARD_NAME}")
set(KNOB_BOARD_GEN_DIR "${CMAKE_BINARY_DIR}/knob_board/components/gen_bmgr_codes")

set(knob_board_srcs "knob_board.c")
set(knob_board_includes "include")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(knob_bmgr_dir espressif__esp_board_manager COMPONENT_DIR)
    file(GLOB knob_board_inputs CONFIGURE_DEPENDS "${KNOB_BOARD_DIR}/*.yaml" "${KNOB_BOARD_DIR}/*.board")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${knob_board_inputs})
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/knob_board")
    idf_build_get_property(knob_python PYTHON)
    execute_process(
        COMMAND ${knob_python} "${knob_bmgr_dir}/gen_bmgr_config_codes.py"
                -b "${KNOB_BOARD_DIR}"
                --project-dir "${CMAKE_BINARY_DIR}/knob_board"
                --skip-sdkconfig-check
                --log-level WARNING
        RESULT_VARIABLE knob_board_gen_result
        OUTPUT_VARIABLE knob_board_gen_output
        ERROR_VARIABLE knob_board_gen_output)
    if(NOT knob_board_gen_result EQUAL 0)
        message(FATAL_ERROR "knob_board: Board Manager code generation failed:\n${knob_board_gen_output}")
    endif()
    file(GLOB knob_board_gen_srcs "${KNOB_BOARD_GEN_DIR}/*.c")
    list(APPEND knob_board_srcs ${knob_board_gen_srcs} "${KNOB_BOARD_DIR}/setup_device.c")
    list(APPEND knob_board_includes "${KNOB_BOARD_GEN_DIR}")
endif()

idf_component_register(
    SRCS ${knob_board_srcs}
    INCLUDE_DIRS ${knob_board_includes}
    REQUIRES esp_board_manager esp_codec_dev esp_lcd esp_lcd_touch esp_lvgl_adapter
             esp_driver_i2c esp_driver_i2s
    PRIV_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst9217)

# Board descriptors and generated tables are discovered by linker sections.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
