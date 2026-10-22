set(LV_DEMO_DIR ../managed_components/lvgl__lvgl/demos)
file(GLOB_RECURSE LV_DEMOS_SOURCES ${LV_DEMO_DIR}/*.c)

idf_component_register(SRCS "bt_app_av.c"
                            "audio_mono_mix.c"
                            "bt_app_core.c"
                            "main.c"
                            ${LV_DEMOS_SOURCES}
                    PRIV_REQUIRES esp_driver_i2s esp32_p4_knob_touch_amoled_1_75 bt nvs_flash esp_ringbuf esp_driver_dac driver esp_driver_jpeg esp_timer bsp_extra
                    INCLUDE_DIRS "." ${LV_DEMO_DIR})
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
