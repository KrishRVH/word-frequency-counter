/* wordcount.c - Implementation
**
** Public domain.
**
** Core implementation of the wordcount library. See wordcount.h
** for API, configuration macros, and detailed documentation.
**
** NOTES
**
**   - This file implements only the core library (wc_open, wc_scan,
**     wc_add, wc_results, etc.). The CLI (wc_main.c) is responsible
**     for streaming stdin and mmap-based file I/O.
**
**   - All internal allocations are routed through wc_alloc_state so
**     memory limits and static-buffer mode are enforced consistently.
**     The only exception is the caller-owned array returned by
**     wc_results(), which is allocated via WC_MALLOC and explicitly
**     documented as *not* counted against internal limits.
**
**   - The implementation assumes a hosted C99 environment with:
**       * CHAR_BIT == 8
**       * ASCII-compatible execution character set
**         ('A'..'Z' == 65..90, 'a'..'z' == 97..122)
**     These are enforced via compile-time assertions.
**
**   - Exact-width integer types are not required by the public API.
**     <stdint.h> is used here only for SIZE_MAX / UINTPTR_MAX checks.
*/

#include "wordcount.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* --- Internal assertion configuration ---------------------------------- */

#ifndef WC_OMIT_ASSERT
#define WC_ASSERT(x) assert(x)
#else
#define WC_ASSERT(x) ((void)0)
#endif

/* --- Compile-time verification of platform requirements ---------------- */

