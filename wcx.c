/*
 * wcx.c — fast word frequency counter (fun CLI)
 *
 * Goals (per request): speed, user experience, data presentation.
 * Not aiming for full generality/robustness.
 *
 * Build:
 *   gcc -O3 -march=native -pthread wcx.c -o wcx
 *
 * Notes:
 *  - Token rule: [A-Za-z]+ (ASCII letters), case-insensitive (stored lowercased)
 *  - Words longer than 63 chars are truncated (MAX_WORD-1)
 *  - Parallel mmap + per-thread open-addressing tables, then merge
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif
#ifdef __AVX512BW__
#include <immintrin.h>
#endif

/*===========================================================================
 * Config
 *===========================================================================*/

#define WCX_VERSION   "0.2"

#define MAX_THREADS   64
#define INITIAL_CAP   (1u << 14)
#define POOL_SIZE     (32u << 20) /* per-thread string arena */
#define MAX_WORD      64
#define DEFAULT_TOPN  25
#define CACHELINE     64

/*===========================================================================
 * Types
 *===========================================================================*/

typedef struct {
    char    *word;   /* lowercased, NUL-terminated */
    uint32_t count;
    uint32_t hash;
    uint16_t len;
    uint16_t fp16;
} Entry;

typedef struct __attribute__((aligned(CACHELINE))) {
    Entry   *entries;
    char    *pool;
    char   **overflow;

    size_t   pool_used;
    size_t   cap;
    size_t   len;    /* unique */

    uint64_t total;  /* tokens */
    uint64_t chars;  /* total token chars (after truncation) */

    size_t   overflow_count;
    size_t   overflow_cap;
} Table;

typedef struct {
    const char *data;
    size_t      start;
    size_t      end;
    Table      *table;
    int         id;
    int         pin_vcache;
} WorkUnit;

typedef struct {
    int threads;     /* 0 = auto */
    int topn;
    int json;
    int color;       /* 0/1 */
    int pin_vcache;  /* 0/1 */
} Options;

typedef struct {
    const char *bold;
    const char *dim;
    const char *reset;
    const char *green;
    const char *cyan;
    const char *yellow;
} Ansi;

/*===========================================================================
 * Globals
 *===========================================================================*/

static int               g_nthreads;
static Table            *g_tables;
static pthread_t        *g_threads;
static WorkUnit         *g_units;
static pthread_barrier_t g_barrier;

static int g_vcache_cpus[256];
static int g_vcache_count;

/*===========================================================================
 * Small utils
 *===========================================================================*/

static inline int is_alpha(unsigned c) { return ((c | 32u) - 'a') < 26u; }

static void *xaligned_alloc(size_t align, size_t size)
{
    void *p = NULL;
    if (posix_memalign(&p, align, size) != 0) return NULL;
    return p;
}

static inline size_t next_pow2(size_t x)
{
    if (x <= 1) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if UINTPTR_MAX > 0xffffffffu
    x |= x >> 32;
#endif
    return x + 1;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static const char *fmt_u64(uint64_t v, char buf[32])
{
    char tmp[32];
    int i = 0;
    int group = 0;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return buf;
    }

    while (v) {
        if (group == 3) {
            tmp[i++] = ',';
            group = 0;
        }
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
        group++;
    }

    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
    return buf;
}

static const char *fmt_bytes(uint64_t bytes, char buf[32])
{
    const char *unit = "B";
    double v = (double)bytes;

    if (bytes >= (1ull << 30)) { unit = "GB"; v /= (double)(1ull << 30); }
    else if (bytes >= (1ull << 20)) { unit = "MB"; v /= (double)(1ull << 20); }
    else if (bytes >= (1ull << 10)) { unit = "KB"; v /= (double)(1ull << 10); }

    snprintf(buf, 32, "%.2f %s", v, unit);
    return buf;
}

