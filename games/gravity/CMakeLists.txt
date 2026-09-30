add_library(remi_gravity STATIC GravityGame.cpp)
target_include_directories(remi_gravity PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(remi_gravity PUBLIC REMI::Physics PRIVATE remi_build_options)
add_executable(REMIGravity main.cpp GameHud.cpp)
target_link_libraries(REMIGravity PRIVATE remi_gravity REMI::DebugUI REMI::Runtime REMI::Renderer REMI::Resources remi_build_options gdi32)
set_target_properties(REMIGravity PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>" VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:REMIGravity>")
add_dependencies(REMIGravity remi_sandbox_shaders)
set(REMI_DEFAULT_CHARACTER "${PROJECT_SOURCE_DIR}/assets/characters/default.rmc")
if(NOT EXISTS "${REMI_DEFAULT_CHARACTER}" AND EXISTS "${PROJECT_SOURCE_DIR}/assets/Quinn/quinn.rmc")
    set(REMI_DEFAULT_CHARACTER "${PROJECT_SOURCE_DIR}/assets/Quinn/quinn.rmc")
endif()
if(EXISTS "${REMI_DEFAULT_CHARACTER}")
    add_custom_command(TARGET REMIGravity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:REMIGravity>/assets/characters"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${REMI_DEFAULT_CHARACTER}" "$<TARGET_FILE_DIR:REMIGravity>/assets/characters/default.rmc")
endif()
add_custom_command(TARGET REMIGravity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:REMIGravity>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/Pretendard-SemiBold.otf" "$<TARGET_FILE_DIR:REMIGravity>/assets/fonts/Pretendard-SemiBold.otf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE.txt" "$<TARGET_FILE_DIR:REMIGravity>/assets/fonts/LICENSE.txt")