/*
** Prefer C11 _Static_assert when available; otherwise fall back to a
** negative-array-size trick that is widely supported on C99
** toolchains. This is the same style used in many production C
** libraries (e.g., SQLite).
*/
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define WC_STATIC_ASSERT(cond, msg) _Static_assert(cond, #msg)
#else
#define WC_STATIC_ASSERT(cond, msg) \
    typedef char wc_static_assert_##msg[(cond) ? 1 : -1]
#endif

/*
** Require an ASCII-compatible execution character set and 8-bit char.
** On non-ASCII systems (e.g., EBCDIC) this will fail at compile time.
*/
WC_STATIC_ASSERT('A' == 65 && 'Z' == 90 && 'a' == 97 && 'z' == 122 &&
                         ('a' ^ 'A') == 32,
                 ascii_charset_required);

WC_STATIC_ASSERT(CHAR_BIT == 8, char_bit_must_be_8);

/* Configuration sanity checks (see wordcount.h for defaults). */

WC_STATIC_ASSERT(WC_MAX_WORD >= 4u,
                 wc_max_word_must_be_at_least_4);

WC_STATIC_ASSERT(WC_MIN_INIT_CAP >= 1u,
                 wc_min_init_cap_must_be_positive);

WC_STATIC_ASSERT(WC_MIN_BLOCK_SZ >= 1u,
                 wc_min_block_sz_must_be_positive);

WC_STATIC_ASSERT(WC_DEFAULT_INIT_CAP >= WC_MIN_INIT_CAP,
                 wc_default_init_cap_too_small);

WC_STATIC_ASSERT(WC_DEFAULT_BLOCK_SZ >= WC_MIN_BLOCK_SZ,
                 wc_default_block_sz_too_small);

/*
** Internal alignment type.
**
** Any object allocated from the internal bump allocator will be
** aligned to WC_ALIGN, which is based on this union. Including all
** types we allocate (void*, size_t, unsigned long) ensures correct
** alignment on conforming ABIs.
*/
typedef union {
    void *p;
    size_t sz;
    unsigned long ul;
} wc_internal_align;

#define WC_ALIGN sizeof(wc_internal_align)

/*
** Dummy use of wc_internal_align members to satisfy static analyzers
** that warn about unused union members. This generates no code.
*/
static void wc_internal_align_sanity(void)
{
    (void)sizeof(((wc_internal_align *)0)->p);
    (void)sizeof(((wc_internal_align *)0)->sz);
    (void)sizeof(((wc_internal_align *)0)->ul);
}

/* --- Configuration defaults (implementation-local) --------------------- */

/*
** Runtime max_word is clamped into [MIN_WORD, WC_MAX_WORD].
** WC_MAX_WORD and the *_INIT_* / *_BLOCK_* macros come from the header.
*/
#define MIN_WORD 4u
#define DEF_WORD 64u

/*
** Hash type and FNV-1a constants (32-bit).
**
** wc_hash_t is unsigned long to avoid optional exact-width integer
** types in the public API; the constants are 32-bit and the hash is
** effectively a widened 32-bit FNV-1a.
*/
typedef unsigned long wc_hash_t;
#define FNV_OFF 2166136261u
#define FNV_MUL 16777619u

/* --- Overflow-safe arithmetic helpers --------------------------------- */

static int add_overflows(size_t a, size_t b)
{
    return a > SIZE_MAX - b;
}

static int mul_overflows(size_t a, size_t b)
{
    return b != 0 && a > SIZE_MAX / b;
}

/* --- Arena allocator --------------------------------------------------- */

typedef struct Block Block;
struct Block {
    Block *next;
    char *cur;
    char *end;
    char buf[];
};

typedef struct {
    Block *head;
    Block *tail;
    size_t block_sz;
} Arena;

/* --- Hash table slot --------------------------------------------------- */

typedef struct {
    char *word;
    wc_hash_t hash;
    size_t cnt;
} Slot;

/* --- Internal allocation state ---------------------------------------- */

/*
** All internal allocations (hash table, arena blocks, optional scan
** buffer when WC_STACK_BUFFER == 0) are tracked through this state.
**
** Dynamic mode:
**   - static_mode == 0
**   - allocations use WC_MALLOC / WC_FREE
**   - bytes_used is kept in sync and enforced against bytes_limit
**
** Static-buffer mode:
**   - static_mode == 1
**   - allocations are carved from [sbuf, sbuf + sbuf_size) via
**     a bump allocator with WC_ALIGN alignment
**   - allocations are zero-initialized
**   - bytes_used and sbuf_used grow monotonically; there is no
**     reuse or free inside the static buffer
*/
typedef struct {
    /* Common to dynamic and static modes. */
    size_t bytes_used;   /* sum of payload bytes allocated internally */
    size_t bytes_limit;  /* upper bound on bytes_used when non-zero  */
    int static_mode;     /* 0 = dynamic, 1 = static-buffer mode      */

    /* Static-buffer mode only. */
    unsigned char *sbuf;
    size_t sbuf_size;
    size_t sbuf_used;
} wc_alloc_state;

/* --- wc object --------------------------------------------------------- */

struct wc {
    Slot *tab;
    size_t cap;   /* hash table capacity (power of two) */
    size_t len;   /* number of unique words             */
    size_t tot;   /* total words (including duplicates) */
    size_t maxw;  /* maximum stored word length         */

    Arena arena;
    wc_alloc_state alloc;
    wc_hash_t seed; /* Seed for HashDoS protection */

#if !WC_STACK_BUFFER
    char *scanbuf; /* per-instance scan buffer (size maxw) */
#endif
};

/* --- Internal helpers (forward declarations) --------------------------- */

static void *wc_xmalloc_state(wc_alloc_state *st, size_t n);
static void *wc_xmalloc(wc *w, size_t n);
static void wc_xfree(wc *w, void *p, size_t n);

static Block *block_new(wc *w, size_t cap);
static int arena_init(wc *w, Arena *a, size_t block_sz);
static void arena_free(wc *w);
static void *arena_alloc(wc *w, size_t sz);

/* UPDATED FORWARD DECLARATION */
static wc_hash_t fnv(const char *s, size_t n, wc_hash_t seed_basis);

static int tab_grow(wc *w);
static Slot *tab_find(wc *w, const char *word, size_t n, wc_hash_t h);
static int tab_insert(wc *w, const char *word, size_t n, wc_hash_t h);

static void
tune_params(const wc_limits *lim, size_t *init_cap, size_t *block_sz);

/* --- Allocation helpers ------------------------------------------------ */

/*
** Central allocation helper on a raw state object.
**
** In dynamic mode this wraps WC_MALLOC and enforces bytes_limit.
** In static-buffer mode it carves from sbuf via a bump allocator with
** WC_ALIGN alignment and enforces both sbuf_size and bytes_limit.
**
** On any arithmetic overflow or limit violation, this function fails
** cleanly (returns NULL) rather than risking UB.
*/
static void *wc_xmalloc_state(wc_alloc_state *st, size_t n)
{
    void *p;

    if (!st || n == 0)
        return NULL;

    if (!st->static_mode) {
        size_t new_used;

        if (add_overflows(st->bytes_used, n))
            return NULL;

        new_used = st->bytes_used + n;
        if (st->bytes_limit && new_used > st->bytes_limit)
            return NULL;

        p = WC_MALLOC(n);
        if (!p)
            return NULL;

        st->bytes_used = new_used;
        return p;
    }

    /* Static-buffer mode: bump allocator within [sbuf, sbuf+sbuf_size). */
    {
        size_t align = WC_ALIGN;
        size_t off = st->sbuf_used;
        size_t pad = (align - (off % align)) % align;
        size_t new_used;

        if (add_overflows(pad, n))
            return NULL;
        if (add_overflows(st->sbuf_used, pad + n))
            return NULL;

        if (st->sbuf_used + pad + n > st->sbuf_size)
            return NULL;

        if (add_overflows(st->bytes_used, n))
            return NULL;
        new_used = st->bytes_used + n;
        if (st->bytes_limit && new_used > st->bytes_limit)
            return NULL;

        p = (void *)(st->sbuf + st->sbuf_used + pad);
        st->sbuf_used += pad + n;
        st->bytes_used = new_used;
        return memset(p, 0, n);
    }
}

/*
** Convenience wrapper for the wc object.
*/
static void *wc_xmalloc(wc *w, size_t n)
{
    return w ? wc_xmalloc_state(&w->alloc, n) : NULL;
}

/*
** Internal free helper.
**
** In dynamic mode it calls WC_FREE and decrements bytes_used.
** In static-buffer mode it is a no-op; memory is never recycled
** inside the static buffer.
*/
static void wc_xfree(wc *w, void *p, size_t n)
{
    if (!w || !p)
        return;

    if (!w->alloc.static_mode) {
        WC_FREE(p);

        if (w->alloc.bytes_used >= n)
            w->alloc.bytes_used -= n;
        else
            w->alloc.bytes_used = 0;
    } else {
        (void)n; /* static-buffer mode: nothing to do */
    }
}

/* --- Arena implementation ---------------------------------------------- */

static Block *block_new(wc *w, size_t cap)
{
    Block *b;
    size_t total;

    if (add_overflows(sizeof(Block), cap))
        return NULL;
    total = sizeof(Block) + cap;

    b = (Block *)wc_xmalloc(w, total);
    if (!b)
        return NULL;

    b->next = NULL;
    b->cur = b->buf;
    b->end = b->buf + cap;

    return b;
}

static int arena_init(wc *w, Arena *a, size_t block_sz)
{
    Block *b;

    WC_ASSERT(w != NULL);
    WC_ASSERT(a != NULL);

    a->head = a->tail = NULL;
    a->block_sz = block_sz;

    b = block_new(w, block_sz);
    if (!b)
        return -1;

    a->head = a->tail = b;
    return 0;
}

static void arena_free(wc *w)
{
    Block *b;

    if (!w)
        return;

    b = w->arena.head;
    while (b) {
        Block *n = b->next;
        size_t cap = (size_t)(b->end - b->buf);
        size_t total = sizeof(Block) + cap;
        wc_xfree(w, b, total);
        b = n;
    }

    w->arena.head = w->arena.tail = NULL;
}

/*
** Arena allocation with WC_ALIGN alignment.
**
** In static-buffer mode, the arena never extends beyond the initial
** block; further requests fail with NULL and are mapped to WC_NOMEM
** by callers.
*/
static void *arena_alloc(wc *w, size_t sz)
{
    size_t offset;
    size_t align = WC_ALIGN;
    size_t pad;
    size_t avail;
    size_t cap;
    size_t need;
    char *p;
    Block *b;
    Arena *a;

    WC_ASSERT(w != NULL);
    a = &w->arena;
    WC_ASSERT(a->tail != NULL);
    WC_ASSERT(a->tail->cur >= a->tail->buf);
    WC_ASSERT(a->tail->cur <= a->tail->end);

    offset = (size_t)(a->tail->cur - a->tail->buf);
    pad = (align - (offset % align)) % align;

    avail = (size_t)(a->tail->end - a->tail->cur);
    if (avail >= pad && avail - pad >= sz) {
        p = a->tail->cur + pad;
        a->tail->cur = p + sz;
        WC_ASSERT(a->tail->cur <= a->tail->end);
        return memset(p, 0, sz);
    }

    /* Static-buffer mode: arena is fixed to the first block. */
    if (w->alloc.static_mode)
        return NULL;

    if (add_overflows(sz, align))
        return NULL;
    need = sz + align;
    cap = need > a->block_sz ? need : a->block_sz;

    b = block_new(w, cap);
    if (!b)
        return NULL;

    a->tail->next = b;
    a->tail = b;

    offset = 0;
    pad = (align - (offset % align)) % align;
    p = b->cur + pad;
    b->cur = p + sz;
    WC_ASSERT(b->cur <= b->end);

    return memset(p, 0, sz);
}

/* --- Hash table implementation ---------------------------------------- */

static wc_hash_t fnv(const char *s, size_t n, wc_hash_t seed_basis)
{
    wc_hash_t h = seed_basis;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= FNV_MUL;
    }

    return h;
}

