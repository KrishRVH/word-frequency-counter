/* wordcount.h - Word frequency counter
**
** Public domain.
**
** STABILITY
**
**   API is stable. Functions will not be removed or change signature.
**   New functions may be added in minor/patch releases.
**   Separate wc objects may be used from different threads.
**   A single wc object must not be shared without synchronization.
**
** RETURN VALUES
**
**   Functions returning int use: WC_OK (0) for success, WC_ERROR (1)
**   for bad arguments or corrupt state, WC_NOMEM (2) for allocation
**   failure. Query functions (wc_total, wc_unique) return 0 on NULL.
**
**   Use wc_errstr() to get a human-readable description of any error
**   code. The returned string is static and must not be freed.
**
** CASE HANDLING
**
**   wc_add()  - case-sensitive: "Hello" and "hello" are distinct
**   wc_scan() - normalizes to lowercase: "Hello" becomes "hello"
**
** WORD DETECTION
**
**   Only ASCII letters (A-Z, a-z) are recognized as word characters.
**   All other bytes (including UTF-8 multibyte sequences) are treated
**   as word separators. This library assumes an ASCII-compatible
**   execution character set (true for ASCII, UTF-8, ISO-8859-*).
**
** WORD LENGTH
**
**   Both functions truncate words exceeding max_word. The hash is
**   computed only over stored characters, ensuring truncated forms
**   of different words collide correctly.
**
**   At the API level, max_word is clamped into [4, WC_MAX_WORD].
**   WC_MAX_WORD defaults to 1024 but can be lowered at compile time
**   for constrained targets (see configuration macros below).
**
** MEMORY CONFIGURATION
**
**   Define WC_MALLOC and WC_FREE before including this header to
**   redirect memory allocation (e.g., to a custom arena or debug
**   allocator). Defaults to stdlib malloc/free.
**
**   WC_REALLOC may also be defined for client code; the core library
**   currently uses only WC_MALLOC and WC_FREE internally.
**
**   For finer control, wc_open_ex() accepts a wc_limits struct that
**   can bound total internal allocations for a wc instance and tune
**   the initial hash table capacity and arena block size. This makes
**   it practical to use on very small systems with fixed memory
**   budgets.
**
**   TINY / STATIC MODE
**
**   For freestanding or MCU-style environments without a usable
**   malloc/free, wc_limits also supports a caller-supplied static
**   buffer. When static_buf/static_size are set, all internal
**   allocations (hash table, arena blocks, optional heap scan buffer)
**   are carved out of that region using a bump allocator. In this
**   mode:
**
**     - The hash table never grows. Once the 0.7 load factor is
**       reached, further inserts fail with WC_NOMEM.
**
**     - The arena never allocates additional blocks. Once the initial
**       block is full, further inserts fail with WC_NOMEM.
**
**     - The wc handle itself and any arrays returned by wc_results()
**       are still allocated via WC_MALLOC/WC_FREE. On tiny systems
**       you can redirect these macros to your own allocator.
**
**   The static buffer must remain valid and exclusively owned by the
**   wc object for its entire lifetime. It must be suitably aligned
**   for the types used internally by the library (at least as strict
**   as alignment for void*, size_t, and unsigned long). On platforms
**   where uintptr_t is available, misaligned buffers are detected and
**   cause wc_open_ex() to fail.
**
** BUILD CONFIGURATION
**
**   WC_OMIT_ASSERT  - Define to disable internal assertions (smaller
**                     code, but less safety checking in debug builds).
**
**   WC_STACK_BUFFER - Define as 0 to heap-allocate scan buffers
**                     instead of using stack. Useful for constrained
**                     stack environments. Default is 1 (use stack).
**
**   WC_MAX_WORD     - Compile-time upper bound for max_word. Defaults
**                     to 1024. Lowering this reduces worst-case stack
**                     or heap usage for scan buffers.
**
**   WC_MIN_INIT_CAP - Compile-time lower bound for the initial hash
**                     table capacity chosen by the internal tuner.
**                     Defaults to 16. May be lowered for tiny-memory
**                     builds at the cost of fewer slots.
**
**   WC_MIN_BLOCK_SZ - Compile-time lower bound for the size in bytes
**                     of the first arena block chosen by the tuner.
**                     Defaults to 256. May be lowered on very small
**                     static buffers; when reduced too far, fewer
**                     distinct words can be stored before WC_NOMEM.
**
**   WC_DEFAULT_INIT_CAP / WC_DEFAULT_BLOCK_SZ
**                   - Compile-time defaults for initial hash table
**                     capacity and arena block size. See below. They
**                     may be overridden independently.
**
** PORTABILITY
**
**   Requires C99. Uses only types guaranteed by C99 (no optional
**   exact-width types) in the public API. Works on any hosted
**   implementation with 8-bit chars and ASCII-compatible character
**   encoding. C11 _Static_assert is used internally when available,
**   with a C99-compatible fallback.
*/
#ifndef WORDCOUNT_H
#define WORDCOUNT_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** Restrict qualifier abstraction.
**
**   - In C99 or later, WC_RESTRICT expands to 'restrict'.
**   - In C++, it uses compiler-specific extensions (__restrict) where available,
**     or expands to nothing if not supported.
*/
#ifndef WC_RESTRICT
#  if defined(__cplusplus)
     /* C++ does not have standard 'restrict', but most compilers support it. */
