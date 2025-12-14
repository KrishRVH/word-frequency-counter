/* wordcount.c - Implementation
**
** Public domain.
**
** Core implementation of the wordcount library. See wordcount.h.
**
** Notable robustness properties:
**   - Overflow-checked size arithmetic before allocations
**   - Defined behavior on allocation failure (WC_NOMEM)
**   - Consistent 32-bit FNV-1a hash across platforms (masked to 32 bits)
**   - Collision-safe comparisons (stores key length per slot)
**
** Platform assumptions (enforced at compile time):
**   - CHAR_BIT == 8
**   - ASCII-compatible execution character set
**   - unsigned long is at least 32 bits (for 32-bit hash storage)
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

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define WC_STATIC_ASSERT(cond, msg) _Static_assert(cond, #msg)
#else
#define WC_STATIC_ASSERT(cond, msg) \
    typedef char wc_static_assert_##msg[(cond) ? 1 : -1]
#endif

WC_STATIC_ASSERT('A' == 65 && 'Z' == 90 && 'a' == 97 && 'z' == 122 &&
                         ('a' ^ 'A') == 32,
                 ascii_charset_required);

WC_STATIC_ASSERT(CHAR_BIT == 8, char_bit_must_be_8);

/* Require unsigned long to be at least 32 bits for the internal hash. */
WC_STATIC_ASSERT(ULONG_MAX >= 0xffffffffUL,
                 unsigned_long_must_be_at_least_32_bits);

WC_STATIC_ASSERT(WC_MAX_WORD >= 4u, wc_max_word_must_be_at_least_4);
WC_STATIC_ASSERT(WC_MIN_INIT_CAP >= 1u, wc_min_init_cap_must_be_positive);
WC_STATIC_ASSERT(WC_MIN_BLOCK_SZ >= 1u, wc_min_block_sz_must_be_positive);
WC_STATIC_ASSERT(WC_DEFAULT_INIT_CAP >= WC_MIN_INIT_CAP,
                 wc_default_init_cap_too_small);
WC_STATIC_ASSERT(WC_DEFAULT_BLOCK_SZ >= WC_MIN_BLOCK_SZ,
                 wc_default_block_sz_too_small);

/* --- Internal alignment type ------------------------------------------ */

typedef union {
    void *p;
    size_t sz;
    unsigned long ul;
} wc_internal_align;

#define WC_ALIGN sizeof(wc_internal_align)

static void wc_internal_align_sanity(void)
{
    (void)sizeof(((wc_internal_align *)0)->p);
    (void)sizeof(((wc_internal_align *)0)->sz);
    (void)sizeof(((wc_internal_align *)0)->ul);
}

/* --- Implementation-local defaults ------------------------------------ */

#define MIN_WORD 4u
#define DEF_WORD 64u

/* --- 32-bit FNV-1a hash in an unsigned long --------------------------- */

typedef unsigned long wc_hash_t;

#define WC_HASH_MASK 0xffffffffUL
#define FNV_OFF_32 2166136261UL
#define FNV_MUL_32 16777619UL

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

/* Store key length to make comparisons collision-safe and OOB-proof. */
typedef struct {
    char *word;
    size_t n; /* stored key length (excluding NUL) */
    size_t cnt;
    wc_hash_t hash; /* 32-bit value masked in wc_hash_t */
} Slot;

/* --- Internal allocation state ---------------------------------------- */

typedef struct {
    /* bytes_used/bytes_limit represent bytes consumed from the internal
       allocator (requested bytes). In static-buffer mode, this includes
       alignment padding. */
    size_t bytes_used;
    size_t bytes_limit; /* 0 = unlimited */
    int static_mode;    /* 0 = dynamic, 1 = static-buffer */

    unsigned char *sbuf;
    size_t sbuf_size;
    size_t sbuf_used;
} wc_alloc_state;

/* --- wc object --------------------------------------------------------- */

struct wc {
    Slot *tab;
    size_t cap;  /* power-of-two capacity */
    size_t len;  /* unique words */
    size_t tot;  /* total words */
    size_t maxw; /* maximum stored word length */

    Arena arena;
    wc_alloc_state alloc;
    wc_hash_t seed; /* masked to 32-bit */

#if !WC_STACK_BUFFER
    char *scanbuf; /* per-instance scan buffer (size maxw) */
#endif
};

/* --- Forward declarations --------------------------------------------- */

static void *wc_xmalloc_state(wc_alloc_state *st, size_t n);
static void *wc_xmalloc(wc *w, size_t n);
static void wc_xfree(wc *w, void *p, size_t n);

static Block *block_new(wc *w, size_t cap);
static int arena_init(wc *w, Arena *a, size_t block_sz);
static void arena_free(wc *w);
static void *arena_alloc(wc *w, size_t sz);

static wc_hash_t fnv32(const char *s, size_t n, wc_hash_t seed_basis);

