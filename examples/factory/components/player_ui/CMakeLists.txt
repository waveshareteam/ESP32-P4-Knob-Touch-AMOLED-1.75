idf_component_register(
    SRCS "now_playing_view.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES app_fonts board_app esp_new_jpeg)