static int tab_grow(wc *w)
{
    size_t nc;
    size_t i;
    size_t idx;
    size_t alloc;
    Slot *ns;
    size_t old_cap;
    size_t old_alloc;
    Slot *old_tab;

    WC_ASSERT(w != NULL);
    WC_ASSERT(w->tab != NULL);
    WC_ASSERT(w->cap > 0);

    if (mul_overflows(w->cap, 2))
        return -1;
    nc = w->cap * 2;

    if (mul_overflows(nc, sizeof(Slot)))
        return -1;
    alloc = nc * sizeof(Slot);

    ns = (Slot *)wc_xmalloc(w, alloc);
    if (!ns)
        return -1;
    memset(ns, 0, alloc);

    for (i = 0; i < w->cap; i++) {
        const Slot *s = &w->tab[i];
        if (!s->word)
            continue;

        idx = (size_t)(s->hash & (nc - 1));
        while (ns[idx].word)
            idx = (idx + 1) & (nc - 1);

        ns[idx] = *s;
    }

    old_tab = w->tab;
    old_cap = w->cap;
    old_alloc = old_cap * sizeof(Slot);

    w->tab = ns;
    w->cap = nc;

    wc_xfree(w, old_tab, old_alloc);
    return 0;
}

static Slot *tab_find(wc *w, const char *word, size_t n, wc_hash_t h)
{
    size_t idx;
    size_t start;

    WC_ASSERT(w != NULL);
    WC_ASSERT(w->tab != NULL);
    WC_ASSERT(w->cap > 0);
    WC_ASSERT(word != NULL || n == 0);

    idx = (size_t)(h & (w->cap - 1));
    start = idx;

    do {
        Slot *s = &w->tab[idx];

        if (!s->word)
            return s;

        if (s->hash == h && memcmp(s->word, word, n) == 0 &&
            s->word[n] == '\0') {
            return s;
        }

        idx = (idx + 1) & (w->cap - 1);
    } while (idx != start);

    /* Full table (should only happen in static mode at very high load). */
    return NULL;
}

