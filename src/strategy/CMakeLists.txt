add_library(tickforge_strategy include/tickforge/strategy/strategy.hpp
    include/tickforge/strategy/rules.hpp src/rules.cpp
    include/tickforge/strategy/threshold.hpp src/threshold.cpp
    include/tickforge/strategy/threshold_maker.hpp src/threshold_maker.cpp)
target_include_directories(tickforge_strategy PUBLIC include)
target_link_libraries(tickforge_strategy PUBLIC tickforge_core tickforge_accounting)
tickforge_enable_warnings(tickforge_strategy)
