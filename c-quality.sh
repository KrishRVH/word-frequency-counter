#!/usr/bin/env bash
#
# c-quality.sh: Run clang-format, clang-tidy, and cppcheck
#

set -euo pipefail

# Configuration
readonly JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
readonly ROOT="${1:-.}"
readonly CDB="compile_commands.json"

# Helpers
note() { printf '\033[0;34m[INFO]\033[0m %s\n' "$*"; }
fail() { printf '\033[0;31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

# Ensure tools exist
for tool in clang-format clang-tidy cppcheck; do
    command -v "$tool" >/dev/null || fail "Missing tool: $tool"
done

# Find sources, excluding build artifacts
# Usage: scan_files "pattern"
scan_files() {
    find "$ROOT" -type d \( -name build -o -name .git -o -name 'cmake-*' \) -prune \
        -o -type f -name "$1" -print0
}

# Locate dir containing compile_commands.json (root or build)
get_cdb_dir() {
    for d in "$ROOT" "$ROOT/build"; do
        [[ -f "$d/$CDB" ]] && echo "$d" && return 0
    done
    return 1
}

# 1. Clang-Format (In-place)
note "Running clang-format..."
scan_files "*.[ch]" | xargs -0 -r -P "$JOBS" clang-format -i

# 2. Clang-Tidy
note "Running clang-tidy..."
if cdb_dir=$(get_cdb_dir); then
    # -n 1 ensures file-level parallelism via xargs
    scan_files "*.c" | xargs -0 -r -P "$JOBS" -n 1 \
        clang-tidy -p "$cdb_dir" -quiet
else
    note "No $CDB; using fallback flags."
    # Fallback: Matches CMake (C99, Includes, Strict Warnings)
    # Note: Flags must follow '--' and the source file
    scan_files "*.c" | xargs -0 -r -P "$JOBS" -I {} \
        clang-tidy -quiet {} -- -std=c99 -I"$ROOT" \
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
fi

# 3. Cppcheck
note "Running cppcheck..."
check_args=(
    --check-level=exhaustive
    --enable=warning,style,performance,portability
    --inconclusive --quiet --inline-suppr
    --suppress=missingIncludeSystem
    --suppress=unmatchedSuppression
    --error-exitcode=1 -j "$JOBS"
)

if cdb_dir=$(get_cdb_dir); then
    cppcheck "${check_args[@]}" --project="$cdb_dir/$CDB"
else
    # Fallback: Generic scan via file list from find
    scan_files "*.[ch]" | xargs -0 \
        cppcheck "${check_args[@]}" --language=c --std=c99 --platform=unix64 -I"$ROOT"
fi

note "All quality checks passed."