static int tab_insert(wc *w, const char *word, size_t n, wc_hash_t h)
{
    Slot *s;
    char *copy;
    size_t alloc;

    WC_ASSERT(w != NULL);
    WC_ASSERT(word != NULL);
    WC_ASSERT(n > 0);

    /*
    ** Grow when load factor exceeds ~0.7. In static-buffer mode,
    ** table growth is disabled; hitting this threshold is treated
    ** as out-of-memory and the caller observes WC_NOMEM.
    */
    if (w->len * 10 >= w->cap * 7) {
        if (w->alloc.static_mode)
            return -1;
        if (tab_grow(w) < 0)
            return -1;
    }

    s = tab_find(w, word, n, h);
    if (!s)
        return -1;

    if (s->word) {
        s->cnt++;
        w->tot++;
        return 0;
    }

    if (add_overflows(n, 1))
        return -1;
    alloc = n + 1;

    copy = (char *)arena_alloc(w, alloc);
    if (!copy)
        return -1;

    memcpy(copy, word, n);

    s->word = copy;
    s->hash = h;
    s->cnt = 1;

    w->len++;
    w->tot++;

    return 0;
}

/* --- Parameter tuning based on limits --------------------------------- */

/*
** Derive initial hash table capacity and arena block size from
** wc_limits (if provided) and the global defaults.
**
** Heuristic:
**   - Start from WC_DEFAULT_INIT_CAP / WC_DEFAULT_BLOCK_SZ.
**   - If a budget can be inferred from max_bytes and/or static_size,
**     trim the initial table size so that its byte cost is not more
**     than half the budget, and limit the first arena block to at
**     most a quarter of the remaining half.
**   - Apply floors WC_MIN_INIT_CAP and WC_MIN_BLOCK_SZ.
**   - Round init_cap up to a power of two.
*/
static void
tune_params(const wc_limits *lim, size_t *init_cap, size_t *block_sz)
{
    size_t cap = WC_DEFAULT_INIT_CAP;
    size_t blk = WC_DEFAULT_BLOCK_SZ;

    if (lim) {
        size_t budget = 0;

        if (lim->init_cap)
            cap = lim->init_cap;
        if (lim->block_size)
            blk = lim->block_size;

        /*
        ** Derive an overall memory budget if one is available.
        ** Prefer the smaller of max_bytes and static_size when both
        ** are provided, since both constrain internal heap usage.
        */
        if (lim->max_bytes)
            budget = lim->max_bytes;
        if (lim->static_buf && lim->static_size) {
            if (budget == 0 || lim->static_size < budget)
                budget = lim->static_size;
        }

        if (budget) {
            size_t b = budget;
            size_t table_budget = b / 2;

            if (!mul_overflows(cap, sizeof(Slot)) &&
                cap * sizeof(Slot) > table_budget && table_budget > 0) {
                size_t max_cap = table_budget / sizeof(Slot);

                if (max_cap < WC_MIN_INIT_CAP)
                    max_cap = WC_MIN_INIT_CAP;

                /* Round down to power of two. */
                {
                    size_t p = 1;
                    while ((p << 1) <= max_cap)
                        p <<= 1;
                    cap = p;
                }
            }

            /*
            ** Use up to one quarter of the arena budget for the
            ** first block. For very small budgets this will pull
            ** blk down; the final floor is applied below via
            ** WC_MIN_BLOCK_SZ.
            */
            {
                size_t arena_budget = b - table_budget;
                size_t max_blk = arena_budget / 4;

                if (max_blk > 0 && blk > max_blk)
                    blk = max_blk;
            }
        }
    }

    if (cap < WC_MIN_INIT_CAP)
        cap = WC_MIN_INIT_CAP;

    /* Ensure power of two for hash table capacity. */
    {
        size_t p = 1;
        while (p < cap && p <= (SIZE_MAX / 2))
            p <<= 1;
        cap = p;
    }

    if (blk < WC_MIN_BLOCK_SZ)
        blk = WC_MIN_BLOCK_SZ;

    *init_cap = cap;
    *block_sz = blk;
}

