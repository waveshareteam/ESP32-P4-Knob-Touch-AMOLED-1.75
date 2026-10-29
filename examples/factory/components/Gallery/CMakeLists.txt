idf_component_register(
    SRCS "Gallery.cpp" "gallery_decoder.c" "assets/img_app_gallery.c"
    INCLUDE_DIRS "."
    REQUIRES app_services
    PRIV_REQUIRES app_fonts board_app esp_driver_jpeg)

set_source_files_properties(
    "Gallery.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
