add_library(tickforge_execution
    include/tickforge/execution/execution.hpp
    include/tickforge/execution/simulator.hpp
    include/tickforge/execution/order_manager.hpp
    src/simulator.cpp
    src/order_manager.cpp
)
target_include_directories(tickforge_execution PUBLIC include)
target_link_libraries(tickforge_execution PUBLIC tickforge_core tickforge_accounting)
tickforge_enable_warnings(tickforge_execution)
