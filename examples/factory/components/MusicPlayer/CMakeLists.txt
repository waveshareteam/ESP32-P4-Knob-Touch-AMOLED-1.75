idf_component_register(
    SRCS "MusicPlayer.cpp" "music_audio.c" "music_meta.cpp" "assets/img_app_musicplayer.c"
    INCLUDE_DIRS "."
    REQUIRES app_fonts app_services board_app player_ui esp-audio-player text_codec)

set_source_files_properties(
    "MusicPlayer.cpp"
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
