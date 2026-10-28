# toml++ v3.4.0, vendored single header (MIT, see LICENSE). Do not edit toml.hpp:
# replace it with a newer release from https://github.com/marzer/tomlplusplus instead.
add_library(tomlplusplus INTERFACE)
# SYSTEM keeps the project's warning flags from applying to third-party code.
target_include_directories(tomlplusplus SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tomlplusplus INTERFACE cxx_std_17)
