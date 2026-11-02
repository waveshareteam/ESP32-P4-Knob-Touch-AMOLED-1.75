idf_component_register(
    SRCS "Reader.cpp" "page_layout.cpp" "text_source.c" "assets/img_app_reader.c"
    INCLUDE_DIRS "."
    REQUIRES app_services
    PRIV_REQUIRES app_fonts board_app nvs_flash text_codec)

set_source_files_properties(
    "Reader.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