static int tab_grow(wc *w);
static Slot *tab_find(wc *w, const char *word, size_t n, wc_hash_t h);
static int tab_insert(wc *w, const char *word, size_t n, wc_hash_t h);

static void
tune_params(const wc_limits *lim, size_t *init_cap, size_t *block_sz);

/* --- Allocation helpers ------------------------------------------------ */

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

    /* Static-buffer mode: bump allocator with WC_ALIGN alignment.
       bytes_used accounts for alignment padding (strict budget). */
    {
        const size_t align = WC_ALIGN;
        const size_t off = st->sbuf_used;
        const size_t pad = (align - (off % align)) % align;
        size_t real;
        size_t new_used;

        if (add_overflows(pad, n))
            return NULL;
        real = pad + n;

        if (add_overflows(st->sbuf_used, real))
            return NULL;
        if (st->sbuf_used + real > st->sbuf_size)
            return NULL;

        if (add_overflows(st->bytes_used, real))
            return NULL;
        new_used = st->bytes_used + real;
        if (st->bytes_limit && new_used > st->bytes_limit)
            return NULL;

        p = (void *)(st->sbuf + st->sbuf_used + pad);
        st->sbuf_used += real;
        st->bytes_used = new_used;
        return memset(p, 0, n);
    }
}

static void *wc_xmalloc(wc *w, size_t n)
{
    return w ? wc_xmalloc_state(&w->alloc, n) : NULL;
}

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
        (void)n;
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

static void *arena_alloc(wc *w, size_t sz)
{
    const size_t align = WC_ALIGN;
    size_t offset;
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

    offset = (size_t)(a->tail->cur - a->tail->buf);
    pad = (align - (offset % align)) % align;

    avail = (size_t)(a->tail->end - a->tail->cur);
    if (avail >= pad && avail - pad >= sz) {
        p = a->tail->cur + pad;
        a->tail->cur = p + sz;
        WC_ASSERT(a->tail->cur <= a->tail->end);
        return memset(p, 0, sz);
    }

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

    /* Fresh block starts aligned, but keep the generic math. */
    offset = 0;
    pad = (align - (offset % align)) % align;

    p = b->cur + pad;
    b->cur = p + sz;
    WC_ASSERT(b->cur <= b->end);

    return memset(p, 0, sz);
}

/* --- Hash table -------------------------------------------------------- */

static wc_hash_t fnv32(const char *s, size_t n, wc_hash_t seed_basis)
{
    wc_hash_t h = seed_basis & WC_HASH_MASK;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= FNV_MUL_32;
        h &= WC_HASH_MASK;
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

        idx = (size_t)((s->hash & WC_HASH_MASK) & (wc_hash_t)(nc - 1));
        while (ns[idx].word)
            idx = (idx + 1) & (nc - 1);

        ns[idx] = *s;
    }

    old_tab = w->tab;
    old_alloc = w->cap * sizeof(Slot);

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

    idx = (size_t)((h & WC_HASH_MASK) & (wc_hash_t)(w->cap - 1));
    start = idx;

    do {
        Slot *s = &w->tab[idx];

        if (!s->word)
            return s;

        /* Collision-safe and OOB-proof: compare lengths first. */
        if (s->hash == (h & WC_HASH_MASK) && s->n == n &&
            memcmp(s->word, word, n) == 0) {
            return s;
        }

        idx = (idx + 1) & (w->cap - 1);
    } while (idx != start);

    return NULL; /* Full table (corruption or pathological static config). */
}

static int tab_insert(wc *w, const char *word, size_t n, wc_hash_t h)
{
    Slot *s;
    char *copy;
    size_t alloc;

    WC_ASSERT(w != NULL);
    WC_ASSERT(word != NULL);
    WC_ASSERT(n > 0);

    /* Grow at ~0.7 load factor unless static-buffer mode. */
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
    /* copy[n] is already NUL due to zero-init. */

    s->word = copy;
    s->n = n;
    s->hash = (h & WC_HASH_MASK);
    s->cnt = 1;

    w->len++;
    w->tot++;

    return 0;
}

