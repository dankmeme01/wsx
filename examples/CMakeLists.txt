file(GLOB ARC_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(example_src ${ARC_EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_src} NAME_WE)
    add_executable(wsx_${example_name} ${example_src})

    target_link_libraries(wsx_${example_name} PRIVATE wsx)
    set_target_properties(wsx_${example_name} PROPERTIES FOLDER "examples")
endforeach()
