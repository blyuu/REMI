add_executable(REMIBootstrap main.cpp)
target_link_libraries(REMIBootstrap PRIVATE REMI::Core remi_build_options)
set_target_properties(REMIBootstrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:REMIBootstrap>"
    FOLDER "Apps")
