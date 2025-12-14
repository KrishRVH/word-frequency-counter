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
**   failure / memory limit reached.
**
**   Query functions (wc_total, wc_unique) return 0 on NULL.
**
**   Use wc_errstr() to get a human-readable description of any error
**   code. The returned string is static and must not be freed.
**
** CASE HANDLING
**
**   wc_add()  - case-sensitive: "Hello" and "hello" are distinct
**   wc_scan() - normalizes to lowercase: "Hello" becomes "hello"
**
** WORD DETECTION (wc_scan)
**
**   Only ASCII letters (A-Z, a-z) are recognized as word characters.
**   All other bytes (including UTF-8 multibyte sequences) are treated
**   as word separators.
**
** WORD LENGTH
**
**   Both functions truncate words exceeding max_word. The hash is
**   computed only over stored characters, ensuring truncated forms
**   of different words collide correctly.
**
**   At the API level, max_word is clamped into [4, WC_MAX_WORD].
**   WC_MAX_WORD defaults to 1024 but can be lowered at compile time
**   for constrained targets.
**
** HASHING / ADVERSARIAL INPUTS
**
**   The library uses a 32-bit FNV-1a hash internally. The stored hash
**   value is always masked to 32 bits on *all* platforms, so behavior
**   is consistent across LLP64/LP64.
**
**   A per-instance hash_seed is available via wc_limits to perturb the
**   basis (simple randomization). This is not cryptographic.
**
** MEMORY CONFIGURATION
**
**   Define WC_MALLOC and WC_FREE before including this header to
**   redirect memory allocation (e.g., to a custom allocator).
**
**   wc_open_ex() accepts a wc_limits struct that can:
**     - bound total internal allocations for a wc instance (max_bytes)
**     - tune initial hash table capacity and arena block size
**     - optionally use a caller-provided static buffer for all internal
**       allocations (static-buffer mode)
**
** MEMORY ACCOUNTING NOTES
**
**   max_bytes applies to INTERNAL allocations only:
**     - hash table (and growth)
**     - arena blocks
**     - optional heap/static scan buffer when WC_STACK_BUFFER == 0
**
**   Not counted:
**     - the wc handle itself (struct wc)
**     - arrays returned by wc_results()
**
**   In static-buffer mode, max_bytes (if non-zero) is enforced against
**   bytes consumed from the static buffer INCLUDING internal alignment
**   padding. (This makes max_bytes a strict cap on static-buffer usage.)
**
** STATIC BUFFER MODE GUARANTEE
**
**   In static-buffer mode, wc_open_ex() will fail (return NULL) if the
**   effective budget cannot support the *minimal* internal structures,
**   including the ability to store at least one word of length max_word
**   plus a terminating NUL.
**
** BUILD CONFIGURATION
**
**   WC_OMIT_ASSERT  - Disable internal assertions (smaller code).
**   WC_STACK_BUFFER - 0 to heap/static-allocate scan buffers (default 1).
**   WC_MAX_WORD     - Compile-time upper bound for max_word (default 1024).
**   WC_MIN_INIT_CAP - Compile-time lower bound for initial hash slots.
**   WC_MIN_BLOCK_SZ - Compile-time lower bound for arena first block.
**
** PORTABILITY
**
**   Requires C99. Public API avoids exact-width integer types. Core
**   assumes:
**     - CHAR_BIT == 8
**     - ASCII-compatible execution character set
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

/* --- Restrict qualifier abstraction ---------------------------------- */

#ifndef WC_RESTRICT
#if defined(__cplusplus)
#if defined(_MSC_VER)
#define WC_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define WC_RESTRICT __restrict__
#else
#define WC_RESTRICT
#endif
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define WC_RESTRICT restrict
#else
#define WC_RESTRICT
#endif
#endif

/* --- Versioning ------------------------------------------------------- */

/* 4.2.1: fixes collision-length OOB risk; tightens hashing consistency */
#define WC_VERSION "4.2.1"
#define WC_VERSION_NUMBER 4002001UL

/* --- Result codes ----------------------------------------------------- */

#define WC_OK 0
#define WC_ERROR 1
#define WC_NOMEM 2

/* --- Allocator configuration ----------------------------------------- */

#ifndef WC_MALLOC
#define WC_MALLOC(n) malloc(n)
#endif
#ifndef WC_REALLOC
#define WC_REALLOC(p, n) realloc((p), (n))
#endif
#ifndef WC_FREE
#define WC_FREE(p) free(p)
#endif

/* --- Scan buffer configuration --------------------------------------- */

#ifndef WC_STACK_BUFFER
#define WC_STACK_BUFFER 1
#endif

/* --- Compile-time tuning --------------------------------------------- */

#ifndef WC_MAX_WORD
#define WC_MAX_WORD 1024u
#endif
#ifndef WC_MIN_INIT_CAP
#define WC_MIN_INIT_CAP 16u
#endif
#ifndef WC_MIN_BLOCK_SZ
#define WC_MIN_BLOCK_SZ 256u
#endif

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

/* --- Public types ----------------------------------------------------- */

typedef struct wc wc;

typedef struct wc_limits {
    size_t max_bytes;
    size_t init_cap;
    size_t block_size;
    void *static_buf;
    size_t static_size;

    /* 0 = deterministic (default). Non-zero = perturb hash basis.
       Not cryptographic; meant to raise the bar for trivial collision attacks.
     */
    unsigned long hash_seed;
} wc_limits;

typedef struct wc_word {
    const char *word; /* owned by wc instance; invalid after wc_close */
    size_t count;
} wc_word;

typedef struct wc_build_config {
    unsigned long version_number;
    size_t max_word;
    size_t min_init_cap;
    size_t min_block_sz;
    int stack_buffer; /* 1 = stack, 0 = heap/static */
} wc_build_config;

/* --- Lifecycle -------------------------------------------------------- */

wc *wc_open_ex(size_t max_word, const wc_limits *limits);
wc *wc_open(size_t max_word);
void wc_close(wc *w);

/* --- Word insertion and scanning ------------------------------------- */

int wc_add(wc *w, const char *WC_RESTRICT word);
int wc_scan(wc *w, const char *WC_RESTRICT text, size_t len);

/* --- Queries ---------------------------------------------------------- */

size_t wc_total(const wc *w);
size_t wc_unique(const wc *w);

/* --- Results ---------------------------------------------------------- */

int wc_results(const wc *w, wc_word **WC_RESTRICT out, size_t *WC_RESTRICT n);

void wc_results_free(wc_word *r);

/* --- Zero-allocation iterator API ------------------------------------ */

typedef struct wc_cursor {
    const wc *w;
    size_t index;
} wc_cursor;

void wc_cursor_init(wc_cursor *c, const wc *w);

int wc_cursor_next(wc_cursor *c,
                   const char **WC_RESTRICT word,
                   size_t *WC_RESTRICT count);

/* --- Utility ---------------------------------------------------------- */

const char *wc_errstr(int rc);
const char *wc_version(void);
const wc_build_config *wc_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* WORDCOUNT_H */