/* --- Public API -------------------------------------------------------- */

wc *wc_open_ex(size_t max_word, const wc_limits *limits)
{
    wc *w;
    size_t init_cap;
    size_t block_sz;
    size_t table_bytes;

    tune_params(limits, &init_cap, &block_sz);

    w = (wc *)WC_MALLOC(sizeof *w);
    if (!w)
        return NULL;
    memset(w, 0, sizeof *w);

    /* Initialize allocator state. */
    w->alloc.bytes_used = 0;
    w->alloc.bytes_limit = 0;
    w->alloc.static_mode = 0;
    w->alloc.sbuf = NULL;
    w->alloc.sbuf_size = 0;
    w->alloc.sbuf_used = 0;

    /* Configure static-buffer mode if requested. */
    if (limits && limits->static_buf && limits->static_size > 0) {
        w->alloc.static_mode = 1;
        w->alloc.sbuf = (unsigned char *)limits->static_buf;
        w->alloc.sbuf_size = limits->static_size;
        w->alloc.sbuf_used = 0;

#if defined(UINTPTR_MAX)
        /*
        ** If uintptr_t is available, enforce that static_buf is
        ** suitably aligned for our internal alignment requirement.
        ** Misaligned buffers are rejected deterministically.
        */
        {
            uintptr_t p = (uintptr_t)limits->static_buf;
            if (p % WC_ALIGN != 0u) {
                WC_FREE(w);
                return NULL;
            }
        }
#else
        /* Fallback for pre-standard/exotic envs lacking uintptr_t */
        if (((size_t)limits->static_buf % WC_ALIGN) != 0u) {
            WC_FREE(w);
            return NULL;
        }
#endif
    }

    /*
    ** Set overall memory budget (0 = unlimited). In static mode,
    ** max_bytes, if non-zero, is clamped to static_size and acts
    ** as an additional guard; otherwise the static buffer alone
    ** bounds internal allocations.
    */
    if (limits && limits->max_bytes) {
        size_t b = limits->max_bytes;

        if (w->alloc.static_mode && limits->static_size > 0 &&
            b > limits->static_size) {
            b = limits->static_size;
        }

        w->alloc.bytes_limit = b;
    }

    /* Clamp max_word into [MIN_WORD, WC_MAX_WORD]. */
    if (max_word == 0)
        max_word = DEF_WORD;
    if (max_word < MIN_WORD)
        max_word = MIN_WORD;
    if (max_word > WC_MAX_WORD)
        max_word = WC_MAX_WORD;
    w->maxw = max_word;

    /*
    ** In static-buffer mode, verify that the minimal internal
    ** structures (hash table, first arena block, optional heap
    ** scan buffer) can fit within the effective budget by simulating
    ** the same allocation sequence with a scratch allocator state.
    */
    if (w->alloc.static_mode) {
        wc_alloc_state scratch = w->alloc;
        size_t arena_bytes;

        if (mul_overflows(init_cap, sizeof(Slot)))
            goto fail;

        table_bytes = init_cap * sizeof(Slot);

        if (!wc_xmalloc_state(&scratch, table_bytes))
            goto fail;

        if (add_overflows(sizeof(Block), block_sz))
            goto fail;
        arena_bytes = sizeof(Block) + block_sz;

        if (!wc_xmalloc_state(&scratch, arena_bytes))
            goto fail;

#if !WC_STACK_BUFFER
        if (!wc_xmalloc_state(&scratch, w->maxw))
            goto fail;
#endif
    }

    /* Allocate initial hash table. */
    if (mul_overflows(init_cap, sizeof(Slot)))
        goto fail;
    table_bytes = init_cap * sizeof(Slot);

    w->tab = (Slot *)wc_xmalloc(w, table_bytes);
    if (!w->tab)
        goto fail;

    memset(w->tab, 0, table_bytes);
    w->cap = init_cap;

    /* Initialize seed with optional user entropy */
    {
        wc_hash_t basis = FNV_OFF;
        if (limits && limits->hash_seed)
            basis ^= (wc_hash_t)limits->hash_seed;
        w->seed = basis;
    }

    /* Initialize arena. */
    if (arena_init(w, &w->arena, block_sz) < 0) {
        wc_xfree(w, w->tab, table_bytes);
        w->tab = NULL;
        w->cap = 0;
        goto fail;
    }

#if !WC_STACK_BUFFER
    /* Optional heap/static-based scan buffer. */
    w->scanbuf = (char *)wc_xmalloc(w, w->maxw);
    if (!w->scanbuf) {
        arena_free(w);
        if (w->tab && w->cap) {
            wc_xfree(w, w->tab, table_bytes);
            w->tab = NULL;
            w->cap = 0;
        }
        goto fail;
    }
#endif

    return w;

fail:
    WC_FREE(w);
    return NULL;
}