static inline uint64_t clamp_u64(uint64_t x, uint64_t lo, uint64_t hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/*===========================================================================
 * V-Cache Detection (AMD Zen 4+ 3D V-Cache heuristic)
 *===========================================================================*/

static void detect_vcache(void)
{
    char path[256], buf[512], sizebuf[64];
    int ncpus = (int)sysconf(_SC_NPROCESSORS_CONF);
    size_t best_l3 = 0;

    g_vcache_count = 0;

    for (int cpu = 0; cpu < ncpus && cpu < 256; cpu++) {
        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%d/cache/index3/size", cpu);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(sizebuf, sizeof sizebuf, f)) { fclose(f); continue; }
        fclose(f);

        char *end;
        size_t l3 = strtoul(sizebuf, &end, 10);
        if (end && (*end == 'K' || *end == 'k')) l3 <<= 10;
        else if (end && (*end == 'M' || *end == 'm')) l3 <<= 20;

        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", cpu);
        f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(buf, sizeof buf, f)) { fclose(f); continue; }
        fclose(f);

        if (l3 > best_l3) {
            best_l3 = l3;
            g_vcache_count = 0;
            const char *p = buf;
            while (*p && g_vcache_count < 256) {
                long start = strtol(p, &end, 10);
                if (end == p) break;
                if (*end == '-') {
                    long stop = strtol(end + 1, &end, 10);
                    for (long i = start; i <= stop && g_vcache_count < 256; i++)
                        g_vcache_cpus[g_vcache_count++] = (int)i;
                } else {
                    g_vcache_cpus[g_vcache_count++] = (int)start;
                }
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
                else p = end;
            }
        }
    }
}

/*===========================================================================
 * Hash: CRC32C (hw) or wyhash-ish fallback
 *===========================================================================*/

#ifdef __SSE4_2__
static inline uint32_t hash_word(const char *s, size_t len)
{
    uint64_t h = 0;
    const uint8_t *p = (const uint8_t *)s;

    while (len >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        h = _mm_crc32_u64(h, v);
        p += 8;
        len -= 8;
    }
    if (len >= 4) {
        uint32_t v;
        memcpy(&v, p, 4);
        h = _mm_crc32_u32((uint32_t)h, v);
        p += 4;
        len -= 4;
    }
    while (len--) h = _mm_crc32_u8((uint32_t)h, *p++);

    /* Murmur-ish finalizer */
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (uint32_t)h;
}
#else
static inline uint64_t wymix(uint64_t a, uint64_t b)
{
    __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)(r ^ (r >> 64));
}