/* --- Parameter tuning -------------------------------------------------- */

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

        if (lim->max_bytes)
            budget = lim->max_bytes;
        if (lim->static_buf && lim->static_size) {
            if (budget == 0 || lim->static_size < budget)
                budget = lim->static_size;
        }

        if (budget) {
            const size_t b = budget;
            const size_t table_budget = b / 2;

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

            /* Cap initial block size heuristically. */
            {
                const size_t arena_budget = b - table_budget;
                const size_t max_blk = arena_budget / 4;
                if (max_blk > 0 && blk > max_blk)
                    blk = max_blk;
            }
        }
    }

    if (cap < WC_MIN_INIT_CAP)
        cap = WC_MIN_INIT_CAP;

    /* Round up to power of two. */
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

    /* Allocator state defaults */
    w->alloc.bytes_used = 0;
    w->alloc.bytes_limit = 0;
    w->alloc.static_mode = 0;
    w->alloc.sbuf = NULL;
    w->alloc.sbuf_size = 0;
    w->alloc.sbuf_used = 0;

    /* Configure static-buffer mode */
    if (limits && limits->static_buf && limits->static_size > 0) {
        w->alloc.static_mode = 1;
        w->alloc.sbuf = (unsigned char *)limits->static_buf;
        w->alloc.sbuf_size = limits->static_size;
        w->alloc.sbuf_used = 0;

#if defined(UINTPTR_MAX)
        {
            uintptr_t p = (uintptr_t)limits->static_buf;
            if (p % WC_ALIGN != 0u) {
                WC_FREE(w);
                return NULL;
            }
        }
#else
        if (((size_t)limits->static_buf % WC_ALIGN) != 0u) {
            WC_FREE(w);
            return NULL;
        }
#endif
    }

    /* Set max_bytes limit (0 = unlimited). In static mode clamp to static_size.
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

    /* Ensure first arena block can store at least one max_word word (+NUL). */
    {
        size_t need;
        if (add_overflows(w->maxw, 1))
            goto fail;
        need = w->maxw + 1;
        if (block_sz < need)
            block_sz = need;
        if (block_sz < WC_MIN_BLOCK_SZ)
            block_sz = WC_MIN_BLOCK_SZ;
    }

    /* Seed: masked 32-bit basis with optional fold-down of hash_seed. */
    {
        wc_hash_t basis = (wc_hash_t)(FNV_OFF_32 & WC_HASH_MASK);
        if (limits && limits->hash_seed) {
            unsigned long hs = limits->hash_seed;
#if ULONG_MAX > 0xffffffffUL
            hs ^= (hs >> 32);
#endif
            basis ^= ((wc_hash_t)hs & WC_HASH_MASK);
        }
        w->seed = (basis & WC_HASH_MASK);
    }

    /* In static mode, preflight minimal allocations deterministically. */
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

    /* Initialize arena. */
    if (arena_init(w, &w->arena, block_sz) < 0) {
        wc_xfree(w, w->tab, table_bytes);
        w->tab = NULL;
        w->cap = 0;
        goto fail;
    }

#if !WC_STACK_BUFFER
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

    h = fnv32(word, n, w->seed);
    return tab_insert(w, word, n, h) < 0 ? WC_NOMEM : WC_OK;
}

static int isalpha_(int c)
{
    return ((unsigned)c | 32u) - 'a' < 26u;
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
        size_t n = 0;
        wc_hash_t h = w->seed;

        while (p < end && !isalpha_(*p))
            p++;
        if (p >= end)
            break;

        while (p < end && isalpha_(*p)) {
            unsigned c = (unsigned)(*p++) | 32u;
            if (n < w->maxw) {
                buf[n++] = (char)c;
                h ^= (wc_hash_t)c;
                h *= FNV_MUL_32;
                h &= WC_HASH_MASK;
            }
        }

        WC_ASSERT(n > 0);
        WC_ASSERT(n <= w->maxw);

        if (tab_insert(w, buf, n, h) < 0)
            return WC_NOMEM;
    }

    return WC_OK;
}

/* --- Queries ---------------------------------------------------------- */

size_t wc_total(const wc *w)
{
    return w ? w->tot : 0;
}

size_t wc_unique(const wc *w)
{
    return w ? w->len : 0;
}

/* --- Results enumeration ---------------------------------------------- */

static int cmp_words(const void *a, const void *b)
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

    if (!out || !n)
        return WC_ERROR;

    *out = NULL;
    *n = 0;

    if (!w)
        return WC_ERROR;

    if (w->len == 0)
        return WC_OK;

    if (mul_overflows(w->len, sizeof *arr))
        return WC_NOMEM;
    alloc = w->len * sizeof *arr;

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

    qsort(arr, w->len, sizeof *arr, cmp_words);
    *out = arr;
    *n = w->len;

    return WC_OK;
}

void wc_results_free(wc_word *r)
{
    WC_FREE(r);
}

/* --- Cursor API -------------------------------------------------------- */

void wc_cursor_init(wc_cursor *c, const wc *w)
{
    if (c) {
        c->w = w;
        c->index = 0;
    }
}

int wc_cursor_next(wc_cursor *c,
                   const char **WC_RESTRICT word,
                   size_t *WC_RESTRICT count)
{
    if (!c || !c->w)
        return 0;

    while (c->index < c->w->cap) {
        const Slot *s = &c->w->tab[c->index++];
        if (s->word) {
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
            return "memory allocation failed or memory limit reached";
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
    wc_internal_align_sanity();
    return &wc_build_cfg;
}
