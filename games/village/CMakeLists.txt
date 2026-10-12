foreach(game_target REMIVillage REMIRaven)
    add_executable(${game_target} main.cpp)
    target_link_libraries(${game_target} PRIVATE REMI::Runtime REMI::Renderer REMI::Resources
        REMI::Physics REMI::Assets REMI::DebugUI remi_build_options)
    set_target_properties(${game_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_dependencies(${game_target} remi_sandbox_shaders)
    add_custom_command(TARGET ${game_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${game_target}>/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-VF.ttf" "$<TARGET_FILE_DIR:${game_target}>/assets/fonts/NotoSansKR-VF.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/NotoSansKR-OFL.txt" "$<TARGET_FILE_DIR:${game_target}>/assets/fonts/NotoSansKR-OFL.txt")
endforeach()
target_compile_definitions(REMIRaven PRIVATE REMI_RAVEN_DEFAULT=1)
