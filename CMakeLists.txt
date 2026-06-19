cmake_minimum_required(VERSION 3.20)
project(word_frequency_counter VERSION 0.1.0 LANGUAGES C CXX)

option(WFC_WERROR "Treat C compiler warnings as errors" ON)
option(WFC_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(wfc_apply_c_defaults target)
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/c/include")

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
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

add_library(wordfreq STATIC c/src/wordfreq.c)
wfc_apply_c_defaults(wordfreq)

add_executable(wordcount_c c/src/main.c)
target_link_libraries(wordcount_c PRIVATE wordfreq)
wfc_apply_c_defaults(wordcount_c)

add_executable(wordcount_cpp cpp/src/main.cpp)
target_compile_features(wordcount_cpp PRIVATE cxx_std_26)

if(MSVC)
  target_compile_options(wordcount_cpp PRIVATE /W4)
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
