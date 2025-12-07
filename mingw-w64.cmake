# ----------------------------------------------
# mingw-w64.cmake – cross‑compile for Windows
# ----------------------------------------------
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Choose the GCC or Clang variant:
#   GCC:   x86_64-w64-mingw32-
#   Clang: x86_64-w64-mingw32-clang
set(TOOLCHAIN_PREFIX "x86_64-w64-mingw32-")   # <-- edit if you prefer clang

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_RC_COMPILER  "${TOOLCHAIN_PREFIX}windres")
set(CMAKE_FIND_ROOT_PATH  /usr/${TOOLCHAIN_PREFIX})

# Prevent CMake from searching the host system for libs/executables
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