static inline uint32_t hash_word(const char *s, size_t len)
{
    const uint8_t *p = (const uint8_t *)s;
    uint64_t seed = 0xa0761d6478bd642full, a, b;

    if (len <= 16) {
        if (len >= 4) {
            uint32_t v1, v2, v3, v4;
            memcpy(&v1, p, 4);
            memcpy(&v2, p + ((len >> 3) << 2), 4);
            memcpy(&v3, p + len - 4, 4);
            memcpy(&v4, p + len - 4 - ((len >> 3) << 2), 4);
            a = ((uint64_t)v1 << 32) | v2;
            b = ((uint64_t)v3 << 32) | v4;
        } else if (len) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        if (i > 48) {
            uint64_t s1 = seed, s2 = seed;
            do {
                uint64_t v[6];
                memcpy(v, p, 48);
                s1 = wymix(v[0] ^ 0xe7037ed1a0b428dbull, v[1] ^ s1);
                s2 = wymix(v[2] ^ 0x8ebc6af09c88c6e3ull, v[3] ^ s2);
                seed = wymix(v[4] ^ 0x589965cc75374cc3ull, v[5] ^ seed);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= s1 ^ s2;
        }
        while (i > 16) {
            uint64_t v[2];
            memcpy(v, p, 16);
            seed = wymix(v[0] ^ 0xe7037ed1a0b428dbull, v[1] ^ seed);
            p += 16;
            i -= 16;
        }
        uint64_t v[2];
        memcpy(&v[0], p + i - 16, 8);
        memcpy(&v[1], p + i - 8, 8);
        a = v[0];
        b = v[1];
    }

    return (uint32_t)wymix(0xe7037ed1a0b428dbull ^ len,
                           wymix(a ^ 0xe7037ed1a0b428dbull, b ^ seed));
}
#endif

/*===========================================================================
 * Arena allocator (with overflow list)
 *===========================================================================*/

static inline char *pool_alloc(Table *t, size_t len)
{
    const size_t need = (len + 1 + 7u) & ~7ull; /* include NUL + 8-byte align */

    if (t->pool_used + need <= POOL_SIZE) {
        char *p = t->pool + t->pool_used;
        t->pool_used += need;
        return p;
    }

    /* overflow */
    char *p = (char *)malloc(len + 1);
    if (!p) return NULL;

    if (t->overflow_count == t->overflow_cap) {
        size_t cap = t->overflow_cap ? (t->overflow_cap * 2) : 64;
        char **n = (char **)realloc(t->overflow, cap * sizeof(char *));
        if (!n) return NULL;
        t->overflow = n;
        t->overflow_cap = cap;
    }
    t->overflow[t->overflow_count++] = p;
    return p;
}

/*===========================================================================
 * Hash table
 *===========================================================================*/

static int table_init(Table *t, size_t cap)
{
    memset(t, 0, sizeof *t);

    t->entries = (Entry *)xaligned_alloc(CACHELINE, cap * sizeof(Entry));
    t->pool    = (char *)xaligned_alloc(CACHELINE, POOL_SIZE);
    if (!t->entries || !t->pool) return -1;

    memset(t->entries, 0, cap * sizeof(Entry));
    t->cap = cap;

    (void)madvise(t->entries, cap * sizeof(Entry), MADV_HUGEPAGE);
    (void)madvise(t->pool, POOL_SIZE, MADV_HUGEPAGE);
    return 0;
}

static void table_free(Table *t)
{
    for (size_t i = 0; i < t->overflow_count; i++) free(t->overflow[i]);
    free(t->overflow);
    free(t->entries);
    free(t->pool);
}

static void table_grow(Table *t)
{
    const size_t nc = t->cap * 2;
    Entry *ne = (Entry *)xaligned_alloc(CACHELINE, nc * sizeof(Entry));
    if (!ne) return; /* YOLO: fun tool */
    memset(ne, 0, nc * sizeof(Entry));

    const size_t mask = nc - 1;
    for (size_t i = 0; i < t->cap; i++) {
        if (!t->entries[i].word) continue;
        size_t idx = (size_t)t->entries[i].hash & mask;
        while (ne[idx].word) idx = (idx + 1) & mask;
        ne[idx] = t->entries[i];
    }

    free(t->entries);
    t->entries = ne;
    t->cap = nc;
    (void)madvise(ne, nc * sizeof(Entry), MADV_HUGEPAGE);
}

static inline void table_insert(Table *t, const char *word, size_t len, uint32_t h)
{
    if ((t->len + 1) * 10 >= t->cap * 7) table_grow(t);

    const uint16_t fp = (uint16_t)(h ^ (h >> 16));
    const size_t mask = t->cap - 1;
    size_t idx = (size_t)h & mask;

    for (;;) {
        Entry *e = &t->entries[idx];

        if (!e->word) {
            char *s = pool_alloc(t, len);
            if (!s) return;
            memcpy(s, word, len);
            s[len] = 0;

            *e = (Entry){ .word = s, .count = 1, .hash = h, .len = (uint16_t)len, .fp16 = fp };
            t->len++;
            t->total++;
            t->chars += len;
            return;
        }

        if (e->hash == h && e->len == len && e->fp16 == fp &&
            memcmp(e->word, word, len) == 0) {
            e->count++;
            t->total++;
            t->chars += len;
            return;
        }

        idx = (idx + 1) & mask;
    }
}

/*===========================================================================
 * Tokenizers
 *===========================================================================*/

#ifdef __AVX512BW__
static void tokenize(Table *t, const char *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;

    char word[MAX_WORD];
    size_t wlen = 0;

    const __m512i v_a = _mm512_set1_epi8('a');
    const __m512i v_z = _mm512_set1_epi8('z');
    const __m512i v_20 = _mm512_set1_epi8(0x20);

    while (p + 64 <= end) {
        __builtin_prefetch(p + 512, 0, 0);

        __m512i chunk = _mm512_loadu_si512((const __m512i *)p);
        __m512i lower = _mm512_or_si512(chunk, v_20);
        __mmask64 ge_a = _mm512_cmpge_epu8_mask(lower, v_a);
        __mmask64 le_z = _mm512_cmple_epu8_mask(lower, v_z);
        uint64_t alpha = (uint64_t)(ge_a & le_z);

        if (alpha == 0) {
            if (wlen) {
                table_insert(t, word, wlen, hash_word(word, wlen));
                wlen = 0;
            }
            p += 64;
            continue;
        }

        if (alpha == UINT64_MAX) {
            /* All letters: bulk-lowercase into buffer (truncate to MAX_WORD-1) */
            char tmp[64] __attribute__((aligned(64)));
            _mm512_storeu_si512((__m512i *)tmp, lower);

            size_t room = (MAX_WORD - 1) - wlen;
            size_t take = room < 64 ? room : 64;
            if (take) {
                memcpy(word + wlen, tmp, take);
                wlen += take;
            }
            p += 64;
            continue;
        }

        /* Mixed: scalar over 64 bytes */
        for (int i = 0; i < 64; i++, p++) {
            uint8_t c = *p;
            if (is_alpha(c)) {
                if (wlen < MAX_WORD - 1) word[wlen++] = (char)(c | 0x20);
            } else if (wlen) {
                table_insert(t, word, wlen, hash_word(word, wlen));
                wlen = 0;
            }
        }
    }

    /* Tail */
    while (p < end) {
        uint8_t c = *p++;
        if (is_alpha(c)) {
            if (wlen < MAX_WORD - 1) word[wlen++] = (char)(c | 0x20);
        } else if (wlen) {
            table_insert(t, word, wlen, hash_word(word, wlen));
            wlen = 0;
        }
    }

    if (wlen) table_insert(t, word, wlen, hash_word(word, wlen));
}

#elif defined(__AVX2__)

static inline __m256i is_alpha_vec(__m256i v)
{
    __m256i lower = _mm256_or_si256(v, _mm256_set1_epi8(0x20));
    __m256i ge_a = _mm256_cmpgt_epi8(lower, _mm256_set1_epi8('a' - 1));
    __m256i le_z = _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), lower);
    return _mm256_and_si256(ge_a, le_z);
}

