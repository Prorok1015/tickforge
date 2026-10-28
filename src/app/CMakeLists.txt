add_executable(tickforge main.cpp cli.hpp cli.cpp commands.hpp replay_command.cpp capture_command.cpp
    paper_command.cpp paper_scenario.hpp paper_scenario.cpp paper_reporter.hpp paper_reporter.cpp
    paper_export.hpp paper_export.cpp paper_diagnostics.hpp paper_diagnostics.cpp
    paper_journal.hpp paper_journal.cpp live_command.cpp live_dashboard.hpp live_dashboard.cpp)
target_link_libraries(tickforge PRIVATE tickforge_core tickforge_console tickforge_execution tickforge_accounting tickforge_strategy
    tickforge_engine
    tomlplusplus)
if(TICKFORGE_ENABLE_LIVE)
    target_compile_definitions(tickforge PRIVATE TICKFORGE_ENABLE_LIVE=1)
    target_link_libraries(tickforge PRIVATE tickforge_binance)
endif()
# Keep existing ./build/tickforge commands and VS launch targets working.
set_target_properties(tickforge PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
tickforge_enable_warnings(tickforge)