#    if defined(_MSC_VER)
#      define WC_RESTRICT __restrict
#    elif defined(__GNUC__) || defined(__clang__)
#      define WC_RESTRICT __restrict__
#    else
#      define WC_RESTRICT
#    endif
#  elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
     /* C99 standard restrict */
#    define WC_RESTRICT restrict
#  else
     /* C89 or pre-standard environment */
#    define WC_RESTRICT
#  endif
#endif

/*
** Version information. The version number is encoded as:
**   (MAJOR * 1000000) + (MINOR * 1000) + PATCH
**
** 4.2.0 introduces optional static-buffer support via wc_limits.
*/
#define WC_VERSION "4.2.0"
#define WC_VERSION_NUMBER 4002000UL

/*
** Result codes for int-returning functions.
*/
#define WC_OK 0    /* Success */
#define WC_ERROR 1 /* Generic error (bad args, corrupt state) */
#define WC_NOMEM 2 /* Memory allocation failed */

/*
** Memory allocator configuration. Define these before including
** wordcount.h to use a custom allocator.
*/
#ifndef WC_MALLOC
#define WC_MALLOC(n) malloc(n)
#endif
#ifndef WC_REALLOC
#define WC_REALLOC(p, n) realloc((p), (n))
#endif
#ifndef WC_FREE
#define WC_FREE(p) free(p)
#endif

/*
** Stack buffer configuration. Set to 0 for heap allocation.
**
** On tiny MCUs or deeply recursive call stacks, defining
** WC_STACK_BUFFER as 0 is recommended to avoid large fixed-size
** arrays on the stack. In that mode, scan buffers are allocated
** from the same internal pools that store words and hash slots.
*/
#ifndef WC_STACK_BUFFER
#define WC_STACK_BUFFER 1
#endif

/*
** Optional compile-time tuning for tiny/embedded targets.
**
** WC_MAX_WORD:
**   Upper bound on max_word accepted by wc_open/wc_open_ex.
**   Defaults to 1024. Lowering this reduces worst-case stack or
**   heap usage for scan buffers. The implementation will clamp
**   the runtime max_word argument into [4, WC_MAX_WORD].
**
** WC_MIN_INIT_CAP:
**   Lower bound on the initial hash table capacity (number of
**   slots) chosen by the internal tuner. Defaults to 16. May be
**   lowered for very small memory configurations; values must be
**   > 0 and are rounded up to a power of two internally.
**
** WC_MIN_BLOCK_SZ:
**   Lower bound on the first arena block size in bytes. Defaults
**   to 256. May be lowered for tiny static buffers. Reducing this
**   too far will limit how many distinct words can be stored
**   before WC_NOMEM is returned.
*/
#ifndef WC_MAX_WORD
#define WC_MAX_WORD 1024u
#endif
#ifndef WC_MIN_INIT_CAP
#define WC_MIN_INIT_CAP 16u
#endif
#ifndef WC_MIN_BLOCK_SZ
#define WC_MIN_BLOCK_SZ 256u
#endif

/*
** Opaque word counter handle.
*/
typedef struct wc wc;