static void tokenize(Table *t, const char *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;

    char word[MAX_WORD];
    size_t wlen = 0;

    const __m256i v_20 = _mm256_set1_epi8(0x20);

    while (p + 32 <= end) {
        __builtin_prefetch(p + 256, 0, 0);

        __m256i chunk = _mm256_loadu_si256((const __m256i *)p);
        uint32_t alpha = (uint32_t)_mm256_movemask_epi8(is_alpha_vec(chunk));

        if (alpha == 0) {
            if (wlen) {
                table_insert(t, word, wlen, hash_word(word, wlen));
                wlen = 0;
            }
            p += 32;
            continue;
        }

        if (alpha == 0xFFFFFFFFu) {
            /* All letters: bulk-lowercase into buffer (truncate safely) */
            __m256i lower = _mm256_or_si256(chunk, v_20);

            size_t room = (MAX_WORD - 1) - wlen;
            if (room) {
                if (wlen <= (MAX_WORD - 1) - 32) {
                    _mm256_storeu_si256((__m256i *)(word + wlen), lower);
                    wlen += 32;
                } else {
                    char tmp[32] __attribute__((aligned(32)));
                    _mm256_storeu_si256((__m256i *)tmp, lower);
                    memcpy(word + wlen, tmp, room);
                    wlen += room;
                }
            }
            p += 32;
            continue;
        }

        /* Mixed.
         * Fix for a subtle boundary bug: if we were mid-word and the first byte
         * of this chunk is non-alpha, we must flush before skipping to first alpha.
         */
        if (wlen && !(alpha & 1u)) {
            table_insert(t, word, wlen, hash_word(word, wlen));
            wlen = 0;
        }

        int first = __builtin_ctz(alpha);
        p += (size_t)first;

        while (p < end && is_alpha(*p)) {
            if (wlen < MAX_WORD - 1) word[wlen++] = (char)(*p | 0x20);
            p++;
        }

        if (wlen) {
            table_insert(t, word, wlen, hash_word(word, wlen));
            wlen = 0;
        }
    }

    /* Tail */
    while (p < end) {
        uint8_t c = *p++;
        if (is_alpha(c)) {
            if (wlen < MAX_WORD - 1) word[wlen++] = (char)(c | 0x20);
        } else if (wlen) {
            table_insert(t, word, wlen, hash_word(word, wlen));
            wlen = 0;
        }
    }

    if (wlen) table_insert(t, word, wlen, hash_word(word, wlen));
}

#else
/* Scalar */
static void tokenize(Table *t, const char *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;

    char word[MAX_WORD];
    size_t wlen = 0;

    while (p < end) {
        uint8_t c = *p++;
        if (is_alpha(c)) {
            if (wlen < MAX_WORD - 1) word[wlen++] = (char)(c | 0x20);
        } else if (wlen) {
            table_insert(t, word, wlen, hash_word(word, wlen));
            wlen = 0;
        }
    }

    if (wlen) table_insert(t, word, wlen, hash_word(word, wlen));
}
#endif

/*===========================================================================
 * Worker
 *===========================================================================*/

