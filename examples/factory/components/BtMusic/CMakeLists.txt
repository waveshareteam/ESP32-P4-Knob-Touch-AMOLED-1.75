idf_component_register(
    SRCS "BtMusic.cpp" "assets/img_app_btmusic.c"
    INCLUDE_DIRS "."
    REQUIRES app_fonts app_services player_ui)

set_source_files_properties(
    "BtMusic.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
