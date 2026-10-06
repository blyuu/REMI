add_executable(REMIBenchmarks main.cpp)
target_link_libraries(REMIBenchmarks PRIVATE REMI::Renderer REMI::Resources REMI::Platform remi_build_options)
set_target_properties(REMIBenchmarks PROPERTIES FOLDER "Tools")