static void *worker(void *arg)
{
    WorkUnit *u = (WorkUnit *)arg;

    if (u->pin_vcache && g_vcache_count > 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(g_vcache_cpus[u->id % g_vcache_count], &set);
        (void)pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }

    pthread_barrier_wait(&g_barrier);
    tokenize(u->table, u->data + u->start, u->end - u->start);
    return NULL;
}

/*===========================================================================
 * Merge
 *===========================================================================*/

static Entry *merge_tables(size_t *out_unique, uint64_t *out_total, uint64_t *out_chars, size_t *out_cap)
{
    size_t est = 0;
    uint64_t total = 0;
    uint64_t chars = 0;
    for (int i = 0; i < g_nthreads; i++) {
        est += g_tables[i].len;
        total += g_tables[i].total;
        chars += g_tables[i].chars;
    }

    size_t cap = next_pow2((est ? est : 1) * 2);
    if (cap < 1024) cap = 1024;

    Entry *g = (Entry *)xaligned_alloc(CACHELINE, cap * sizeof(Entry));
    if (!g) return NULL;
    memset(g, 0, cap * sizeof(Entry));

    const size_t mask = cap - 1;
    size_t unique = 0;

    for (int ti = 0; ti < g_nthreads; ti++) {
        Table *t = &g_tables[ti];
        for (size_t i = 0; i < t->cap; i++) {
            const Entry *e = &t->entries[i];
            if (!e->word) continue;

            size_t idx = (size_t)e->hash & mask;
            while (g[idx].word) {
                if (g[idx].hash == e->hash && g[idx].len == e->len && g[idx].fp16 == e->fp16 &&
                    memcmp(g[idx].word, e->word, e->len) == 0) {
                    g[idx].count += e->count;
                    goto next_entry;
                }
                idx = (idx + 1) & mask;
            }
            g[idx] = *e;
            unique++;

        next_entry:;
        }
    }

    *out_unique = unique;
    *out_total = total;
    *out_chars = chars;
    *out_cap = cap;
    return g;
}

/*===========================================================================
 * Top-N selection (heap)
 *===========================================================================*/

typedef struct {
    const Entry *e;
} TopPtr;

static inline int entry_better(const Entry *a, const Entry *b)
{
    if (a->count != b->count) return a->count > b->count;
    return strcmp(a->word, b->word) < 0;
}

static inline int entry_worse(const Entry *a, const Entry *b)
{
    if (a->count != b->count) return a->count < b->count;
    return strcmp(a->word, b->word) > 0;
}

static inline void heap_swap(TopPtr *a, TopPtr *b)
{
    TopPtr t = *a;
    *a = *b;
    *b = t;
}

static void heap_sift_up(TopPtr *h, size_t idx)
{
    while (idx) {
        size_t parent = (idx - 1) / 2;
        if (!entry_worse(h[idx].e, h[parent].e)) break;
        heap_swap(&h[idx], &h[parent]);
        idx = parent;
    }
}

static void heap_sift_down(TopPtr *h, size_t n, size_t idx)
{
    for (;;) {
        size_t l = idx * 2 + 1;
        if (l >= n) break;
        size_t r = l + 1;

        size_t smallest = idx; /* 'smallest' == worst */
        if (entry_worse(h[l].e, h[smallest].e)) smallest = l;
        if (r < n && entry_worse(h[r].e, h[smallest].e)) smallest = r;

        if (smallest == idx) break;
        heap_swap(&h[idx], &h[smallest]);
        idx = smallest;
    }
}

static int top_cmp_desc(const void *a, const void *b)
{
    const Entry *ea = ((const TopPtr *)a)->e;
    const Entry *eb = ((const TopPtr *)b)->e;

    if (ea->count != eb->count) return (eb->count > ea->count) ? 1 : -1;
    return strcmp(ea->word, eb->word);
}

static TopPtr *collect_topn(const Entry *merged, size_t cap, size_t unique, int topn, size_t *out_n)
{
    (void)unique;

    if (topn <= 0) { *out_n = 0; return NULL; }

    TopPtr *heap = (TopPtr *)malloc((size_t)topn * sizeof(TopPtr));
    if (!heap) { *out_n = 0; return NULL; }

    size_t hlen = 0;
    for (size_t i = 0; i < cap; i++) {
        const Entry *e = &merged[i];
        if (!e->word) continue;

        if (hlen < (size_t)topn) {
            heap[hlen++].e = e;
            heap_sift_up(heap, hlen - 1);
            continue;
        }

        if (entry_better(e, heap[0].e)) {
            heap[0].e = e;
            heap_sift_down(heap, hlen, 0);
        }
    }

    qsort(heap, hlen, sizeof(TopPtr), top_cmp_desc);
    *out_n = hlen;
    return heap;
}

