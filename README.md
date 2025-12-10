# word-frequency-counter – C99 Word Frequency Library

A small, embeddable, **C99** word-frequency library with:

- Predictable memory use (optional hard caps and static-buffer mode)
- Robust error handling (including torture-tested OOM paths)
- Cross-platform support (Windows + POSIX)
- A clean, stable API (single header + single implementation)
- C++-friendly linkage via `extern "C"` and a `WC_RESTRICT` macro
- Build-configuration introspection (`wc_build_info`) for ABI sanity checks
- A zero-allocation iterator API for memory-constrained enumeration

This README is the authoritative specification for the library’s behavior,
motivation, and integration model. When in doubt, this document plus
`wordcount.h` define the contract.

---

## Table of Contents

1. [Overview](#overview)  
2. [Motivation and Non-Goals](#motivation-and-non-goals)  
3. [High-Level Design](#high-level-design)  
   - [Data Model](#data-model)  
   - [Hash Table](#hash-table)  
   - [Arena Allocator](#arena-allocator)  
   - [Memory Accounting and Limits](#memory-accounting-and-limits)  
   - [Static Buffer (MCU / No-malloc Mode)](#static-buffer-mcu--no-malloc-mode)  
   - [Error Model](#error-model)  
   - [Thread Safety](#thread-safety)  
4. [API Reference](#api-reference)  
   - [Types](#types)  
   - [Result Codes](#result-codes)  
   - [Lifecycle Functions](#lifecycle-functions)  
   - [Word Insertion and Scanning](#word-insertion-and-scanning)  
   - [Query and Results Functions](#query-and-results-functions)  
   - [Zero-Allocation Iterator API](#zero-allocation-iterator-api)  
   - [Utility Functions](#utility-functions)  
5. [Word Detection and Normalization](#word-detection-and-normalization)  
6. [Memory Configuration](#memory-configuration)  
   - [Compile-Time Configuration Macros](#compile-time-configuration-macros)  
   - [Runtime Limits via `wc_limits`](#runtime-limits-via-wc_limits)  
7. [Building and Integration](#building-and-integration)  
   - [Using CMake (Presets, Recommended)](#using-cmake-presets-recommended)  
   - [One-shot Build & Quality Script](#one-shot-build--quality-script)  
   - [Direct Compilation](#direct-compilation)  
   - [Directory Layout](#directory-layout)  
8. [CLI Tool (`wc`)](#cli-tool-wc)  
9. [Testing, Fuzzing, and OOM Injection](#testing-fuzzing-and-oom-injection)  
10. [Portability and Platform Assumptions](#portability-and-platform-assumptions)  
11. [Adversarial Inputs and Complexity](#adversarial-inputs-and-complexity)  
12. [ABI and Build Configuration Compatibility](#abi-and-build-configuration-compatibility)  
13. [Performance and Complexity](#performance-and-complexity)  
14. [Versioning and Stability Guarantees](#versioning-and-stability-guarantees)  
15. [Guidelines for Contributors](#guidelines-for-contributors)  
16. [License](#license)  

---

## Overview

The `wordcount` library provides a **small, robust, and embeddable** API for
counting word frequencies in text. It is designed to be:

- **Predictable**: all internal allocations are overflow-checked; behavior on
  `WC_NOMEM` is defined and tested.
- **Portable**: pure C99 core, with no dependencies beyond the standard C
  library.
- **Configurable**: suitable both for desktop/server workloads and
  resource-constrained embedded systems.
- **Defensive**: fails fast on unsupported platforms (non-ASCII, strange
  character encodings) via compile-time checks.
- **Inspectable**: build-time configuration is available at runtime via
  `wc_build_info`.

The core implementation resides in:

- `wordcount.h` – public API  
- `wordcount.c` – implementation  

On top of the library, the repository ships:

- `wc_main.c` – a CLI tool using memory-mapped I/O for files and streaming for
  stdin
- `wc_test.c` – a comprehensive test suite with optional OOM injection
- `CMakeLists.txt` / `CMakePresets.json` – cross-compiler build configuration
- `c-build.sh` / `c-quality.sh` – developer tooling for multi-toolchain builds
  and static analysis
- `mingw-w64.cmake` – optional MinGW cross-compilation toolchain file

---

## Motivation and Non-Goals

### Motivation

The library is aimed at:

- Applications that need **word frequency statistics** as a component:
  - Text analytics, indexing, search, tagging
  - Developer tools and static analyzers
- Environments that require **tight control over memory usage**:
  - Embedded Linux, RTOSes, larger MCUs
  - Sandboxed / untrusted-input scenarios
- Systems that value **robustness over cleverness**:
  - All size calculations are overflow-checked
  - All allocation failures yield defined error codes
  - No undefined behavior for malformed input on supported platforms
- Scenarios where **configuration drift matters**:
  - You can introspect the build-time configuration at runtime and verify it
    matches your expectations.

### Non-Goals

The library deliberately does **not** try to:

- Provide Unicode word segmentation or locale-aware rules  
  Word detection is strictly ASCII-letter based (`A–Z`, `a–z`); all other bytes
  are separators.
- Be the fastest possible implementation on x86-64  
  The focus is correctness and robustness, not micro-benchmarks.
- Expose complex concurrency primitives  
  The library is re-entrant and per-instance thread-safe; it does not manage
  threads for you.
- Support arbitrary text encodings or EBCDIC  
  It assumes an ASCII-compatible execution character set.
- Provide cryptographic or DoS-hard hashing  
  FNV-1a is used for speed and reproducibility; a hash seed is available for
  simple randomization, but this is not a cryptographic defense.

---

## High-Level Design

### Data Model

A `wc` instance (opaque handle) tracks:

- A hash table of **slots**, each holding:
  - `word` – pointer to NUL-terminated stored word
  - `hash` – precomputed hash (FNV-1a over stored characters, with a per-instance seed)
  - `cnt` – occurrence count
- An **arena** of one or more blocks in which:
  - Word strings are stored in contiguous buffers
- Global counters:
  - `tot` – total number of words observed (including duplicates)
  - `len` – number of unique words
- Configuration:
  - `maxw` – maximum stored word length (runtime; clamped)
  - Internal memory accounting / limits
  - A per-instance hash seed used for hash randomization

### Hash Table

- **Open addressing** with **linear probing**
- **Power-of-two capacity** (`cap` always a power of two):
  - Index: `hash & (cap - 1)`
- **Load factor threshold** ~0.7:
  - When `len * 10 >= cap * 7`, the table is doubled (unless static-buffer mode
    is active).
- **Stored hash per slot**:
  - Collisions are filtered by hash equality before `memcmp`.

Hash function:

- FNV-1a (32-bit), widened to `unsigned long`, with a per-instance seed:

  ```c
  // Conceptual form
  h = seed_basis;         // derived from FNV offset and optional hash_seed
  for each byte c:
      h ^= (unsigned char)c;
      h *= 16777619u;
  ```

- `seed_basis` is derived from a fixed FNV offset (`2166136261`) XORed with an
  optional `hash_seed` supplied via `wc_limits`.

The public API does not expose exact-width integer types. Internally, a widened
32-bit hash is stored in `unsigned long`.

### Arena Allocator

Word strings are stored in an arena:

- First block allocated during `wc_open_ex`
- Subsequent blocks added as needed (unless in static-buffer mode)
- Each block:
  - Header: `next`, `cur`, `end`
  - Flexible buffer: `buf[]`
- Allocations:
  - Bump-pointer within the current block
  - Alignment to an internal union type that safely covers `void *`, `size_t`,
    and `unsigned long`
  - Zero-initialized memory for safety and deterministic behavior

In static-buffer mode:

- Only a **single initial block** is used; no further blocks are allocated.
- When the arena fills up, insertions fail with `WC_NOMEM` (while leaving the
  structure in a valid, queryable state).

### Memory Accounting and Limits

Internal allocations (hash table, arena blocks, optional heap/static scan
buffer) are accounted in `bytes_used`, up to `bytes_limit`:

- In **dynamic mode** (no static buffer):
  - `bytes_limit` (from `wc_limits.max_bytes`) caps internal allocations.
- In **static-buffer mode**:
  - All internal allocations are carved out of `[static_buf,
    static_buf + static_size)`.
  - `bytes_limit`, if provided, is clamped to `static_size` and acts as an
    additional guard.

All relevant arithmetic is overflow-checked; any attempted allocation that would
overflow `size_t` or exceed configured limits fails cleanly.

Allocations that would exceed limits:

- Are rejected before memory is obtained from the allocator.
- Cause the relevant public function to return `WC_NOMEM`.

Allocations performed via the public interface (e.g., `wc_results` buffer) are
*not* counted against these internal limits, as their lifetime is under the
caller’s control.

### Static Buffer (MCU / No-malloc Mode)

For MCU-style environments without reliable `malloc`/`free`,
`wc_open_ex` can be configured with a caller-supplied static buffer:

- `static_buf` – pointer to caller-owned memory
- `static_size` – size of that region in bytes

In this mode:

- **All internal allocations** (hash table, first arena block, heap/static scan
  buffer when `WC_STACK_BUFFER == 0`) use a simple bump allocator inside the
  static buffer.
- **No re-use** or freeing occurs inside the buffer.
- **Hash table growth is disabled**:
  - Once the load factor exceeds ~0.7, further inserts fail with `WC_NOMEM`.
- **Arena growth is disabled**:
  - All words must fit in the first block; further allocation attempts yield
    `WC_NOMEM`.

The handle (`struct wc`) itself and the array returned by `wc_results()` still
use `WC_MALLOC` / `WC_FREE`. On very small systems, these macros should be
redirected to a custom allocator or another static pool.

To make behavior deterministic and fail-fast:

- In static-buffer mode, `wc_open_ex` performs an **initialization-time dry
  run** using a scratch allocator state:
  - It simulates allocating:
    - The initial hash table
    - The first arena block
    - The optional heap/static scan buffer (if `WC_STACK_BUFFER == 0`)
  - If any of those simulated allocations would fail under the effective budget
    (minimum of `static_size` and `max_bytes` when both are set),
    `wc_open_ex` fails with `NULL` before creating the instance.

Static buffer alignment:

- The buffer must be suitably aligned for internal types.
- At runtime, `wc_open_ex` verifies that `static_buf` is aligned to the
  internal alignment (`WC_ALIGN`) using `uintptr_t` when available (and
  `size_t` as a fallback).
- Misaligned buffers are rejected (return `NULL`).

### Error Model

All public functions follow a clear error protocol:

- Functions returning `int`:
  - `WC_OK` (0) – success
  - `WC_ERROR` (1) – invalid arguments or corrupted internal state
  - `WC_NOMEM` (2) – allocation failed or memory limit reached
- Query functions returning `size_t`:
  - Return 0 when passed `NULL`
- `wc_results`:
  - Returns `WC_OK`, `WC_ERROR`, or `WC_NOMEM`
  - On `WC_OK` with no entries: `*out == NULL`, `*n == 0`
- The helper `wc_errstr` converts any result code into a human-readable,
  static string.

All internal error paths strive to leave the `wc` object in a valid and
queryable state (e.g., after `WC_NOMEM`, you can still call `wc_total`,
`wc_unique`, `wc_results`).

### Thread Safety

- Separate `wc` instances **may be used concurrently** from multiple threads.
- A single `wc` instance **MUST NOT** be accessed concurrently without external
  synchronization.
- The library does not use global mutable state and does not perform its own
  locking.

---

## API Reference

The API is defined in `wordcount.h`. This section summarizes the public surface.

### Types

```c
typedef struct wc wc;
```

Opaque handle for a word counter. Must be created via `wc_open` /
`wc_open_ex` and destroyed via `wc_close`.

```c
typedef struct wc_limits {
    size_t        max_bytes;
    size_t        init_cap;
    size_t        block_size;
    void         *static_buf;
    size_t        static_size;
    /* Set to 0 for deterministic behavior (default)
       Set to random value for DoS protection. */
    unsigned long hash_seed;
} wc_limits;
```

Per-instance memory and sizing limits:

- `max_bytes`:
  - Hard cap on total **internal** allocations for this `wc` when using the
    default dynamic allocator.
  - Counts:
    - Hash table (all growth)
    - Arena blocks
    - Optional heap/static scan buffer (if `WC_STACK_BUFFER == 0`)
  - Does **not** count:
    - The `wc` struct itself
    - Arrays returned by `wc_results`
  - `0` = unlimited.
- `init_cap`:
  - Initial hash table capacity (number of slots).
  - `0` = let the library choose a platform-tuned default.
  - Rounded up to a power of two internally and floored by `WC_MIN_INIT_CAP`.
- `block_size`:
  - Arena block size in bytes for the first block.
  - `0` = platform-tuned default, floored by `WC_MIN_BLOCK_SZ`.
- `static_buf`, `static_size`:
  - Optional caller-supplied region used for all **internal** allocations.
  - Enables static-buffer mode (see above).
  - Must be suitably aligned for `void *`, `size_t`, and `unsigned long`.
  - Must not be shared with another `wc` instance.
  - On platforms where `uintptr_t` is available, misaligned buffers are
    detected at runtime and cause `wc_open_ex` to fail.
  - When both `static_size` and `max_bytes` are set, the effective budget is
    the minimum of the two.
- `hash_seed`:
  - Optional per-instance hash seed.
  - `0` (default) yields deterministic hashing based on a fixed FNV offset
    basis.
  - Non-zero values randomize the hash function to make simple collision
    attacks harder (not cryptographic).

Always initialize with all fields zeroed (e.g., `memset(&lim, 0, sizeof lim)`).
New fields may be added in future versions; zero-initialization ensures a
sensible default.

```c
typedef struct wc_word {
    const char *word;
    size_t      count;
} wc_word;
```

Result entry produced by `wc_results`:

- `word` – pointer to the internal string (owned by the `wc` until `wc_close`)
- `count` – occurrence count

```c
typedef struct wc_build_config {
    unsigned long version_number;
    size_t        max_word;
    size_t        min_init_cap;
    size_t        min_block_sz;
    int           stack_buffer; /* 1 = stack, 0 = heap/static */
} wc_build_config;
```

Build-time configuration for this library binary:

- `version_number` – `WC_VERSION_NUMBER` used to build this library.
- `max_word` – compile-time `WC_MAX_WORD`.
- `min_init_cap` – compile-time `WC_MIN_INIT_CAP`.
- `min_block_sz` – compile-time `WC_MIN_BLOCK_SZ`.
- `stack_buffer` – non-zero if `WC_STACK_BUFFER` was non-zero (stack scan
  buffers).

This allows callers to detect build-configuration mismatches between headers and
library binaries, or to assert expected sizing at runtime.

```c
typedef struct wc_cursor {
    const wc *w;
    size_t    index;
} wc_cursor;
```

Cursor used by the zero-allocation iterator API to traverse all words.

### Result Codes

```c
#define WC_OK    0
#define WC_ERROR 1
#define WC_NOMEM 2
```

Returned by all `int`-returning API functions.

### Lifecycle Functions

```c
wc *wc_open(size_t max_word);
```

Create a new word counter with default limits.

- `max_word`:
  - Maximum stored word length:
    - `0` – use default (64)
    - Clamped into `[4, WC_MAX_WORD]` (see configuration macros below).
- Returns:
  - Non-NULL on success.
  - `NULL` on allocation failure.

```c
wc *wc_open_ex(size_t max_word, const wc_limits *limits);
```

Create a new word counter with explicit size and memory limits.

- `max_word`:
  - Same semantics as `wc_open`.
- `limits`:
  - Optional pointer to a `wc_limits` struct (`NULL` = same behavior as
    `wc_open`).
- Returns:
  - Non-NULL on success.
  - `NULL` on allocation failure.
  - `NULL` if supplied limits are impossible to satisfy (e.g., `static_size`
    and/or `max_bytes` too small to hold minimal internal structures under the
    configured budget, or misaligned `static_buf`).

```c
void wc_close(wc *w);
```

Destroy a word counter.

- Safe to call with `NULL`.
- Frees all internal memory associated with the instance.

### Word Insertion and Scanning

```c
int wc_add(wc *w, const char *WC_RESTRICT word);
```

Add a single **case-sensitive** word.

- `w` must be non-NULL.
- `word`:
  - NUL-terminated C string.
  - Empty strings (`""`) are ignored; the function returns `WC_OK` but does not
    modify counts.
  - Words longer than `max_word` are truncated before hashing and storage. The
    hash is computed only over the stored prefix; truncated forms that share
    the same prefix collide intentionally.
- Returns:
  - `WC_OK` on success.
  - `WC_ERROR` if `w == NULL` or `word == NULL`.
  - `WC_NOMEM` if an allocation fails or the configured memory budget is
    exhausted.

```c
int wc_scan(wc *w, const char *WC_RESTRICT text, size_t len);
```

Scan a text buffer for words, applying **case folding** to lowercase.

- `w` must be non-NULL.
- `text`:
  - If `len == 0`, `text` may be `NULL`; the function returns `WC_OK` and does
    nothing.
  - If `len > 0`, `text` must be non-NULL; otherwise, `WC_ERROR` is returned.
- Behavior:
  - Reads exactly `len` bytes, even if `text` contains embedded NULs.
  - Identifies maximal runs of ASCII letters (`A–Z`, `a–z`) as words.
  - Converts letters to lowercase (`'A'`–`'Z'` → `'a'`–`'z'`) using a branchless
    bit trick.
  - Truncates words at `max_word` bytes; only the stored prefix contributes to
    hashing and equality.
  - Non-ASCII bytes (including UTF-8 multibyte sequences) are treated as
    separators.
- Returns:
  - `WC_OK` on success (even if no words are found).
  - `WC_ERROR` for invalid arguments.
  - `WC_NOMEM` if a required allocation fails or the memory budget is
    exhausted.

### Query and Results Functions

```c
size_t wc_total(const wc *w);
size_t wc_unique(const wc *w);
```

Query total and unique word counts.

- If `w == NULL`, returns `0`.
- `wc_total` counts all words added or scanned (including duplicates).
- `wc_unique` counts distinct stored words.

```c
int wc_results(const wc *w,
               wc_word **WC_RESTRICT out,
               size_t  *WC_RESTRICT n);
```

Produce sorted word frequency results.

- Parameters:
  - `w` must be non-NULL.
  - `out`, `n` must be non-NULL pointers.
- Behavior:
  - Allocates a contiguous array of `wc_word` via `WC_MALLOC`.
  - Populates it with all unique words and their counts.
  - Sorts it:
    - Primary: descending by `count`
    - Secondary: ascending lexicographical order (`strcmp`) by `word`
  - The sort is not guaranteed to be stable across equal `(count, word)`, but
    since `(word)` is unique per entry, the ordering is deterministic for a
    given dataset and build.
- Returns:
  - `WC_OK` on success.
    - If `w->len == 0`, then `*out == NULL` and `*n == 0`.
  - `WC_ERROR` if `w == NULL` or `out == NULL` or `n == NULL`, or if internal
    consistency checks fail.
  - `WC_NOMEM` if the temporary results array cannot be allocated.
- Ownership:
  - The array is owned by the caller and must be freed via
    `wc_results_free`.
  - The `word` pointers inside each `wc_word` remain owned by the `wc`
    instance; they become invalid after `wc_close`.

```c
void wc_results_free(wc_word *r);
```

Free a result array returned by `wc_results`.

- Safe to call with `NULL`.
- Uses `WC_FREE` internally.

### Zero-Allocation Iterator API

The iterator API allows enumeration of words and counts **without allocating a
results array**. This is useful under strict memory budgets or when streaming
results.

```c
void wc_cursor_init(wc_cursor *c, const wc *w);
```

Initialize a cursor for iteration.

- `c` may be NULL (in which case the function does nothing).
- `w`:
  - Pointer to a `wc` instance.
  - Must remain valid for the entire duration of iteration.

The cursor does **not** own the `wc` object.

```c
int wc_cursor_next(wc_cursor      *c,
                   const char    **WC_RESTRICT word,
                   size_t        *WC_RESTRICT count);
```

Advance to the next word.

- Parameters:
  - `c` must not be NULL and must have been initialized with
    `wc_cursor_init`.
  - `word`:
    - Optional; if non-NULL, receives a pointer to the stored word.
  - `count`:
    - Optional; if non-NULL, receives the occurrence count.
- Returns:
  - `1` if a word was produced.
  - `0` when iteration is complete or if `c`/`c->w` is NULL.

Iteration order:

- The iterator performs a linear scan of the underlying open-addressed hash
  table.
- Order is **implementation-defined** and **not sorted**.
- It is stable for a given process and build, but not guaranteed across
  versions.

Usage example:

```c
wc *w = wc_open(0);
wc_cursor cur;
const char *word;
size_t count;

/* ... fill w ... */

wc_cursor_init(&cur, w);
while (wc_cursor_next(&cur, &word, &count)) {
    printf("%s -> %zu\n", word, count);
}
```

### Utility Functions

```c
const char *wc_errstr(int rc);
```

Return a static, human-readable description of a result code.

- The returned pointer is valid for the lifetime of the program.
- Must not be freed.
- Returns a generic `"unknown error"` string for unrecognized codes.

```c
const char *wc_version(void);
```

Return the library version string (e.g., `"4.2.0"`).

- The string is static.
- Must not be freed.

```c
const wc_build_config *wc_build_info(void);
```

Return a pointer to a static structure describing the build-time configuration
of this library binary:

- `version_number` – `WC_VERSION_NUMBER`
- `max_word` – `WC_MAX_WORD`
- `min_init_cap` – `WC_MIN_INIT_CAP`
- `min_block_sz` – `WC_MIN_BLOCK_SZ`
- `stack_buffer` – `1` if `WC_STACK_BUFFER` was non-zero at build time,
  otherwise `0`.

The structure is immutable and valid for the lifetime of the program.

Use cases:

- Assert expectations at runtime:

  ```c
  const wc_build_config *cfg = wc_build_info();
  if (cfg->max_word < 256) {
      /* This build does not meet our minimum word length requirements. */
      abort();
  }
  ```

- Detect header/library mismatches in complex deployments (e.g., plugin
  systems).

---

## Word Detection and Normalization

The word model is deliberately simple and ASCII-centric:

- A **word** is a maximal contiguous sequence of ASCII letters:
  - `'A'`–`'Z'` and `'a'`–`'z'`.
- All other bytes are treated as separators:
  - Digits
  - Whitespace
  - Punctuation (e.g., `'`, `-`, `,`, `.`)
  - Non-ASCII bytes (e.g., UTF-8 multibyte sequences)

`wc_scan` lowercases all letters using the bit trick: `c | 32`.

Examples:

```c
// "it's" -> "it", "s"
wc_scan(w, "it's", 4);      // total = 2

// "foo-bar" -> "foo", "bar"
wc_scan(w, "foo-bar", 7);   // total = 2

// "abc123def" -> "abc", "def"
wc_scan(w, "abc123def", 9); // total = 2

// "café" (UTF-8) -> "caf" (é is non-ASCII, treated as separator)
wc_scan(w, "café", 5);
```

Case handling:

- `wc_add` is **case-sensitive**:
  - `"Hello"`, `"HELLO"`, `"hello"` are distinct keys.
- `wc_scan` is **case-insensitive**:
  - `"Hello HELLO hello"` becomes `"hello"` three times.

Truncation:

- If `max_word = 4`:
  - `wc_add("testing")` stores `"test"`.
  - `wc_add("tested")` also stores `"test"` and increments that count.
  - Similarly for words scanned via `wc_scan`.

The hash is computed only over the stored (possibly truncated) prefix; truncation
is intentional and well-defined.

---

## Memory Configuration

### Compile-Time Configuration Macros

The following macros can be defined **before** including `wordcount.h` (and/or
when compiling `wordcount.c`) to tune behavior.

#### Allocator Overrides

```c
#define WC_MALLOC(n)     my_malloc(n)
#define WC_FREE(p)       my_free(p)
#define WC_REALLOC(p, n) my_realloc(p, n)
```

Used for:

- The `wc` handle itself
- Dynamic-mode internal allocations (hash table, arena blocks, heap/static scan
  buffer when `WC_STACK_BUFFER == 0`)
- Temporary arrays returned by `wc_results`

On tiny or specialized systems, you may redirect these to a custom allocator.

#### Stack vs. Heap Scan Buffer

```c
#define WC_STACK_BUFFER 0  /* default is 1 */
```

- When `1` (default):
  - `wc_scan` uses a stack-allocated buffer sized at `WC_MAX_WORD` per call.
- When `0`:
  - `wc_scan` uses a heap/static buffer:
    - Allocated once per `wc` instance (size `max_word`)
    - Freed in `wc_close`
  - In static-buffer mode, this buffer is carved from `static_buf`.

On tiny MCUs or small stacks, `WC_STACK_BUFFER = 0` is recommended.

#### Global Limits for Word Length and Internal Floors

```c
#define WC_MAX_WORD     1024u  /* default upper bound on max_word */
#define WC_MIN_INIT_CAP 16u    /* default minimum initial hash slots */
#define WC_MIN_BLOCK_SZ 256u   /* default minimum initial arena block size */
```

- `WC_MAX_WORD`:
  - Upper bound for runtime `max_word` arguments.
  - Lowering this reduces worst-case stack/heap usage for scan buffers.
- `WC_MIN_INIT_CAP`:
  - Lower bound on initial hash table capacity chosen by the internal tuner.
  - Must be > 0; capacity is always rounded up to a power of two.
- `WC_MIN_BLOCK_SZ`:
  - Lower bound on first arena block size.
  - Lowering this may be useful on tiny static buffers, but reduces the number
    of words that can be stored.

These values are exposed at runtime via `wc_build_info` (`max_word`,
`min_init_cap`, `min_block_sz`).

#### Default Initial Sizing

If not overridden, these are derived from `SIZE_MAX`:

```c
#define WC_DEFAULT_INIT_CAP ...
#define WC_DEFAULT_BLOCK_SZ ...
```

Defaults:

- 16-bit `size_t`:
  - `WC_DEFAULT_INIT_CAP = 128`
  - `WC_DEFAULT_BLOCK_SZ = 1024`
- 32-bit `size_t`:
  - `WC_DEFAULT_INIT_CAP = 1024`
  - `WC_DEFAULT_BLOCK_SZ = 16384`
- 64-bit `size_t`:
  - `WC_DEFAULT_INIT_CAP = 4096`
  - `WC_DEFAULT_BLOCK_SZ = 65536`

These macros **can be overridden independently**. If you define one and not the
other, the library uses your override plus the default for the other.

#### Language / ABI Integration: `WC_RESTRICT`

```c
#define WC_RESTRICT  /* see default in header */
```

By default:

- In C99 and later, `WC_RESTRICT` expands to `restrict`.
- In C++, `WC_RESTRICT` expands to a compiler-specific extension where
  available (`__restrict` or `__restrict__`), or nothing on compilers without
  such support.

You may override this if you want a specific restrict-like qualifier in C++ or
non-standard environments.

#### Assertions

The implementation uses internal assertions via `assert` by default:

```c
#define WC_OMIT_ASSERT
```

- If `WC_OMIT_ASSERT` is defined when compiling `wordcount.c`, internal
  assertions are disabled (`WC_ASSERT` becomes a no-op).
- This does not change public API behavior; it only affects internal
  sanity-checks during development.

### Runtime Limits via `wc_limits`

Example: dynamic mode with a 1 MiB budget:

```c
wc_limits lim;
memset(&lim, 0, sizeof lim);
lim.max_bytes = 1 * 1024 * 1024;  /* 1 MiB */

wc *w = wc_open_ex(0, &lim);
```

Example: static-buffer mode for an embedded system:

```c
static unsigned char pool[2048];

wc_limits lim;
memset(&lim, 0, sizeof lim);
lim.static_buf  = pool;
lim.static_size = sizeof pool;

wc *w = wc_open_ex(32, &lim);
if (!w) {
    /* pool too small even for minimal structures under configured limits */
}
```

Randomized hashing for untrusted inputs:

```c
#include <time.h>

wc_limits lim;
memset(&lim, 0, sizeof lim);
lim.hash_seed = (unsigned long)time(NULL);  /* Simple per-process seed */

wc *w = wc_open_ex(0, &lim);
```

In static-buffer mode, `wc_open_ex` will:

- Use a scratch allocator state to **simulate** minimal internal allocations:
  - Hash table
  - First arena block
  - Scan buffer (if `WC_STACK_BUFFER == 0`)
- Fail early (`NULL`) if the configuration cannot satisfy those allocations
  under the effective budget (`min(static_size, max_bytes)` when both are set).

---

## Building and Integration

### Using CMake (Presets, Recommended)

This repository ships CMake presets for multiple compilers and targets.

Common workflow (from project root):

```bash
# Configure and build with Clang + sanitizers
cmake --preset clang
cmake --build --preset clang

# Run tests for that preset
ctest --preset clang
```

Available configure presets (see `CMakePresets.json`):

- `clang` – Clang, sanitizers enabled (`ENABLE_SANITIZERS=ON`)
- `gcc` – GCC, sanitizers enabled
- `tcc` – TinyCC (no sanitizers)
- `mingw` – MinGW cross-compile target (Windows)

Each configure preset has a matching build and test preset.

These builds produce:

- Static libraries:
  - `libwordcount_lib.a` – default configuration (stack scan buffer)
  - `libwordcount_lib_heap.a` – heap/static scan buffer (`WC_STACK_BUFFER=0`)
  - `libwordcount_lib_tiny.a` – tiny-profile config (reduced floors,
    small `WC_MAX_WORD`)
- Executables:
  - `wc` – CLI tool
  - `wc_test`, `wc_test_heap`, `wc_test_tiny` – test binaries for each profile

### One-shot Build & Quality Script

For a full “gamut” run across available toolchains plus static analysis:

```bash
./c-build.sh
```

This script:

1. Configures, builds, and tests:
   - `clang` preset
   - `gcc` preset
2. Optionally, if available on your system:
   - `tcc` preset (TinyCC)
   - `mingw` preset (x86_64-w64-mingw32)
3. Runs `./c-quality.sh build-clang`, which applies:
   - `clang-format` (in-place)
   - `clang-tidy`
   - `cppcheck`

`c-quality.sh` expects `clang-format`, `clang-tidy`, and `cppcheck` to be
installed. It uses `compile_commands.json` from the specified build directory
when available.

### Direct Compilation

To embed in another project without CMake:

```bash
cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
   wordcount.c your_program.c -o your_program
```

If you need the CLI:

```bash
cc -std=c99 -O2 \
   wordcount.c wc_main.c -o wc
```

For heap/static-based scan buffer (no large stack arrays):

```bash
cc -std=c99 -O2 -DWC_STACK_BUFFER=0 \
   wordcount.c your_program.c -o your_program
```

### Directory Layout

Relevant source files:

```text
.
├── CMakeLists.txt         # Build configuration
├── CMakePresets.json      # Preset builds: clang, gcc, tcc, mingw
├── c-build.sh             # Multi-preset build + test + static analysis
├── c-quality.sh           # clang-format, clang-tidy, cppcheck runner
├── mingw-w64.cmake        # Optional MinGW toolchain file
├── wordcount.h            # Public API
├── wordcount.c            # Core implementation
├── wc_main.c              # CLI tool
├── wc_test.c              # Test suite (including optional OOM harness)
├── LICENSE                # Unlicense (public domain dedication)
└── README.md              # This document
```

Build directories created by CMake (not part of the API):

```text
./build*                    # e.g., build, build-clang, build-gcc, build-tcc, build-mingw
```

---

## CLI Tool (`wc`)

The `wc` executable in `wc_main.c` is a simple command-line interface built on
top of the library.

### Usage

```bash
wc [file ...]
```

- With no arguments:
  - Reads from `stdin` (streaming, bounded memory).
- With one or more filenames:
  - Processes each file in order.
  - Aggregates word counts across all files.

Examples:

```bash
./wc book.txt
./wc file1.txt file2.txt
cat file.txt | ./wc
```

### Memory-Mapped I/O and Streaming

- For regular files:
  - On POSIX:
    - Uses `open`, `fstat`, `mmap`, `madvise`, `close`.
  - On Windows:
    - Uses UTF-16 APIs for correct Unicode handling of filenames:
      - `GetCommandLineW`, `CommandLineToArgvW` to obtain a UTF-16 argv.
      - Conversion to UTF-8 for the library and back to UTF-16 for file APIs.
    - Uses `CreateFileW`, `CreateFileMappingW`, `MapViewOfFile`,
      `UnmapViewOfFile`, `CloseHandle`.
  - Files too large for `size_t` (e.g., on 32-bit) are rejected with `EFBIG`.
  - Empty files are allowed and simply yield no words.

- For stdin:
  - Processed in **streaming chunks** (`STDIN_CHUNK`, currently 65536 bytes).
  - A small internal carry buffer tracks words that span chunk boundaries.
  - Semantics are equivalent to a single `wc_scan` on the entire stdin stream,
    but host memory usage remains bounded.

### Environment-Based Limits

The CLI honors the `WC_MAX_BYTES` environment variable:

```bash
WC_MAX_BYTES=8388608 ./wc largefile.txt  # 8 MiB soft cap
```

Semantics:

- Parsed as an unsigned integer number of bytes.
- If invalid, the CLI prints an error and exits non-zero.
- If set and valid, the CLI passes `max_bytes` via `wc_limits` to `wc_open_ex`.
- The limit caps **internal** library allocations (hash table, arena, scan
  buffer), not the file mapping itself.

### Output Format

Top 10 words (stdout):

```text
  Count  Word                  %
-------  --------------------  ------
   5432  the                   2.34
   3210  and                   1.82
   ...
```

Summary (stderr):

```text
Total: 928012  Unique: 33782
```

If no words were found, the CLI prints (to stderr):

```text
No words found.
```

Exit codes:

- `0` if all files/stdin were processed successfully (even if no words found).
- Non-zero if:
  - A file could not be opened/mapped.
  - `WC_MAX_BYTES` was invalid.
  - An internal error occurred (e.g., irrecoverable `WC_ERROR` from the
    library).

---

## Testing, Fuzzing, and OOM Injection

### Core Test Suite

Build and run with CMake:

```bash
cmake --preset clang
cmake --build --preset clang
ctest --preset clang
```

Or manually from a build directory:

```bash
cmake ..
make wc_test wc_test_heap wc_test_tiny
ctest
```

The test suite covers:

- Lifecycle and limits:
  - `wc_open`, `wc_open_ex`, `wc_close`, NULL safety
  - `max_word` clamping, tiny budgets, static-buffer enforcement
- `wc_add`:
  - Single, duplicate, multiple words
  - Empty strings, NULL handling
  - Truncation and truncation collision semantics
- `wc_scan`:
  - Simple cases, case folding
  - Punctuation, digits, “no words” input
  - Empty input, NULL handling, embedded NUL bytes
  - Truncation and truncation collisions
- `wc_results`:
  - Sorting by count then alphabetically
  - Empty results, NULL argument validation
- Queries:
  - `wc_total` / `wc_unique` on NULL
  - Version and error strings
  - Build configuration via `wc_build_info`
- Stress:
  - Many unique words
  - Many duplicates
  - Hash table growth and multi-block arenas
- Static-buffer mode:
  - Minimum viable static buffer size detection
  - Behavior when the static arena and table are exhausted (`WC_NOMEM` paths)

Separate test binaries exercise:

- `wc_test` – default profile (stack scan buffer)
- `wc_test_heap` – heap/static scan buffer (`WC_STACK_BUFFER=0`)
- `wc_test_tiny` – tiny-profile configuration
  (`WC_STACK_BUFFER=0`, reduced floors, small `WC_MAX_WORD`)

### OOM Injection (glibc-specific)

When built with `-DWC_TEST_OOM` on **glibc-based** systems, `wc_test.c`
interposes `malloc` and `realloc` using glibc’s internal `__libc_malloc` /
`__libc_realloc` symbols to simulate allocation failures at specific call
counts.

Build (glibc only):

```bash
cc -std=c99 -O0 -g -DWC_TEST_OOM \
   wordcount.c wc_test.c -o wc_test_oom
./wc_test_oom
```

OOM tests exercise:

- Failures during `wc_open`
- Failures during `wc_add`, `wc_scan`, `wc_results`
- Failures during hash table growth
- Failures during arena growth
- A “torture” mode that iterates many failure points in sequence, ensuring:
  - No memory corruption on `WC_NOMEM`
  - No double-frees
  - Valid `wc_results` after partial failures

On non-glibc platforms, or when `WC_TEST_OOM` is not defined, the OOM harness is
stubbed out and these tests are skipped. For portable OOM testing, compile
`wordcount.c` with custom allocator macros and provide your own interposition.

### Fuzzing and Sanitizers

The library is designed to be **fuzzable**:

- Recommended fuzzing targets:
  - `wc_scan` with arbitrary byte inputs (including long runs of punctuation
    and non-ASCII).
  - Sequences of `wc_open_ex`, `wc_add`, `wc_scan`, `wc_results`, `wc_cursor`
    and `wc_close` with randomized parameters and data.
- Recommended sanitizers (where supported by your toolchain):
  - AddressSanitizer (ASan)
  - UndefinedBehaviorSanitizer (UBSan)
  - LeakSanitizer (LSan)

The absence of global mutable state and the simple allocator design make it
easy to drive from multiple fuzzer harnesses in parallel.

---

## Portability and Platform Assumptions

### Language and Library

- Requires **C99** for the core library:
  - Uses `restrict` (via `WC_RESTRICT` macro), `<stdint.h>` (for `SIZE_MAX`),
    `<stddef.h>`, and `size_t`.
- Uses only the C standard library:
  - `malloc`, `free`, `realloc` (via macros)
  - `memcpy`, `memset`, `memcmp`
  - `strlen`, `strcmp`, `qsort`
  - `assert` (optional; can be disabled with `WC_OMIT_ASSERT`)

The implementation uses C11 `_Static_assert` when available, but falls back to a
C99-compatible pattern when not. The **minimum requirement remains C99** on
supported targets.

The CLI (`wc_main.c`) additionally uses POSIX or Win32 APIs.

### Character Encoding

Compile-time checks enforce:

- `CHAR_BIT == 8`
- ASCII-compatible encoding:
  - `'A' == 65`, `'Z' == 90`
  - `'a' == 97`, `'z' == 122`
  - `'a' ^ 'A' == 32`

If these are not true (e.g., on EBCDIC), the build will fail via a
compile-time assertion.

### Integer and Pointer Assumptions

The implementation assumes a conventional hosted C environment:

- The internal alignment union used by the bump allocator (`void *`, `size_t`,
  `unsigned long`) is sufficient to satisfy alignment requirements for all
  internal allocations.
- All size calculations are explicitly overflow-checked before any allocation
  request.
- The `wc_limits.static_buf` alignment checks use `uintptr_t` when available
  and fall back to `size_t` otherwise.

The implementation is portable across:

- 16-bit, 32-bit, and 64-bit `size_t`
- Little- and big-endian architectures
- Hosted environments with a conventional C standard library

### C++ Integration

The header is usable from C++:

- All functions are wrapped in `extern "C" { ... }` when `__cplusplus` is
  defined.
- The public API uses `WC_RESTRICT` instead of raw `restrict`, which:
  - Expands to `restrict` in C99 and later.
  - Maps to a compiler-appropriate extension (or nothing) in C++ by default.

You may redefine `WC_RESTRICT` to `__restrict__` or similar if you rely on
compiler-specific aliasing hints in C++ builds.

### Freestanding vs. Hosted

The library assumes a **hosted** environment with a working C standard
library. On freestanding MCUs:

- You may need to provide:
  - Implementations of `malloc`, `free`, `memcpy`, etc., or
  - Custom `WC_MALLOC`, `WC_FREE` macros.
- Static-buffer mode (`wc_limits.static_buf` / `static_size`) is the intended
integration path for systems without a general-purpose allocator.

---

## Adversarial Inputs and Complexity

The library is robust against:

- Arbitrary byte sequences (including embedded NULs and non-ASCII bytes) in
  `wc_scan`.
- Very long inputs, as long as `size_t` can represent the length and configured
  limits are not exceeded.
- Allocation failures at any internal call site (mapped to `WC_NOMEM`).

However, it is **not** designed to be cryptographically strong or DoS-hard at
the hash-function level:

- FNV-1a is predictable; an attacker who can control input contents and knows
  the seed may force many collisions in the open-addressed table.
- The implementation uses linear probing; worst-case probe chain length can be
  large on adversarial input.

For untrusted, network-facing use where adversaries can craft inputs freely:

- Consider:
  - Using a randomized `hash_seed` via `wc_limits`.
  - Pre-limiting maximum text length per request.
  - Running in a process/jail with CPU and memory limits.
  - Applying pre-filtering or sampling, depending on the threat model.

For most non-hostile applications (analytics tools, batch processing, CLI
usage), the current design is more than adequate.

---

## ABI and Build Configuration Compatibility

`wordcount` is intentionally simple at the ABI level:

- All public types are either:
  - Opaque (`struct wc`), or
  - Plain C structs with documented fields (`wc_limits`, `wc_word`,
    `wc_build_config`, `wc_cursor`).
- No inline functions are exposed in the header.
- No public macros affect layout of opaque types.

However, build-time configuration **does** matter for:

- `WC_MAX_WORD` (affects stack/heap buffer size).
- `WC_MIN_INIT_CAP`, `WC_MIN_BLOCK_SZ`, `WC_DEFAULT_*` (affect sizing and
  memory behavior).
- `WC_STACK_BUFFER` (stack vs heap/static for scan buffers).

To help detect mismatches:

- `wc_build_info()` returns a `wc_build_config` struct describing the library
  binary’s build-time parameters.
- Applications can compare these against:
  - The macros compiled into the application (e.g. via `#if`/`#error`), or
  - Hard-coded expectations.

Example runtime check:

```c
const wc_build_config *cfg = wc_build_info();

if (cfg->version_number / 1000000UL != WC_VERSION_NUMBER / 1000000UL) {
    /* Linked against a different major version than we were compiled with. */
    abort();
}

/* Optional: enforce minimum word length */
if (cfg->max_word < 256) {
    abort();
}
```

Guidelines:

- For static linking or single-project builds, you normally don’t need to
  worry.
- For shared-library deployments or plugin architectures:
  - Treat mismatches as configuration errors and check via `wc_build_info`
    early in process startup.

---

## Performance and Complexity

### Time Complexity

| Operation       | Complexity       | Notes                                  |
|----------------|------------------|----------------------------------------|
| `wc_add`       | Amortized O(1)   | Single hash lookup and possible insert |
| `wc_scan`      | O(n)             | Single pass over `len` bytes           |
| `wc_total`     | O(1)             | Maintained counter                     |
| `wc_unique`    | O(1)             | Maintained counter                     |
| `wc_results`   | O(U log U)       | `U = wc_unique(w)`, qsort over entries |
| `wc_cursor_*`  | O(U) total       | Single linear scan of hash table       |

### Space Complexity

- Hash table:
  - `O(U)` slots, where `U` is the number of unique words.
- Arena:
  - `O(T)` bytes, where `T` is total bytes across all stored word strings
    (subject to truncation).
- Temporary results array (in `wc_results`):
  - `O(U)` additional memory.

In static-buffer mode:

- Total internal usage is ≤ `static_size` (and ≤ `max_bytes` if set).
- Once the pool is exhausted, operations return `WC_NOMEM` but remain safe.

---

## Versioning and Stability Guarantees

The API is explicitly marked as **stable**:

- Function signatures are not changed or removed in minor/patch releases.
- New functions may be added in minor/patch releases.
- New fields may be added to `wc_limits` (and similar structs) in the future:
  - Callers must always zero-initialize these structs so new fields default to
    0.

Version macros:

```c
#define WC_VERSION        "4.2.0"
#define WC_VERSION_NUMBER 4002000UL  /* MAJOR * 1,000,000 + MINOR * 1,000 + PATCH */
```

- `wc_version()` returns the string version.
- `wc_build_info()->version_number` returns the numeric version used when
  building the library binary.

Semantics:

- **Major** (4.x.x → 5.x.x):
  - May introduce breaking changes.
  - The library aims to avoid this unless absolutely necessary.
- **Minor** (4.2.x → 4.3.x):
  - May add functions and fields, but will not remove or change existing
    signatures.
- **Patch** (4.2.0 → 4.2.1):
  - Bug fixes and internal improvements only.

---

## Guidelines for Contributors

When modifying or extending the library:

1. **Preserve C99 and simple dependencies**
   - No C11-only or non-standard **requirements** in `wordcount.c` /
     `wordcount.h`.
   - Optional use of `_Static_assert` is allowed with a safe fallback.
   - No additional external dependencies (beyond the C standard library) in
     the core library.

2. **Maintain robustness properties**
   - All size calculations must be overflow-checked.
   - All allocations must be checked for failure and mapped to `WC_NOMEM`.
   - No undefined behavior on malformed or adversarial input on supported
     platforms.

3. **Keep configuration data-driven**
   - Prefer extending `wc_limits`, `wc_build_config`, or configuration macros
     over introducing ad-hoc global variables.
   - Tiny/embedded configurations should remain first-class (testable,
     documented).

4. **Update tests**
   - Add unit tests for any new behavior.
   - Run all configurations:
     - `wc_test` (default)
     - `wc_test_heap` (heap/static scan buffer)
     - `wc_test_tiny` (tiny profile)
     - `wc_test_oom` where applicable
   - If you introduce new behavior relevant to adversarial inputs or memory
     limits, extend tests accordingly.

5. **Use the quality tooling**
   - Run `./c-quality.sh build-clang` to apply `clang-format`, `clang-tidy`,
     and `cppcheck`.
   - Keep code style consistent with existing files:
     - Functions small and focused where reasonable.
     - Use `goto`-cleanup idiom for multi-resource functions in CLI or complex
       flows.
     - Comments explain **why**, not just **what**.
     - Avoid micro-optimizations that significantly reduce clarity unless they
       are backed by benchmarks in realistic scenarios.

Pull requests that improve clarity, correctness, or test coverage without
increasing complexity are preferred over speculative micro-optimizations.

---

## License

All library files are released into the **public domain** (see `LICENSE` and
file headers for details).

You may use, copy, modify, and distribute this library for any purpose, without
restriction.

For jurisdictions that do not recognize the public domain as such, the project
ships the standard *Unlicense* text; see:

- `LICENSE`
- <https://unlicense.org>
