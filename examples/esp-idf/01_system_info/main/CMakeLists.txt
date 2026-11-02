idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board spi_flash esp_psram heap)