wc *wc_open(size_t max_word)
{
    return wc_open_ex(max_word, NULL);
}

void wc_close(wc *w)
{
    size_t table_bytes;

    if (!w)
        return;

#if !WC_STACK_BUFFER
    if (w->scanbuf)
        wc_xfree(w, w->scanbuf, w->maxw);
#endif

    if (w->tab && w->cap) {
        table_bytes = w->cap * sizeof(Slot);
        wc_xfree(w, w->tab, table_bytes);
    }

    arena_free(w);
    WC_FREE(w);
}

/* --- Word insertion and scanning -------------------------------------- */

int wc_add(wc *w, const char *WC_RESTRICT word)
{
    size_t n;
    wc_hash_t h;

    if (!w || !word)
        return WC_ERROR;

    for (n = 0; n < w->maxw && word[n]; n++)
        ;
    if (n == 0)
        return WC_OK;

    h = fnv(word, n, w->seed);
    return tab_insert(w, word, n, h) < 0 ? WC_NOMEM : WC_OK;
}

/*
** ASCII-only letter check. Non-ASCII bytes (including UTF-8) are
** treated as word separators. The bit-twiddling works because in
** ASCII, lowercase letters differ from uppercase by exactly bit 5.
*/
static int isalpha_(int c)
{
    return ((unsigned)c | 32) - 'a' < 26;
}

