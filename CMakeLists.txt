cmake_minimum_required(VERSION 3.30)
project(word_frequency_counter VERSION 0.1.0 LANGUAGES C CXX)

option(WFC_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(WFC_WERROR "Treat compiler warnings as errors" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(wfc_apply_c_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(WFC_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
    )
    if(WFC_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(WFC_SANITIZERS)
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
      )
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "EXECUTABLE" OR target_type STREQUAL "SHARED_LIBRARY" OR
         target_type STREQUAL "MODULE_LIBRARY")
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
      endif()
    endif()
  endif()
endfunction()

add_executable(wordcount_c c/src/wordcount.c)
wfc_apply_c_defaults(wordcount_c)

add_executable(wordcount_cpp cpp/src/main.cpp)
target_compile_features(wordcount_cpp PRIVATE cxx_std_23)
set_target_properties(wordcount_cpp PROPERTIES CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
  target_compile_options(wordcount_cpp PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
  if(WFC_WERROR)
    target_compile_options(wordcount_cpp PRIVATE /WX)
  endif()
else()
  target_compile_options(wordcount_cpp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wold-style-cast
    -Woverloaded-virtual
    -Wnon-virtual-dtor
    -Wformat=2
  )
  if(WFC_WERROR)
    target_compile_options(wordcount_cpp PRIVATE -Werror)
  endif()
  if(WFC_SANITIZERS)
    target_compile_options(wordcount_cpp PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
    )
    target_link_options(wordcount_cpp PRIVATE -fsanitize=address,undefined)
  endif()
endif()
