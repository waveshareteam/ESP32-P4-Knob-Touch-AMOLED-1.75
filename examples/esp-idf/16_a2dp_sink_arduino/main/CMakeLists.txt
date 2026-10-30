set(IDF_EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../16_a2dp_sink/main")
set(IDF_EXAMPLE_MAIN "${IDF_EXAMPLE_DIR}/main.c")

idf_component_register(SRCS "main.cpp"
                            "${IDF_EXAMPLE_DIR}/bt_app_av.c"
                            "${IDF_EXAMPLE_DIR}/audio_mono_mix.c"
                            "${IDF_EXAMPLE_DIR}/bt_app_core.c"
                            ${IDF_EXAMPLE_MAIN}
                       INCLUDE_DIRS "." ${IDF_EXAMPLE_DIR}
                       PRIV_REQUIRES espressif__arduino-esp32 bt nvs_flash esp_ringbuf esp_timer esp_driver_gpio
                                     knob_board knob_wireless esp_codec_dev)

set_source_files_properties(${IDF_EXAMPLE_MAIN}
                            PROPERTIES COMPILE_DEFINITIONS "app_main=idf_example_app_main")

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_USE_DEMO_MUSIC)
