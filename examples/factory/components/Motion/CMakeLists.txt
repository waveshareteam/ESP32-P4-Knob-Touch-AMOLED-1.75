idf_component_register(
    SRCS "Motion.cpp" "assets/img_app_motion.c"
    INCLUDE_DIRS "."
    REQUIRES app_services board_app
    PRIV_REQUIRES app_fonts nvs_flash esp_timer)

set_source_files_properties(
    "Motion.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