/*
** Default sizing for initial hash table capacity and arena block
** size. These can be overridden at compile time by defining
** WC_DEFAULT_INIT_CAP and/or WC_DEFAULT_BLOCK_SZ before including
** this header. If not defined, they are derived from SIZE_MAX.
*/
#ifndef WC_DEFAULT_INIT_CAP
#if SIZE_MAX <= 65535u
#define WC_DEFAULT_INIT_CAP 128u
#elif SIZE_MAX <= 4294967295u
#define WC_DEFAULT_INIT_CAP 1024u
#else
#define WC_DEFAULT_INIT_CAP 4096u
#endif
#endif

#ifndef WC_DEFAULT_BLOCK_SZ
#if SIZE_MAX <= 65535u
#define WC_DEFAULT_BLOCK_SZ 1024u
#elif SIZE_MAX <= 4294967295u
#define WC_DEFAULT_BLOCK_SZ 16384u
#else
#define WC_DEFAULT_BLOCK_SZ 65536u
#endif
#endif

/*
** Optional per-instance memory and sizing limits.
**
**   max_bytes:
**     Hard cap on total internal allocations for this wc object when
**     using the default dynamic allocator. The following pools are
**     counted against this limit:
**       - the hash table (Slot array and its growth)
**       - the arena blocks used for word storage
**       - the optional heap scan buffer when WC_STACK_BUFFER==0
**
**     The wc handle itself (the struct wc) and any arrays returned
**     by wc_results() are NOT counted, since their lifetime and
**     ownership are under the caller's control. 0 = unlimited.
**
**   init_cap:
**     Initial hash table capacity (number of slots). Must be > 0.
**     Rounded up to a power of two internally. 0 = library default
**     chosen from WC_DEFAULT_INIT_CAP based on platform.
**
**   block_size:
**     Arena block size in bytes. Acts as the typical allocation
**     quantum for word storage. 0 = library default chosen from
**     WC_DEFAULT_BLOCK_SZ based on platform.
**
**   static_buf / static_size:
**     Optional caller-supplied memory region for all internal
**     allocations. When static_buf is non-NULL and static_size > 0:
**
**       - The library does NOT call WC_MALLOC/WC_FREE for internal
**         structures (hash table, arena blocks, heap scan buffer).
**
**       - All such objects are carved out of [static_buf,
**         static_buf + static_size) using a simple bump allocator,
**         with alignment chosen to be safe for the internal types
**         used by the library.
**
**       - The buffer must be suitably aligned; on hosted
**         implementations, alignment at least as strict as that of
**         void*, size_t, and unsigned long is sufficient. When
**         uintptr_t is available, misaligned buffers are detected at
**         runtime and cause wc_open_ex() to fail.
**
**       - The buffer must remain valid and must not be shared with
**         another wc instance for its entire lifetime.
**
**       - max_bytes, if non-zero, is treated as an additional guard
**         and is clamped to static_size when computing initial
**         sizing.
**
**     The wc handle itself and any arrays returned by wc_results()
**     are still allocated via WC_MALLOC/WC_FREE. On very small
**     systems, redirect those macros to your own allocator.
**
** On small systems, set max_bytes or static_size to a fixed budget
** and leave the others at 0 to let the library derive conservative
** values. On larger systems, you can tune init_cap/block_size
** directly.
**
** NOTE: This struct may grow in future releases. Always initialize
** it by zeroing the whole struct (e.g., via memset or a compound
** literal with omitted fields) so new fields default to 0.
*/
typedef struct wc_limits {
    size_t max_bytes;
    size_t init_cap;
    size_t block_size;
    void *static_buf;
    size_t static_size;
    /* Set to 0 for deterministic behavior (default)
       Set to random value for DoS protection. */
    unsigned long hash_seed;
} wc_limits;

/*
** Result entry returned by wc_results().
*/
typedef struct wc_word {
    const char *word;
    size_t count;
} wc_word;

