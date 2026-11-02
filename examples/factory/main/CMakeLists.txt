idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)

# Function to get component library
function(get_component_library component_name output_var)
    # Get the exact component name
    idf_build_get_property(build_components BUILD_COMPONENTS)
    set(TARGET_COMPONENT "")
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${component_name}" OR COMPONENT MATCHES "espressif__${component_name}")
            set(TARGET_COMPONENT ${COMPONENT})
            break()
        endif()
    endforeach()

    # Get the component library
    if(TARGET_COMPONENT STREQUAL "")
        message(FATAL_ERROR "Component '${component_name}' not found.")
    else()
        idf_component_get_property(COMPONENT_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
        set(${output_var} ${COMPONENT_LIB} PARENT_SCOPE)
    endif()
endfunction()

# get_component_library("lvgl" LVGL_LIB)
# target_compile_options(
#     ${LVGL_LIB}
#     PUBLIC
#         "-Wno-attributes"
# )

# Pack the SPIFFS image from a copy without placeholder dotfiles such as
# .gitkeep, which apps would otherwise list as media files.
set(spiffs_src "${CMAKE_CURRENT_LIST_DIR}/../spiffs")
set(spiffs_stage "${CMAKE_BINARY_DIR}/spiffs_image")
file(GLOB_RECURSE spiffs_files CONFIGURE_DEPENDS "${spiffs_src}/*")
file(REMOVE_RECURSE "${spiffs_stage}")
file(COPY "${spiffs_src}/" DESTINATION "${spiffs_stage}" PATTERN ".*" EXCLUDE)
spiffs_create_partition_image(storage "${spiffs_stage}" FLASH_IN_PROJECT)
