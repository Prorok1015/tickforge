add_library(tickforge_console
    include/tickforge/console/console.hpp
    include/tickforge/console/replay_reporter.hpp
    src/console.cpp
    src/replay_reporter.cpp
)
target_include_directories(tickforge_console PUBLIC include)
target_link_libraries(tickforge_console PUBLIC tickforge_core)
tickforge_enable_warnings(tickforge_console)
