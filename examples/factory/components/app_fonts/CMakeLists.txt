idf_component_register(
    SRCS "app_fonts.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl)

# The CJK fonts ship as relocatable binaries; link them into the app image and map them at run time.
idf_component_get_property(fonts_dir 78__xiaozhi-fonts COMPONENT_DIR)
target_add_binary_data(${COMPONENT_LIB} "${fonts_dir}/cbin/font_noto_sans_common_20_4.bin" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${fonts_dir}/cbin/font_noto_sans_common_30_4.bin" BINARY)
