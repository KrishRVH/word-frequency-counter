#!/usr/bin/env bash
set -euo pipefail

# Define Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

note() { echo -e "${BLUE}[GAMUT]${NC} $1"; }
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }

# 1. PRIMARY BUILD: Clang + Sanitizers + Static Analysis
# We use this build for compile_commands.json because Clang tooling works best with Clang
note "Step 1: Primary Build (Clang + Sanitizers + Quality Checks)"
rm -rf build
cmake -S . -B build -G Ninja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DENABLE_SANITIZERS=ON
cmake --build build --parallel $(nproc)

# Link commands for LSP
ln -sf build/compile_commands.json .

# Run Tests
ctest --test-dir build --output-on-failure || { echo -e "${RED}Clang Tests Failed${NC}"; exit 1; }

# Run Static Analysis (only needed once)
JOBS=$(nproc) ./c-quality.sh build

pass "Clang (Sanitized) & Static Analysis Clean"

# 2. SECONDARY BUILD: GCC + Sanitizers
note "Step 2: Secondary Build (GCC + Sanitizers)"
rm -rf build-gcc
cmake -S . -B build-gcc -G Ninja \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_SANITIZERS=ON
cmake --build build-gcc --parallel $(nproc)
ctest --test-dir build-gcc --output-on-failure || { echo -e "${RED}GCC Tests Failed${NC}"; exit 1; }

pass "GCC (Sanitized) Clean"

# 3. FAST BUILD: TinyCC (TCC)
# Great for catching strict C standard compliance issues
if command -v tcc >/dev/null; then
    note "Step 3: TinyCC Build"
    rm -rf build-tcc
    cmake -S . -B build-tcc -G Ninja \
        -DCMAKE_C_COMPILER=tcc \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_SANITIZERS=OFF
    cmake --build build-tcc --parallel $(nproc)
    
    # Run tests (TCC binaries run natively on Linux)
    ctest --test-dir build-tcc --output-on-failure || { echo -e "${RED}TCC Tests Failed${NC}"; exit 1; }
    pass "TinyCC Clean"
else
    note "Skipping TinyCC (not installed)"
fi

# 4. CROSS BUILD: MinGW (Windows Check)
# Checks if code compiles for Windows. Running tests requires Wine.
if command -v x86_64-w64-mingw32-gcc >/dev/null; then
    note "Step 4: MinGW (Windows) Cross-Compilation"
    rm -rf build-mingw
    
    # Simple toolchain setup on the fly
    cmake -S . -B build-mingw -G Ninja \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
        -DENABLE_SANITIZERS=OFF
        
    cmake --build build-mingw --parallel $(nproc)
    pass "MinGW Compilation Clean"
else
    note "Skipping MinGW (not installed)"
fi

echo -e "${GREEN}=== ALL SYSTEMS GO ===${NC}"
