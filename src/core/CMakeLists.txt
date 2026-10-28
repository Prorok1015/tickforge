add_library(tickforge_core
    include/tickforge/core/events.hpp
    include/tickforge/core/orders.hpp
    include/tickforge/core/amounts.hpp
    include/tickforge/core/order_book.hpp
    include/tickforge/core/book_tracker.hpp
    include/tickforge/core/replay.hpp
    include/tickforge/core/recorder.hpp
    include/tickforge/core/units.hpp
    src/order_book.cpp
    src/units.cpp
    src/amounts.cpp
    src/book_tracker.cpp
    src/replay.cpp
    src/recorder.cpp
)
target_include_directories(tickforge_core PUBLIC include)
target_compile_features(tickforge_core PUBLIC cxx_std_20)
tickforge_enable_warnings(tickforge_core)
