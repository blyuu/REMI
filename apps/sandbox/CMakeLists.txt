add_executable(REMISandbox main.cpp)
target_link_libraries(REMISandbox PRIVATE REMI::Runtime REMI::Renderer REMI::Resources REMI::Physics REMI::DebugUI REMI::Assets remi_build_options)
set_target_properties(REMISandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:REMISandbox>"
    FOLDER "Apps")
add_custom_target(remi_sandbox_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:REMISandbox>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/shaders/Basic.hlsl" "$<TARGET_FILE_DIR:REMISandbox>/shaders/Basic.hlsl")
add_dependencies(REMISandbox remi_sandbox_shaders)
