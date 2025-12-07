cmake_minimum_required(VERSION 3.16)
project(word_parser_performance C)

# --- Options ---
option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Compiler Configuration ---

if(CMAKE_C_COMPILER_ID MATCHES "TinyCC")
    # TinyCC is very basic: strict warnings but no fancy stuff
    add_compile_options(-Wall)
    # TCC doesn't support sanitizers, so we ignore ENABLE_SANITIZERS here

elseif(MSVC)
    # Visual Studio / MSVC
    add_compile_options(/W4 /permissive-)
    if(ENABLE_SANITIZERS)
        add_compile_options(/fsanitize=address)
    endif()

elseif(MINGW)
    # MinGW (Windows cross-compile on Linux)
    # We use static linking so we don't need to copy DLLs to run tests via Wine
    add_link_options(-static)
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    
    # MinGW often lacks full sanitizer support depending on the version
    if(ENABLE_SANITIZERS)
        message(STATUS "Note: Sanitizers disabled for MinGW build to ensure stability")
    endif()

else()
    # GCC / Clang (Linux Native)
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# --- Helper: Apply C99 strictness ---
function(configure_c99_strict target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
endfunction()

# =============================================================================
# Core library variants
# =============================================================================

# 1. Default (Stack buffer, platform sizing)
add_library(wordcount_lib STATIC wordcount.c)
target_include_directories(wordcount_lib PUBLIC .)
configure_c99_strict(wordcount_lib)

# 2. Heap (Force heap buffer via WC_STACK_BUFFER=0)
add_library(wordcount_lib_heap STATIC wordcount.c)
target_include_directories(wordcount_lib_heap PUBLIC .)
target_compile_definitions(wordcount_lib_heap PUBLIC WC_STACK_BUFFER=0)
configure_c99_strict(wordcount_lib_heap)

# 3. Tiny (MCU simulation)
add_library(wordcount_lib_tiny STATIC wordcount.c)
target_include_directories(wordcount_lib_tiny PUBLIC .)
target_compile_definitions(wordcount_lib_tiny PUBLIC
    WC_STACK_BUFFER=0
    WC_MIN_INIT_CAP=8
    WC_MIN_BLOCK_SZ=64
    WC_MAX_WORD=32
)
configure_c99_strict(wordcount_lib_tiny)

# =============================================================================
# Executables
# =============================================================================

add_executable(wc wc_main.c)
target_link_libraries(wc PRIVATE wordcount_lib)
configure_c99_strict(wc)

# =============================================================================
# Tests
# =============================================================================
enable_testing()

function(create_test_variant name lib)
    add_executable(${name} wc_test.c)
    target_link_libraries(${name} PRIVATE ${lib})
    configure_c99_strict(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

create_test_variant(wc_test      wordcount_lib)
create_test_variant(wc_test_heap wordcount_lib_heap)
create_test_variant(wc_test_tiny wordcount_lib_tiny)