int wc_scan(wc *w, const char *WC_RESTRICT text, size_t len)
{
    const unsigned char *p;
    const unsigned char *end;
#if WC_STACK_BUFFER
    char buf[WC_MAX_WORD];
#else
    char *buf;
#endif

    if (!w)
        return WC_ERROR;
    if (len == 0)
        return WC_OK;
    if (!text)
        return WC_ERROR;

#if !WC_STACK_BUFFER
    buf = w->scanbuf;
    WC_ASSERT(buf != NULL);
#endif

    p = (const unsigned char *)text;
    end = p + len;

    while (p < end) {
        size_t n;
        wc_hash_t h;

        while (p < end && !isalpha_(*p))
            p++;
        if (p >= end)
            break;

        h = w->seed;
        n = 0;

        while (p < end && isalpha_(*p)) {
            unsigned c = (unsigned)(*p++) | 32u;
            if (n < w->maxw) {
                buf[n++] = (char)c;
                h ^= c;
                h *= FNV_MUL;
            }
        }

        WC_ASSERT(n > 0);
        WC_ASSERT(n <= w->maxw);

        if (tab_insert(w, buf, n, h) < 0)
            return WC_NOMEM;
    }

    return WC_OK;
}

/* --- Query functions --------------------------------------------------- */

size_t wc_total(const wc *w)
{
    return w ? w->tot : 0;
}

