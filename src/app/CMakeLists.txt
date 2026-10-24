add_executable(tickforge main.cpp cli.hpp cli.cpp commands.hpp replay_command.cpp capture_command.cpp)
target_link_libraries(tickforge PRIVATE tickforge_core tickforge_console)
if(TICKFORGE_ENABLE_LIVE)
    target_compile_definitions(tickforge PRIVATE TICKFORGE_ENABLE_LIVE=1)
    target_link_libraries(tickforge PRIVATE tickforge_live)
endif()
# Keep existing ./build/tickforge commands and VS launch targets working.
set_target_properties(tickforge PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
tickforge_enable_warnings(tickforge)
