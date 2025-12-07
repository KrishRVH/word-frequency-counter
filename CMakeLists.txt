cmake_minimum_required(VERSION 3.16)
project(word_parser_performance C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings globally
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

# =============================================================================
# Core library variants (C99) - files in root
# =============================================================================

# Default configuration: stack scan buffer, platform-tuned sizing
add_library(wordcount_lib STATIC wordcount.c)
target_include_directories(wordcount_lib PUBLIC .)
set_target_properties(wordcount_lib PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Heap scan buffer configuration: WC_STACK_BUFFER=0
# This exercises the !WC_STACK_BUFFER path in wordcount.c.
add_library(wordcount_lib_heap STATIC wordcount.c)
target_include_directories(wordcount_lib_heap PUBLIC .)
target_compile_definitions(wordcount_lib_heap PUBLIC WC_STACK_BUFFER=0)
set_target_properties(wordcount_lib_heap PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Tiny-profile configuration: simulates a constrained MCU build.
# Smaller floors for table capacity and arena block size, reduced max word,
# and heap-based scan buffer to avoid large stack allocations.
add_library(wordcount_lib_tiny STATIC wordcount.c)
target_include_directories(wordcount_lib_tiny PUBLIC .)
target_compile_definitions(wordcount_lib_tiny PUBLIC
    WC_STACK_BUFFER=0   # no fixed 1KB stack buffer
    WC_MIN_INIT_CAP=8   # smaller hash table floor
    WC_MIN_BLOCK_SZ=64  # smaller arena floor
    WC_MAX_WORD=32      # small maximum word length
)
set_target_properties(wordcount_lib_tiny PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# =============================================================================
# CLI using the default library
# =============================================================================
add_executable(wc wc_main.c)
target_link_libraries(wc PRIVATE wordcount_lib)
set_target_properties(wc PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# =============================================================================
# Tests
# =============================================================================

# Default test binary (stack scan buffer, default sizing)
add_executable(wc_test wc_test.c)
target_link_libraries(wc_test PRIVATE wordcount_lib)
set_target_properties(wc_test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Heap-buffer test binary (WC_STACK_BUFFER=0 for library)
add_executable(wc_test_heap wc_test.c)
target_link_libraries(wc_test_heap PRIVATE wordcount_lib_heap)
set_target_properties(wc_test_heap PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Tiny-profile test binary (MCU-style config)
add_executable(wc_test_tiny wc_test.c)
target_link_libraries(wc_test_tiny PRIVATE wordcount_lib_tiny)
set_target_properties(wc_test_tiny PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

enable_testing()
add_test(NAME wc_unit_tests       COMMAND wc_test)
add_test(NAME wc_unit_tests_heap  COMMAND wc_test_heap)
add_test(NAME wc_unit_tests_tiny  COMMAND wc_test_tiny)
