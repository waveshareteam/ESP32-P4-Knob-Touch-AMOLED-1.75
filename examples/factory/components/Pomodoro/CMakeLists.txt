idf_component_register(
    SRCS "Pomodoro.cpp" "pomodoro_timer.c" "pomodoro_chime.c" "assets/img_app_pomodoro.c"
    INCLUDE_DIRS "."
    REQUIRES app_fonts app_services board_app
    PRIV_REQUIRES esp_timer nvs_flash)

set_source_files_properties(
    "Pomodoro.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