/*===========================================================================
 * Output
 *===========================================================================*/

static void print_table(const Ansi *a,
                        const char *file,
                        uint64_t file_size,
                        const char *mode,
                        const char *hashname,
                        int threads,
                        int vcache_count,
                        const Entry *merged,
                        size_t cap,
                        size_t unique,
                        uint64_t total,
                        uint64_t chars,
                        int topn,
                        double ms)
{
    char buf1[32], buf2[32], buf3[32], buf4[32];

    printf("%s%s%s  v%s\n", a->bold, "wcx", a->reset, WCX_VERSION);
    printf("File:    %s (%s)\n", file, fmt_bytes(file_size, buf1));
    printf("Mode:    %s  |  Hash: %s\n", mode, hashname);
    printf("Threads: %d", threads);
    if (vcache_count > 0) printf("  |  Pin: V-Cache (%d cores)", vcache_count);
    printf("\n");
    printf("Token:   [A-Za-z]+ (lowercased), max %d chars\n", MAX_WORD - 1);

    if (total == 0 || unique == 0) {
        printf("\n%s(no tokens found)%s\n", a->dim, a->reset);
        printf("\nTotal words:  0\nUnique words: 0\nTime:         %.2f ms\n", ms);
        return;
    }

    size_t nout = 0;
    TopPtr *top = collect_topn(merged, cap, unique, topn, &nout);

    uint32_t top_count = (nout > 0) ? top[0].e->count : 0;

    printf("\n%s%4s  %-24s  %12s  %7s  %s%s\n",
           a->bold, "#", "Word", "Count", "Share", "Bar", a->reset);
    printf("────  ────────────────────────  ────────────  ───────  ─────────────────────────────\n");

    const int bar_w = 28;
    for (size_t i = 0; i < nout; i++) {
        const Entry *e = top[i].e;
        double pct = 100.0 * (double)e->count / (double)total;

        int filled = 0;
        if (top_count) {
            double ratio = (double)e->count / (double)top_count;
            filled = (int)(ratio * (double)bar_w + 0.5);
            filled = (int)clamp_u64((uint64_t)filled, 0, (uint64_t)bar_w);
        }

        /* word column: truncate with ellipsis if needed */
        char wbuf[32];
        const char *w = e->word;
        size_t wl = strlen(w);
        if (wl <= 24) {
            snprintf(wbuf, sizeof wbuf, "%s", w);
        } else {
            snprintf(wbuf, sizeof wbuf, "%.21s...", w);
        }

        printf("%4zu  %-24s  %12s  %6.2f%%  ",
               i + 1,
               wbuf,
               fmt_u64(e->count, buf2),
               pct);

        /* bar */
        for (int k = 0; k < filled; k++) fputs("█", stdout);
        for (int k = filled; k < bar_w; k++) fputs(" ", stdout);
        putchar('\n');
    }

    free(top);

    const double sec = ms / 1000.0;
    const double mb = (double)file_size / (1024.0 * 1024.0);
    const double mbps = (sec > 0) ? (mb / sec) : 0;
    const double wps = (sec > 0) ? ((double)total / sec) : 0;
    const double avg_len = (total > 0) ? ((double)chars / (double)total) : 0;

    printf("\n");
    printf("Total words:  %s\n", fmt_u64(total, buf3));
    printf("Unique words: %s\n", fmt_u64(unique, buf4));
    printf("Avg length:   %.2f\n", avg_len);
    printf("Time:         %.2f ms\n", ms);
    printf("Throughput:   %s%.2f MB/s%s\n", a->green, mbps, a->reset);
    printf("Rate:         %.2f Mwords/s\n", wps / 1e6);
}

