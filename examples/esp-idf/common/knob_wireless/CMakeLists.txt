idf_component_register(SRCS "knob_wireless.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_board_manager esp-extconn)

# Keep the custom-device entry in the final image for Board Manager discovery.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
