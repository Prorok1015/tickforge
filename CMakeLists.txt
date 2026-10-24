cmake_minimum_required(VERSION 3.20)

project(tickforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <format>
int main() { auto value = std::format(\"{}\", 42); return value.empty(); }"
    TICKFORGE_HAS_STD_FORMAT)
if(NOT TICKFORGE_HAS_STD_FORMAT)
    message(FATAL_ERROR "tickforge requires a C++20 standard library with std::format support. Use a newer compiler and matching standard library (for example GCC 13+ with libstdc++).")
endif()

add_library(tickforge_core src/replay.cpp src/order_book.cpp src/book_tracker.cpp src/recorder.cpp src/console.cpp)
target_include_directories(tickforge_core PUBLIC src)
target_compile_features(tickforge_core PUBLIC cxx_std_20)
set_target_properties(tickforge_core PROPERTIES CXX_EXTENSIONS NO)
add_executable(tickforge src/main.cpp)
target_link_libraries(tickforge PRIVATE tickforge_core)
option(TICKFORGE_ENABLE_LIVE "Build public Binance depth capture (Boost, OpenSSL, JSON required)" OFF)
if(TICKFORGE_ENABLE_LIVE)
    find_package(Boost 1.74 REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    find_package(nlohmann_json 3.10 REQUIRED)
    add_library(tickforge_live src/binance_protocol.cpp src/capture.cpp)
    target_link_libraries(tickforge_live PUBLIC tickforge_core PRIVATE Boost::headers
        OpenSSL::SSL OpenSSL::Crypto Threads::Threads nlohmann_json::nlohmann_json)
    target_compile_definitions(tickforge PRIVATE TICKFORGE_ENABLE_LIVE=1)
    target_link_libraries(tickforge PRIVATE tickforge_live)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tickforge_live PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
set_target_properties(tickforge PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tickforge PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(tickforge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(capture_contract_tests tests/capture_contract_tests.cpp)
    target_link_libraries(capture_contract_tests PRIVATE tickforge_core)
    add_test(NAME capture_contract COMMAND capture_contract_tests ${CMAKE_CURRENT_BINARY_DIR})
    if(TICKFORGE_ENABLE_LIVE)
        add_executable(binance_protocol_tests tests/binance_protocol_tests.cpp)
        target_link_libraries(binance_protocol_tests PRIVATE tickforge_live)
        add_test(NAME binance_protocol COMMAND binance_protocol_tests)
    endif()
    add_executable(replay_tests tests/replay_tests.cpp)
    target_link_libraries(replay_tests PRIVATE tickforge_core)
    add_test(NAME replay_contract COMMAND replay_tests)
    add_executable(order_book_tests tests/order_book_tests.cpp)
    target_link_libraries(order_book_tests PRIVATE tickforge_core)
    add_test(NAME order_book_contract COMMAND order_book_tests)
    add_test(NAME book_replay_cli COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tickforge>
        -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/examples/synthetic-book.csv
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/book_replay_cli.cmake)
    add_test(NAME replay_cli COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tickforge>
        -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/examples/synthetic.csv
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_cli.cmake)
    add_test(NAME cli_smoke COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tickforge>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
endif()
