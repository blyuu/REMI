add_library(remi_relay STATIC RelayGame.cpp RelayRules.cpp)
target_include_directories(remi_relay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(remi_relay PUBLIC REMI::Physics PRIVATE remi_build_options)

add_executable(REMIRelay main.cpp)
target_link_libraries(REMIRelay PRIVATE remi_relay REMI::Runtime REMI::Renderer REMI::DebugUI
    REMI::Resources remi_build_options)
set_target_properties(REMIRelay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:REMIRelay>")
add_dependencies(REMIRelay remi_sandbox_shaders)
add_custom_command(TARGET REMIRelay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:REMIRelay>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-VF.ttf" "$<TARGET_FILE_DIR:REMIRelay>/assets/fonts/NotoSansKR-VF.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-OFL.txt" "$<TARGET_FILE_DIR:REMIRelay>/assets/fonts/NotoSansKR-OFL.txt")
