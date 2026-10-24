include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <format>
int main() { auto value = std::format(\"{}\", 42); return value.empty(); }"
    TICKFORGE_HAS_STD_FORMAT)
if(NOT TICKFORGE_HAS_STD_FORMAT)
    message(FATAL_ERROR "tickforge requires a C++20 standard library with std::format support. Use a newer compiler and matching standard library (for example GCC 13+ with libstdc++).")
endif()

function(tickforge_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