/*
** Build-time configuration introspection.
**
**   version_number:
**     Equal to WC_VERSION_NUMBER.
**
**   max_word:
**     Compile-time WC_MAX_WORD used when building this library.
**
**   min_init_cap / min_block_sz:
**     Compile-time WC_MIN_INIT_CAP / WC_MIN_BLOCK_SZ used when
**     building this library.
**
**   stack_buffer:
**     1 if WC_STACK_BUFFER was non-zero at build time, 0 otherwise.
**
** This is useful to detect header/library mismatches across
** translation units or during dynamic loading.
*/
typedef struct wc_build_config {
    unsigned long version_number;
    size_t max_word;
    size_t min_init_cap;
    size_t min_block_sz;
    int stack_buffer; /* 1 = stack, 0 = heap/static */
} wc_build_config;

/*
** Create a new word counter with optional limits.
**
**   max_word: Maximum word length to store. 0 = default (64).
**             Clamped to range [4, WC_MAX_WORD].
**
**   limits:   Optional pointer to a wc_limits struct. May be NULL.
**
** Returns NULL on allocation failure or if the supplied limits are
** impossible to satisfy (e.g., max_bytes/static_size too small for
** even minimal internal structures).
*/
wc *wc_open_ex(size_t max_word, const wc_limits *limits);

/*
** Create a new word counter with default limits (no explicit memory
** cap, platform-tuned defaults for table and arena sizes).
**
**   max_word: Maximum word length to store. 0 = default (64).
**             Clamped to range [4, WC_MAX_WORD].
**
** Returns NULL on allocation failure.
*/
wc *wc_open(size_t max_word);

/*
** Destroy a word counter. NULL-safe.
*/
void wc_close(wc *w);

/*
** Add a single word (case-sensitive, truncates at max_word).
** Empty strings are ignored. Returns WC_OK, WC_ERROR, or WC_NOMEM.
*/
int wc_add(wc *w, const char *WC_RESTRICT word);

/*
** Scan text for words (lowercases, truncates at max_word).
** Non-alphabetic characters are word separators.
** Returns WC_OK, WC_ERROR, or WC_NOMEM.
**
** If len == 0, text may be NULL and WC_OK is returned.
*/
int wc_scan(wc *w, const char *WC_RESTRICT text, size_t len);

/*
** Query total word count. Returns 0 if w is NULL.
*/
size_t wc_total(const wc *w);

/*
** Query unique word count. Returns 0 if w is NULL.
*/
size_t wc_unique(const wc *w);

/*
** Get sorted results (by count desc, then alphabetically).
**
**   out: Receives pointer to array (caller must free via
**        wc_results_free)
**   n:   Receives array length
**
** Returns WC_OK, WC_ERROR (bad args), or WC_NOMEM.
** On empty results, *out=NULL and *n=0 with WC_OK return.
**
** Note: The temporary results array is allocated via WC_MALLOC and
** is not counted against max_bytes in wc_limits, nor against any
** static buffer provided via wc_limits.static_buf, since its
** lifetime is entirely under the caller's control.
*/
int wc_results(const wc *w, wc_word **WC_RESTRICT out, size_t *WC_RESTRICT n);

/*
** Free results array. NULL-safe.
*/
void wc_results_free(wc_word *r);

/*
** -------------------------------------------------------------------------
** Zero-allocation Iterator API
** -------------------------------------------------------------------------
**
** Allows iterating over all words without allocating a results array.
** Useful for strict memory budgets or streaming processing.
**
** Note: Iteration order is arbitrary (based on hash table layout) and
** is NOT sorted.
*/
typedef struct wc_cursor {
    const wc *w;
    size_t index; 
} wc_cursor;

/*
** Initialize a cursor for iteration.
** w must remain valid during iteration.
*/
void wc_cursor_init(wc_cursor *c, const wc *w);

/*
** Advance to the next word.
**
**   word:  Receives pointer to the stored word string.
**   count: Receives the occurrence count.
**
** Returns 1 if a word was found, 0 if iteration is complete.
*/

int wc_cursor_next(wc_cursor *c, const char **WC_RESTRICT word, size_t *WC_RESTRICT count);
/*
** Return human-readable error description.
** The returned string is static and must not be freed.
*/
const char *wc_errstr(int rc);

/*
** Return version string.
*/
const char *wc_version(void);

/*
** Return build-time configuration.
**
** The returned pointer refers to a static, immutable struct. It
** remains valid for the lifetime of the program.
*/
const wc_build_config *wc_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* WORDCOUNT_H */