static void print_json(const char *file,
                       uint64_t file_size,
                       const char *mode,
                       const char *hashname,
                       int threads,
                       int vcache_count,
                       const Entry *merged,
                       size_t cap,
                       size_t unique,
                       uint64_t total,
                       uint64_t chars,
                       int topn,
                       double ms)
{
    size_t nout = 0;
    TopPtr *top = (total && unique) ? collect_topn(merged, cap, unique, topn, &nout) : NULL;

    const double sec = ms / 1000.0;
    const double mb = (double)file_size / (1024.0 * 1024.0);
    const double mbps = (sec > 0) ? (mb / sec) : 0;

    printf("{\n");
    printf("  \"tool\": \"wcx\",\n");
    printf("  \"version\": \"%s\",\n", WCX_VERSION);
    printf("  \"file\": \"%s\",\n", file);
    printf("  \"file_bytes\": %llu,\n", (unsigned long long)file_size);
    printf("  \"mode\": \"%s\",\n", mode);
    printf("  \"hash\": \"%s\",\n", hashname);
    printf("  \"threads\": %d,\n", threads);
    printf("  \"vcache_pinned_cores\": %d,\n", vcache_count);
    printf("  \"total_words\": %llu,\n", (unsigned long long)total);
    printf("  \"unique_words\": %llu,\n", (unsigned long long)unique);
    printf("  \"avg_length\": %.4f,\n", (total ? (double)chars / (double)total : 0.0));
    printf("  \"time_ms\": %.3f,\n", ms);
    printf("  \"throughput_mb_s\": %.3f,\n", mbps);
    printf("  \"top\": [\n");

    for (size_t i = 0; i < nout; i++) {
        const Entry *e = top[i].e;
        double pct = (total ? 100.0 * (double)e->count / (double)total : 0.0);
        printf("    {\"rank\": %zu, \"word\": \"%s\", \"count\": %u, \"share\": %.6f}%s\n",
               i + 1, e->word, e->count, pct,
               (i + 1 == nout) ? "" : ",");
    }

    printf("  ]\n");
    printf("}\n");

    free(top);
}

/*===========================================================================
 * CLI
 *===========================================================================*/

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [options] <file>\n\n"
            "options:\n"
            "  -t, --threads N     threads (default: auto)\n"
            "  -n, --top N         show top N words (default: %d)\n"
            "      --json          JSON output\n"
            "      --no-color      disable ANSI colors\n"
            "      --no-vcache     don't pin threads to largest L3 group\n"
            "  -h, --help\n",
            argv0, DEFAULT_TOPN);
}

static Options parse_args(int argc, char **argv)
{
    Options o;
    o.threads = 0;
    o.topn = DEFAULT_TOPN;
    o.json = 0;
    o.color = -1; /* auto */
    o.pin_vcache = 1;

    static const struct option long_opts[] = {
        {"threads", required_argument, 0, 't'},
        {"top", required_argument, 0, 'n'},
        {"json", no_argument, 0, 1},
        {"no-color", no_argument, 0, 2},
        {"no-vcache", no_argument, 0, 3},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "t:n:h", long_opts, NULL);
        if (opt == -1) break;

        switch (opt) {
        case 't':
            o.threads = atoi(optarg);
            break;
        case 'n':
            o.topn = atoi(optarg);
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        case 1:
            o.json = 1;
            break;
        case 2:
            o.color = 0;
            break;
        case 3:
            o.pin_vcache = 0;
            break;
        default:
            usage(argv[0]);
            exit(1);
        }
    }

    if (o.topn < 0) o.topn = 0;
    if (o.threads < 0) o.threads = 0;

    return o;
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(int argc, char **argv)
{
    Options opt = parse_args(argc, argv);

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    const char *path = argv[optind];

    /* ANSI color auto-detect */
    if (opt.color == -1) opt.color = isatty(STDOUT_FILENO);

    const Ansi ansi = opt.color
        ? (Ansi){"\033[1m", "\033[2m", "\033[0m", "\033[1;32m", "\033[1;36m", "\033[1;33m"}
        : (Ansi){"", "", "", "", "", ""};

    const double t0 = now_ms();

    if (opt.pin_vcache) detect_vcache();

    int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus < 1) ncpus = 1;

    /* Open file */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "stat %s: %s\n", path, strerror(errno));
        close(fd);
        return 1;
    }

    uint64_t size = (uint64_t)st.st_size;

    /* Empty file: avoid mmap(0) */
    if (size == 0) {
        const double t1 = now_ms();
        const char *mode =
#ifdef __AVX512BW__
            "AVX-512";
#elif defined(__AVX2__)
            "AVX2";
#else
            "Scalar";
#endif

        const char *hashname =
#ifdef __SSE4_2__
            "CRC32C";
#else
            "wyhash";
#endif

        if (opt.json) {
            print_json(path, 0, mode, hashname, 1, 0, NULL, 0, 0, 0, 0, opt.topn, t1 - t0);
        } else {
            print_table(&ansi, path, 0, mode, hashname, 1, 0, NULL, 0, 0, 0, 0, opt.topn, t1 - t0);
        }
        close(fd);
        return 0;
    }

    /* Threads */
    int threads = opt.threads ? opt.threads : ncpus;
    if (threads > MAX_THREADS) threads = MAX_THREADS;
    if (threads < 1) threads = 1;

    /* Simple size-based clamp (avoid too many threads on tiny files) */
    if (!opt.threads) {
        const uint64_t min_chunk = 1ull << 20; /* 1 MiB */
        int by_size = (int)(size / min_chunk);
        if (by_size < 1) by_size = 1;
        if (threads > by_size) threads = by_size;
    }

    g_nthreads = threads;

    const char *mode =
