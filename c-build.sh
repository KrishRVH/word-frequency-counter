#!/usr/bin/env bash
set -euo pipefail

run_preset() {
    local preset="$1"
    echo "----------------------------------------------------"
    echo "Running preset: $preset"
    echo "----------------------------------------------------"
    
    # 1. Configure
    cmake --preset "$preset"
    
    # 2. Build (Now finds the 'buildPreset' named $preset)
    cmake --build --preset "$preset" --parallel "$(nproc)"
    
    # 3. Test (Skip MinGW)
    if [[ "$preset" != "mingw" ]]; then
        ctest --preset "$preset"
    fi
}

# Run the matrix
run_preset clang
run_preset gcc

# Checks for optional tools
if command -v tcc >/dev/null; then run_preset tcc; fi
if command -v x86_64-w64-mingw32-gcc >/dev/null; then run_preset mingw; fi

# Static Analysis on the main build
./c-quality.sh build-clang

echo "✅ GAMUT COMPLETE"
