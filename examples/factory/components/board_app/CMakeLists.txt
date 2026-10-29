# Board Manager tables are generated from the board YAML at configure time so a
# plain `idf.py build` works without a separate `idf.py bmgr` step. A project
# that already ran `idf.py bmgr` keeps its own components/gen_bmgr_codes.
set(BOARD_APP_BOARD_NAME "esp32_p4_knob_touch_amoled_1_75")
set(BOARD_APP_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf/common/waveshare_boards/boards/${BOARD_APP_BOARD_NAME}")
set(BOARD_APP_GEN_DIR "${CMAKE_BINARY_DIR}/board_app/components/gen_bmgr_codes")

set(board_app_srcs "board_app.c" "board_audio.c")
set(board_app_includes "include")

if(EXISTS "${CMAKE_SOURCE_DIR}/components/gen_bmgr_codes")
    message(STATUS "board_app: using project components/gen_bmgr_codes")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        # Those tables are only refreshed by `idf.py bmgr`; refuse to build stale ones.
        file(GLOB board_app_yaml CONFIGURE_DEPENDS "${BOARD_APP_BOARD_DIR}/*.yaml")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${board_app_yaml})
        foreach(yaml IN LISTS board_app_yaml)
            if("${yaml}" IS_NEWER_THAN "${CMAKE_SOURCE_DIR}/components/gen_bmgr_codes/gen_board_device_config.c")
                message(FATAL_ERROR "board_app: ${yaml} changed; run `idf.py bmgr -b ${BOARD_APP_BOARD_NAME}` again")
            endif()
        endforeach()
    endif()
elseif(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(bmgr_dir espressif__esp_board_manager COMPONENT_DIR)
    file(GLOB board_app_gen_inputs CONFIGURE_DEPENDS "${BOARD_APP_BOARD_DIR}/*.yaml" "${BOARD_APP_BOARD_DIR}/*.board")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${board_app_gen_inputs})

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/board_app")
    idf_build_get_property(python PYTHON)
    execute_process(
        COMMAND ${python} "${bmgr_dir}/gen_bmgr_config_codes.py"
                -b "${BOARD_APP_BOARD_DIR}"
                --project-dir "${CMAKE_BINARY_DIR}/board_app"
                --skip-sdkconfig-check
                --log-level WARNING
        RESULT_VARIABLE gen_result
        OUTPUT_VARIABLE gen_output
        ERROR_VARIABLE gen_output
    )
    if(NOT gen_result EQUAL 0)
        message(FATAL_ERROR "Board Manager code generation failed:\n${gen_output}")
    endif()

    # The generated defaults must match the static board selection in sdkconfig.defaults.
    file(STRINGS "${BOARD_APP_GEN_DIR}/board_manager.defaults" bmgr_support_lines
         REGEX "^CONFIG_ESP_BOARD_(PERIPH|DEV)_[A-Z0-9_]+_SUPPORT=y$")
    foreach(line IN LISTS bmgr_support_lines)
        string(REGEX REPLACE "^CONFIG_(.*)=y$" "\\1" symbol "${line}")
        if(NOT CONFIG_${symbol})
            message(FATAL_ERROR "board_app: board needs CONFIG_${symbol}=y; add it to sdkconfig.defaults")
        endif()
    endforeach()

    file(GLOB board_app_gen_srcs "${BOARD_APP_GEN_DIR}/*.c")
    list(APPEND board_app_srcs ${board_app_gen_srcs} "${BOARD_APP_BOARD_DIR}/setup_device.c")
    list(APPEND board_app_includes "${BOARD_APP_GEN_DIR}")
endif()

idf_component_register(
    SRCS ${board_app_srcs}
    INCLUDE_DIRS ${board_app_includes}
    REQUIRES esp_board_manager esp_codec_dev esp_lcd esp_lcd_touch esp_lvgl_adapter
             esp_driver_i2c esp_driver_i2s
    PRIV_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst9217 spiffs
)

# Board descriptor tables are only referenced from esp_board_manager.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