#ifdef __AVX512BW__
        "AVX-512";
#elif defined(__AVX2__)
        "AVX2";
#else
        "Scalar";
#endif

    const char *hashname =
#ifdef __SSE4_2__
        "CRC32C";
#else
        "wyhash";
#endif

    /* mmap */
    const char *data = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (data == MAP_FAILED) {
        data = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            fprintf(stderr, "mmap %s: %s\n", path, strerror(errno));
            close(fd);
            return 1;
        }
    }

    (void)madvise((void *)data, (size_t)size, MADV_SEQUENTIAL | MADV_WILLNEED);

    /* Allocate thread structures */
    g_tables = (Table *)calloc((size_t)g_nthreads, sizeof(Table));
    g_threads = (pthread_t *)malloc((size_t)g_nthreads * sizeof(pthread_t));
    g_units = (WorkUnit *)malloc((size_t)g_nthreads * sizeof(WorkUnit));

    if (!g_tables || !g_threads || !g_units) {
        fprintf(stderr, "oom\n");
        return 1;
    }

    /* Partition file on word boundaries */
    size_t chunk = (size_t)(size / (uint64_t)g_nthreads);
    size_t pos = 0;

    for (int i = 0; i < g_nthreads; i++) {
        size_t end = (i == g_nthreads - 1) ? (size_t)size : pos + chunk;

        while (end < (size_t)size && is_alpha((unsigned char)data[end])) end++;

        size_t seg = end - pos;
        size_t est_unique = seg / 25; /* ~5 chars/word, ~5 repeats */
        size_t cap = next_pow2(est_unique * 2);
        if (cap < INITIAL_CAP) cap = INITIAL_CAP;

        if (table_init(&g_tables[i], cap) != 0) {
            fprintf(stderr, "table_init: oom\n");
            return 1;
        }

        g_units[i] = (WorkUnit){
            .data = data,
            .start = pos,
            .end = end,
            .table = &g_tables[i],
            .id = i,
            .pin_vcache = opt.pin_vcache,
        };

        pos = end;
    }

    /* Launch workers */
    pthread_barrier_init(&g_barrier, NULL, (unsigned)g_nthreads + 1);

    for (int i = 0; i < g_nthreads; i++) {
        pthread_create(&g_threads[i], NULL, worker, &g_units[i]);
    }

    pthread_barrier_wait(&g_barrier);
    for (int i = 0; i < g_nthreads; i++) pthread_join(g_threads[i], NULL);
    pthread_barrier_destroy(&g_barrier);

    /* Merge */
    size_t unique = 0, mcap = 0;
    uint64_t total = 0, chars = 0;
    Entry *merged = merge_tables(&unique, &total, &chars, &mcap);

    const double t1 = now_ms();
    const double ms = t1 - t0;

    if (!merged) {
        fprintf(stderr, "merge: oom\n");
        return 1;
    }

    if (opt.json) {
        print_json(path, size, mode, hashname, g_nthreads,
                   (opt.pin_vcache ? g_vcache_count : 0),
                   merged, mcap, unique, total, chars, opt.topn, ms);
    } else {
        print_table(&ansi, path, size, mode, hashname, g_nthreads,
                    (opt.pin_vcache ? g_vcache_count : 0),
                    merged, mcap, unique, total, chars, opt.topn, ms);
    }

    /* Cleanup */
    free(merged);
    for (int i = 0; i < g_nthreads; i++) table_free(&g_tables[i]);
    free(g_tables);
    free(g_threads);
    free(g_units);
    munmap((void *)data, (size_t)size);
    close(fd);
    return 0;
}
