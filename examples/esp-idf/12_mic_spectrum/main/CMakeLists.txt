idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES knob_board esp_codec_dev esp-dsp esp_timer freertos lvgl)
