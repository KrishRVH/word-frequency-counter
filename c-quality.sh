#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./c-quality.sh [source_root] [build_dir_with_compile_commands]
#
# Examples:
#   ./c-quality.sh .
#   ./c-quality.sh . build

readonly JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
readonly SRC_ROOT="${1:-.}"
readonly BUILD_HINT="${2:-}"
readonly CDB="compile_commands.json"

note() { printf '\033[0;34m[INFO]\033[0m %s\n' "$*"; }
fail() { printf '\033[0;31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

for tool in clang-format clang-tidy cppcheck; do
  command -v "$tool" >/dev/null 2>&1 || fail "Missing tool: $tool"
done

# Prefer git-tracked sources so we don't format/check build artifacts.
list_files() {
  if command -v git >/dev/null 2>&1 && git -C "$SRC_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$SRC_ROOT" ls-files -z -- '*.c' '*.h'
  else
    find "$SRC_ROOT" -type d \( -name .git -o -name build -o -name 'build-*' \) -prune \
      -o -type f \( -name '*.c' -o -name '*.h' \) -print0
  fi
}

list_c_files() {
  if command -v git >/dev/null 2>&1 && git -C "$SRC_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$SRC_ROOT" ls-files -z -- '*.c'
  else
    find "$SRC_ROOT" -type d \( -name .git -o -name build -o -name 'build-*' \) -prune \
      -o -type f -name '*.c' -print0
  fi
}

detect_cdb_dir() {
  # 1) explicit build hint
  if [[ -n "$BUILD_HINT" && -f "$BUILD_HINT/$CDB" ]]; then
    printf '%s\n' "$BUILD_HINT"
    return 0
  fi

  # 2) repo root (your symlink case)
  if [[ -f "$SRC_ROOT/$CDB" ]]; then
    printf '%s\n' "$SRC_ROOT"
    return 0
  fi

  # 3) common build dir
  if [[ -f "$SRC_ROOT/build/$CDB" ]]; then
    printf '%s\n' "$SRC_ROOT/build"
    return 0
  fi

  return 1
}

note "Running clang-format..."
list_files | xargs -0 -r -P "$JOBS" clang-format -i

note "Running clang-tidy..."
if cdb_dir="$(detect_cdb_dir)"; then
  list_c_files | xargs -0 -r -P "$JOBS" -n 1 \
    clang-tidy -p "$cdb_dir"
else
  note "No $CDB found (expected in repo root or build dir); skipping clang-tidy."
fi

note "Running cppcheck (hard fail on warnings/perf/portability)..."
hard_args=(
  --check-level=exhaustive
  --enable=warning,performance,portability
  --inconclusive --quiet --inline-suppr
  --suppress=missingIncludeSystem
  --suppress=unmatchedSuppression
  --error-exitcode=1 -j "$JOBS"
)

if cdb_dir="$(detect_cdb_dir)"; then
  cppcheck "${hard_args[@]}" --project="$cdb_dir/$CDB"
else
  list_files | xargs -0 -r \
    cppcheck "${hard_args[@]}" --language=c --std=c99 -I"$SRC_ROOT"
fi

note "Running cppcheck (style, informational only)..."
soft_args=(
  --check-level=exhaustive
  --enable=style
  --inconclusive --quiet --inline-suppr
  --suppress=missingIncludeSystem
  --suppress=unmatchedSuppression
  # Public API headers will *always* look unused to cppcheck inside the library itself.
  --suppress=unusedStructMember
  -j "$JOBS"
)

if cdb_dir="$(detect_cdb_dir)"; then
  cppcheck "${soft_args[@]}" --project="$cdb_dir/$CDB" || true
else
  list_files | xargs -0 -r \
    cppcheck "${soft_args[@]}" --language=c --std=c99 -I"$SRC_ROOT" || true
fi

note "All quality checks passed (hard checks)."