size_t wc_unique(const wc *w)
{
    return w ? w->len : 0;
}

/* --- Results enumeration ---------------------------------------------- */

static int cmp(const void *a, const void *b)
{
    const wc_word *x = (const wc_word *)a;
    const wc_word *y = (const wc_word *)b;

    if (x->count != y->count)
        return x->count > y->count ? -1 : 1;

    return strcmp(x->word, y->word);
}

int wc_results(const wc *w, wc_word **WC_RESTRICT out, size_t *WC_RESTRICT n)
{
    wc_word *arr;
    size_t i;
    size_t j;
    size_t cnt;
    size_t alloc;

    if (!w || !out || !n)
        return WC_ERROR;

    if (w->len == 0) {
        *out = NULL;
        *n = 0;
        return WC_OK;
    }

    if (mul_overflows(w->len, sizeof *arr))
        return WC_NOMEM;
    alloc = w->len * sizeof *arr;

    /*
    ** Results buffer is allocated via WC_MALLOC without being
    ** accounted against bytes_limit or any static buffer, since its
    ** lifetime is under the caller's control.
    */
    arr = (wc_word *)WC_MALLOC(alloc);
    if (!arr)
        return WC_NOMEM;

    cnt = 0;
    for (i = 0; i < w->cap; i++) {
        if (w->tab[i].word)
            cnt++;
    }

    if (cnt != w->len) {
        WC_FREE(arr);
        return WC_ERROR;
    }

    for (i = 0, j = 0; i < w->cap; i++) {
        if (w->tab[i].word) {
            arr[j].word = w->tab[i].word;
            arr[j].count = w->tab[i].cnt;
            j++;
        }
    }

    WC_ASSERT(j == w->len);

    qsort(arr, w->len, sizeof *arr, cmp);
    *out = arr;
    *n = w->len;

    return WC_OK;
}

void wc_results_free(wc_word *r)
{
    WC_FREE(r);
}

void wc_cursor_init(wc_cursor *c, const wc *w)
{
    if (c) {
        c->w = w;
        c->index = 0;
    }
}

int wc_cursor_next(wc_cursor *c, const char **WC_RESTRICT word, size_t *WC_RESTRICT count)
{
    if (!c || !c->w)
        return 0;

    /* Linear scan of the open-addressed hash table */
    while (c->index < c->w->cap) {
        const Slot *s = &c->w->tab[c->index++];
        if (s->word) { /* Found a populated slot */
            if (word)
                *word = s->word;
            if (count)
                *count = s->cnt;
            return 1;
        }
    }
    return 0;
}

/* --- Utility functions ------------------------------------------------- */

const char *wc_errstr(int rc)
{
    switch (rc) {
        case WC_OK:
            return "success";
        case WC_ERROR:
            return "invalid argument or corrupted state";
        case WC_NOMEM:
            return "memory allocation failed";
        default:
            return "unknown error";
    }
}

const char *wc_version(void)
{
    return WC_VERSION;
}

/* --- Build configuration introspection -------------------------------- */

static const wc_build_config wc_build_cfg = { WC_VERSION_NUMBER,
                                              WC_MAX_WORD,
                                              WC_MIN_INIT_CAP,
                                              WC_MIN_BLOCK_SZ,
                                              WC_STACK_BUFFER ? 1 : 0 };

const wc_build_config *wc_build_info(void)
{
    /* Reference internal alignment helpers so analyzers
       see wc_internal_align as "used". */
    wc_internal_align_sanity();
    return &wc_build_cfg;
}
