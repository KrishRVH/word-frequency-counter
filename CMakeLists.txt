cmake_minimum_required(VERSION 3.16)
project(word_frequency_counter C)

# --- Options ---
option(ENABLE_SANITIZERS "Enable Address+UB sanitizers (GCC/Clang only)" OFF)
option(WC_WERROR "Treat compiler warnings as errors when supported" OFF)
option(WC_MINGW_STATIC "Static link MinGW executables (helps running under Wine)" ON)
option(WC_BUILD_CLI "Build the wc CLI tool" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Standard, project-wide defaults (still allow per-target overrides)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest) # provides BUILD_TESTING and enables testing when ON

# Detect CompCert by compiler executable name (CMake may not have a built-in ID)
get_filename_component(WC_C_COMPILER_BASENAME "${CMAKE_C_COMPILER}" NAME)
set(WC_COMPILER_IS_COMPCERT OFF)
if(WC_C_COMPILER_BASENAME MATCHES "^(ccomp|ccomp\\.exe)$")
  set(WC_COMPILER_IS_COMPCERT ON)
endif()

function(wc_apply_common target)
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}")

  # Warnings: keep strict for GCC/Clang, conservative for CompCert, MSVC-friendly
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(WC_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  elseif(WC_COMPILER_IS_COMPCERT)
    # CompCert supports -Wall and -W<warning>/ -Werror (but not all GCC warnings)
    target_compile_options(${target} PRIVATE -Wall)
    if(WC_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    if(WC_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  # Sanitizers: only on native GCC/Clang (not MinGW, not MSVC, not CompCert)
  if(ENABLE_SANITIZERS)
    if(NOT MSVC AND NOT MINGW AND NOT WC_COMPILER_IS_COMPCERT)
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
      )
      # Only executables link sanitizer runtimes
      get_target_property(_type ${target} TYPE)
      if(_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
      endif()
    endif()
  endif()

  # MinGW static linking (executables only)
  if(WIN32 AND NOT MSVC AND WC_MINGW_STATIC)
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "EXECUTABLE")
      target_link_options(${target} PRIVATE -static)
    endif()
  endif()
endfunction()

# =============================================================================
# Core library variants
# =============================================================================

add_library(wordcount_lib STATIC wordcount.c)
wc_apply_common(wordcount_lib)

add_library(wordcount_lib_heap STATIC wordcount.c)
target_compile_definitions(wordcount_lib_heap PUBLIC WC_STACK_BUFFER=0)
wc_apply_common(wordcount_lib_heap)

add_library(wordcount_lib_tiny STATIC wordcount.c)
target_compile_definitions(wordcount_lib_tiny PUBLIC
  WC_STACK_BUFFER=0
  WC_MIN_INIT_CAP=8
  WC_MIN_BLOCK_SZ=64
  WC_MAX_WORD=32
)
wc_apply_common(wordcount_lib_tiny)

# =============================================================================
# CLI
# =============================================================================

if(WC_BUILD_CLI)
  add_executable(wc wc_main.c)
  target_link_libraries(wc PRIVATE wordcount_lib)
  wc_apply_common(wc)
endif()

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTING)
  function(create_test_variant name lib)
    add_executable(${name} wc_test.c)
    target_link_libraries(${name} PRIVATE ${lib})
    wc_apply_common(${name})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  create_test_variant(wc_test      wordcount_lib)
  create_test_variant(wc_test_heap wordcount_lib_heap)
  create_test_variant(wc_test_tiny wordcount_lib_tiny)
endif()
