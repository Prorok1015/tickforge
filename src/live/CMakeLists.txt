find_package(Boost 1.74 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)

add_library(tickforge_live
    include/tickforge/live/binance_protocol.hpp
    include/tickforge/live/capture.hpp
    src/binance_protocol.cpp
    src/capture.cpp
)
target_include_directories(tickforge_live PUBLIC include)
target_link_libraries(tickforge_live
    PUBLIC tickforge_core
    PRIVATE tickforge_console Boost::headers OpenSSL::SSL OpenSSL::Crypto
        Threads::Threads nlohmann_json::nlohmann_json
)
tickforge_enable_warnings(tickforge_live)
