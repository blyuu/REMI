add_executable(REMIMazeEscape main.cpp MazeHud.cpp)
target_link_libraries(REMIMazeEscape PRIVATE REMI::Runtime REMI::Renderer REMI::Resources
    REMI::Physics REMI::Assets REMI::DebugUI remi_build_options gdi32)
set_target_properties(REMIMazeEscape PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
add_dependencies(REMIMazeEscape remi_sandbox_shaders)
add_custom_command(TARGET REMIMazeEscape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:REMIMazeEscape>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-VF.ttf"
        "$<TARGET_FILE_DIR:REMIMazeEscape>/assets/fonts/NotoSansKR-VF.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-OFL.txt"
        "$<TARGET_FILE_DIR:REMIMazeEscape>/assets/fonts/NotoSansKR-OFL.txt")
