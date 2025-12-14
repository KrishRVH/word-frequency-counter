/*
** wc_test.c - Test suite + optional fuzz harness
**
** Public domain.
**
** DESIGN
**
**   - Happy path functionality
**   - Edge cases and boundary conditions
**   - Deterministic regression tests for collision-length hazards
**   - Static-buffer and max_bytes limit behavior
**   - Cursor/invariant checks without allocations (useful after NOMEM)
**
** OOM INJECTION
**
**   Same glibc-specific approach as before (malloc/realloc interpose).
**
** FUZZING (libFuzzer)
**
**   Build example:
**     clang -std=c99 -O1 -g -fsanitize=address,undefined,fuzzer \
**       -DWC_TEST_FUZZ wordcount.c wc_test.c -o wc_fuzz
**
**   Or standalone fuzz runner from stdin:
**     clang -std=c99 -O1 -g -fsanitize=address,undefined \
**       -DWC_TEST_FUZZ -DWC_TEST_FUZZ_STANDALONE \
**       wordcount.c wc_test.c -o wc_fuzz_stdin
**     cat corpus.bin | ./wc_fuzz_stdin
*/
#ifndef WC_NO_TEST_MAIN

#include "wordcount.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_run, g_pass, g_fail;

#define TEST(name)                \
    do {                          \
        g_run++;                  \
        printf("  %-55s ", name); \
        (void)fflush(stdout);     \
    } while (0)

#define PASS()            \
    do {                  \
        g_pass++;         \
        printf("[OK]\n"); \
    } while (0)

#define FAIL(m)                   \
    do {                          \
        g_fail++;                 \
        printf("[FAIL] %s\n", m); \
    } while (0)

#define ASSERT(c)     \
    do {              \
        if (!(c)) {   \
            FAIL(#c); \
            return 1; \
        }             \
    } while (0)

/* Aligned buffer helper for wc_limits.static_buf */
#define WC_TEST_STATIC_BUF(name, size_) \
    union {                             \
        void *p;                        \
        size_t sz;                      \
        unsigned long ul;               \
        unsigned char buf[(size_)];     \
    } name

/* --- OOM injection framework (glibc-specific) -------------------------- */

#if defined(WC_TEST_OOM) && defined(__GLIBC__)

static int oom_target = 0;
static int oom_count = 0;
static int oom_active = 0;

static void oom_reset(void)
{
    oom_target = 0;
    oom_count = 0;
    oom_active = 0;
}

static void oom_arm(int n)
{
    oom_target = n;
    oom_count = 0;
    oom_active = 1;
}

static int oom_check(void)
{
    if (!oom_active)
        return 0;
    oom_count++;
    if (oom_count == oom_target) {
        oom_active = 0;
        return 1;
    }
    return 0;
}

void *malloc(size_t n)
{
    extern void *__libc_malloc(size_t);
    if (oom_check()) {
        errno = ENOMEM;
        return NULL;
    }
    return __libc_malloc(n);
}

void *calloc(size_t nm, size_t sz)
{
    if (nm != 0 && sz > SIZE_MAX / nm) {
        errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(nm * sz);
    if (p)
        memset(p, 0, nm * sz);
    return p;
}

void *realloc(void *ptr, size_t n)
{
    extern void *__libc_realloc(void *, size_t);
    if (oom_check()) {
        errno = ENOMEM;
        return NULL;
    }
    return __libc_realloc(ptr, n);
}

#else

#if defined(__GNUC__) || defined(__clang__)
#define OOM_UNUSED __attribute__((unused))
#else
#define OOM_UNUSED
#endif

OOM_UNUSED static void oom_reset(void) {}
OOM_UNUSED static void oom_arm(int n)
{
    (void)n;
}

#endif /* WC_TEST_OOM && __GLIBC__ */

/* --- Invariant checks -------------------------------------------------- */

static int invariant_cursor_sum_matches_total(const wc *w)
{
    wc_cursor c;
    size_t seen = 0;
    size_t sum = 0;

    wc_cursor_init(&c, w);

    for (;;) {
        const char *word = NULL;
        size_t count = 0;

        if (!wc_cursor_next(&c, &word, &count))
            break;

        /* Basic sanity */
        if (!word || count == 0)
            return 0;

        seen++;
        sum += count; /* modulo behavior matches size_t totals */
    }

    return (seen == wc_unique(w)) && (sum == wc_total(w));
}

/* --- Lifecycle / Limits tests ----------------------------------------- */

static int test_open_close(void)
{
    wc *w;
    TEST("open and close");
    w = wc_open(0);
    ASSERT(w != NULL);
    ASSERT(wc_total(w) == 0);
    ASSERT(wc_unique(w) == 0);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_close_null(void)
{
    TEST("close NULL");
    wc_close(NULL);
    PASS();
    return 0;
}

static int test_max_word_clamp(void)
{
    wc *w;
    TEST("max_word clamping");
    w = wc_open(1);
    ASSERT(w != NULL);
    wc_close(w);
    w = wc_open(9999);
    ASSERT(w != NULL);
    wc_close(w);
    PASS();
    return 0;
}

static int test_max_word_clamped_to_wc_max_word(void)
{
    wc *w;
    wc_word *r;
    size_t n;
    size_t i;
    char word[WC_MAX_WORD + 16];

    TEST("max_word clamped to WC_MAX_WORD");

    for (i = 0; i < WC_MAX_WORD + 8; i++)
        word[i] = 'a';
    word[WC_MAX_WORD + 8] = '\0';

    w = wc_open(WC_MAX_WORD + 1000);
    ASSERT(w != NULL);
    ASSERT(wc_add(w, word) == WC_OK);
    ASSERT(wc_results(w, &r, &n) == WC_OK);
    ASSERT(n == 1);
    ASSERT(strlen(r[0].word) == WC_MAX_WORD);

    wc_results_free(r);
    wc_close(w);

    PASS();
    return 0;
}

static int test_open_ex_null_limits(void)
{
    wc *w;
    TEST("open_ex NULL limits");
    w = wc_open_ex(0, NULL);
    ASSERT(w != NULL);
    ASSERT(wc_total(w) == 0);
    ASSERT(wc_unique(w) == 0);
    wc_close(w);
    PASS();
    return 0;
}

static int test_open_ex_tiny_budget_fail(void)
{
    wc_limits lim;
    TEST("open_ex tiny max_bytes fails");
    memset(&lim, 0, sizeof lim);
    lim.max_bytes = 1;
    ASSERT(wc_open_ex(0, &lim) == NULL);
    PASS();
    return 0;
}

static int test_open_ex_tiny_static_fail(void)
{
    wc_limits lim;
    WC_TEST_STATIC_BUF(pool, 32);

    TEST("open_ex tiny static_buf fails");

    memset(&lim, 0, sizeof lim);
    lim.static_buf = pool.buf;
    lim.static_size = sizeof pool.buf;

    ASSERT(wc_open_ex(0, &lim) == NULL);

    PASS();
    return 0;
}

static int test_static_limits_enforced(void)
{
    wc_limits lim;
    WC_TEST_STATIC_BUF(pool, 4096);
    wc *w;
    size_t i;
    char word[32];
    int rc;
    wc_word *r = NULL;
    size_t n = 0;

    TEST("static_buf enforces capacity");

    memset(&lim, 0, sizeof lim);
    lim.static_buf = pool.buf;
    lim.static_size = sizeof pool.buf;

    w = wc_open_ex(0, &lim);
    ASSERT(w != NULL);

    rc = WC_OK;
    for (i = 0; i < 100000 && rc == WC_OK; i++) {
        (void)snprintf(word, sizeof word, "w%zu", i);
        rc = wc_add(w, word);
        ASSERT(rc == WC_OK || rc == WC_NOMEM);
    }

    ASSERT(wc_unique(w) > 0);
    ASSERT(invariant_cursor_sum_matches_total(w));

    {
        const char *t = "alpha beta gamma delta epsilon";
        int rc2 = wc_scan(w, t, strlen(t));
        ASSERT(rc2 == WC_OK || rc2 == WC_NOMEM);
    }

    ASSERT(wc_results(w, &r, &n) == WC_OK);
    ASSERT(n == wc_unique(w));
    wc_results_free(r);

    wc_close(w);
    PASS();
    return 0;
}

static int test_static_with_tiny_max_bytes_fails(void)
{
    wc_limits lim;
    WC_TEST_STATIC_BUF(pool, 4096);

    TEST("static_buf + tiny max_bytes fails");

    memset(&lim, 0, sizeof lim);
    lim.static_buf = pool.buf;
    lim.static_size = sizeof pool.buf;
    lim.max_bytes = 1;

    ASSERT(wc_open_ex(0, &lim) == NULL);

    PASS();
    return 0;
}

static int test_limits_budget_enforced(void)
{
    wc_limits lim;
    size_t i;
    char word[32];
    int rc;

    TEST("limits enforce max_bytes");

    memset(&lim, 0, sizeof lim);
    lim.max_bytes = 4096;

    wc *const w = wc_open_ex(0, &lim);
    ASSERT(w != NULL);

    rc = WC_OK;
    for (i = 0; i < 100000 && rc == WC_OK; i++) {
        (void)snprintf(word, sizeof word, "w%zu", i);
        rc = wc_add(w, word);
        ASSERT(rc == WC_OK || rc == WC_NOMEM);
    }

    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

/* Monotonic boundary test for static_size */
static int test_static_minimum_size_boundary(void)
{
    wc_limits lim;
    WC_TEST_STATIC_BUF(pool, 4096);
    size_t sz;

    TEST("static_buf minimum size boundary");

    memset(&lim, 0, sizeof lim);
    lim.static_buf = pool.buf;

    for (sz = 1; sz <= sizeof pool.buf; sz++) {
        wc *w;
        lim.static_size = sz;
        w = wc_open_ex(0, &lim);
        if (w) {
            wc_close(w);
            break;
        }
    }

    ASSERT(sz <= sizeof pool.buf);

    if (sz > 1) {
        lim.static_size = sz - 1;
        ASSERT(wc_open_ex(0, &lim) == NULL);
    }

    PASS();
    return 0;
}

/* --- wc_add tests ------------------------------------------------------ */

static int test_add_single(void)
{
    wc *w;
    TEST("add single");
    w = wc_open(0);
    ASSERT(wc_add(w, "hello") == WC_OK);
    ASSERT(wc_total(w) == 1);
    ASSERT(wc_unique(w) == 1);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_add_dup(void)
{
    wc *w;
    TEST("add duplicate");
    w = wc_open(0);
    ASSERT(wc_add(w, "hello") == WC_OK);
    ASSERT(wc_add(w, "hello") == WC_OK);
    ASSERT(wc_add(w, "hello") == WC_OK);
    ASSERT(wc_total(w) == 3);
    ASSERT(wc_unique(w) == 1);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_add_empty(void)
{
    wc *w;
    TEST("add empty string");
    w = wc_open(0);
    ASSERT(wc_add(w, "") == WC_OK);
    ASSERT(wc_total(w) == 0);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_add_null(void)
{
    wc *w;
    TEST("add NULL args");
    ASSERT(wc_add(NULL, "x") == WC_ERROR);
    w = wc_open(0);
    ASSERT(wc_add(w, NULL) == WC_ERROR);
    wc_close(w);
    PASS();
    return 0;
}

static int test_add_trunc(void)
{
    wc *w;
    wc_word *r;
    size_t n;
    TEST("add truncation");
    w = wc_open(4);
    ASSERT(wc_add(w, "abcdefghij") == WC_OK);
    ASSERT(wc_results(w, &r, &n) == WC_OK);
    ASSERT(n == 1);
    ASSERT(strcmp(r[0].word, "abcd") == 0);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_results_free(r);
    wc_close(w);
    PASS();
    return 0;
}

/* Deterministic hash collision regression (different lengths).
   Old buggy code could OOB-read under ASan on the second insert. */
static int test_add_hash_collision_different_length(void)
{
    wc *w;
    const char *a = "MXl";    /* 32-bit FNV-1a collides with b */
    const char *b = "QFdzF2"; /* longer */

    TEST("add: hash collision (different lengths) regression");

    w = wc_open(0);
    ASSERT(w != NULL);

    ASSERT(wc_add(w, a) == WC_OK);
    ASSERT(wc_add(w, b) == WC_OK);

    ASSERT(wc_unique(w) == 2);
    ASSERT(wc_total(w) == 2);
    ASSERT(invariant_cursor_sum_matches_total(w));

    wc_close(w);
    PASS();
    return 0;
}

/* --- wc_scan tests ----------------------------------------------------- */

static int test_scan_simple(void)
{
    wc *w;
    const char *t = "Hello World";
    TEST("scan simple");
    w = wc_open(0);
    ASSERT(wc_scan(w, t, strlen(t)) == WC_OK);
    ASSERT(wc_total(w) == 2);
    ASSERT(wc_unique(w) == 2);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_scan_empty(void)
{
    wc *w;
    TEST("scan empty");
    w = wc_open(0);
    ASSERT(wc_scan(w, "", 0) == WC_OK);
    ASSERT(wc_scan(w, NULL, 0) == WC_OK);
    ASSERT(wc_total(w) == 0);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_scan_null(void)
{
    wc *w;
    TEST("scan NULL args");
    ASSERT(wc_scan(NULL, "x", 1) == WC_ERROR);
    w = wc_open(0);
    ASSERT(wc_scan(w, NULL, 100) == WC_ERROR);
    wc_close(w);
    PASS();
    return 0;
}

static int test_scan_binary(void)
{
    wc *w;
    const char t[] = "hello\0world\0test";
    TEST("scan with embedded NUL");
    w = wc_open(0);
    ASSERT(wc_scan(w, t, sizeof(t) - 1) == WC_OK);
    ASSERT(wc_total(w) == 3);
    ASSERT(wc_unique(w) == 3);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

/* Hash collision regression via wc_scan using lowercase-only colliders.
   This specifically exercises the wc_scan path. */
static int test_scan_hash_collision_different_length(void)
{
    wc *w;
    const char *a = "svhpy";                 /* collides with b */
    const char *b = "znycrycwqhztadbhsrdok"; /* longer */
    char text[128];

    TEST("scan: hash collision (different lengths) regression");

    (void)snprintf(text, sizeof text, "%s %s", a, b);

    w = wc_open(0);
    ASSERT(w != NULL);

    ASSERT(wc_scan(w, text, strlen(text)) == WC_OK);
    ASSERT(wc_unique(w) == 2);
    ASSERT(wc_total(w) == 2);
    ASSERT(invariant_cursor_sum_matches_total(w));

    wc_close(w);
    PASS();
    return 0;
}

/* --- wc_results tests -------------------------------------------------- */

static int test_results_sorted(void)
{
    wc *w;
    wc_word *r;
    size_t n;
    const char *t = "apple banana apple cherry apple banana";
    TEST("results sorted");
    w = wc_open(0);
    ASSERT(wc_scan(w, t, strlen(t)) == WC_OK);
    ASSERT(wc_results(w, &r, &n) == WC_OK);
    ASSERT(n == 3);
    ASSERT(strcmp(r[0].word, "apple") == 0 && r[0].count == 3);
    ASSERT(strcmp(r[1].word, "banana") == 0 && r[1].count == 2);
    ASSERT(strcmp(r[2].word, "cherry") == 0 && r[2].count == 1);

    wc_results_free(r);
    ASSERT(invariant_cursor_sum_matches_total(w));
    wc_close(w);
    PASS();
    return 0;
}

static int test_results_empty(void)
{
    wc *w;
    wc_word *r = (wc_word *)0x1;
    size_t n = 999;

    TEST("results empty");
    w = wc_open(0);
    ASSERT(wc_results(w, &r, &n) == WC_OK);
    ASSERT(r == NULL && n == 0);
    wc_close(w);
    PASS();
    return 0;
}

static int test_results_null(void)
{
    wc *w;
    wc_word *r;
    size_t n;
    TEST("results NULL args");
    w = wc_open(0);
    ASSERT(wc_results(NULL, &r, &n) == WC_ERROR);
    ASSERT(wc_results(w, NULL, &n) == WC_ERROR);
    ASSERT(wc_results(w, &r, NULL) == WC_ERROR);
    wc_close(w);
    PASS();
    return 0;
}

/* --- Query / metadata tests ------------------------------------------- */

static int test_query_null(void)
{
    TEST("query NULL");
    ASSERT(wc_total(NULL) == 0);
    ASSERT(wc_unique(NULL) == 0);
    PASS();
    return 0;
}

static int test_version(void)
{
    const char *v;
    TEST("version");
    v = wc_version();
    ASSERT(v && strlen(v) > 0);
    ASSERT(strcmp(v, WC_VERSION) == 0);
    PASS();
    return 0;
}

static int test_errstr(void)
{
    const char *s;
    TEST("errstr");
    s = wc_errstr(WC_OK);
    ASSERT(s && strlen(s) > 0);
    s = wc_errstr(WC_ERROR);
    ASSERT(s && strlen(s) > 0);
    s = wc_errstr(WC_NOMEM);
    ASSERT(s && strlen(s) > 0);
    s = wc_errstr(9999);
    ASSERT(s && strlen(s) > 0);
    PASS();
    return 0;
}

static int test_build_info(void)
{
    const wc_build_config *cfg;

    TEST("build_info");
    cfg = wc_build_info();
    ASSERT(cfg != NULL);
    ASSERT(cfg->version_number == WC_VERSION_NUMBER);
    ASSERT(cfg->max_word == WC_MAX_WORD);
    ASSERT(cfg->min_init_cap == WC_MIN_INIT_CAP);
    ASSERT(cfg->min_block_sz == WC_MIN_BLOCK_SZ);
    ASSERT((cfg->stack_buffer != 0) == (WC_STACK_BUFFER != 0));
    PASS();
    return 0;
}

/* --- Stress / cursor tests -------------------------------------------- */

static int test_cursor_iteration(void)
{
    wc *w;
    wc_cursor c;
    size_t seen = 0;
    size_t sum = 0;

    TEST("cursor iterates all entries and sums to total");

    w = wc_open(0);
    ASSERT(w != NULL);

    ASSERT(wc_add(w, "alpha") == WC_OK);
    ASSERT(wc_add(w, "beta") == WC_OK);
    ASSERT(wc_add(w, "beta") == WC_OK);
    ASSERT(wc_add(w, "gamma") == WC_OK);
    ASSERT(wc_add(w, "gamma") == WC_OK);
    ASSERT(wc_add(w, "gamma") == WC_OK);

    wc_cursor_init(&c, w);
    for (;;) {
        const char *word = NULL;
        size_t cnt = 0;
        if (!wc_cursor_next(&c, &word, &cnt))
            break;
        ASSERT(word != NULL);
        ASSERT(cnt > 0);
        seen++;
        sum += cnt;
    }

    ASSERT(seen == wc_unique(w));
    ASSERT(sum == wc_total(w));
    ASSERT(invariant_cursor_sum_matches_total(w));

    wc_close(w);
    PASS();
    return 0;
}

/* --- OOM injection tests (glibc-specific) ------------------------------ */

#if defined(WC_TEST_OOM) && defined(__GLIBC__)

static int test_oom_open(void)
{
    wc *w;
    int i;
    TEST("OOM in wc_open");
    for (i = 1; i <= 10; i++) {
        oom_arm(i);
        w = wc_open(0);
        if (w)
            wc_close(w);
        oom_reset();
    }
    w = wc_open(0);
    ASSERT(w != NULL);
    wc_close(w);
    PASS();
    return 0;
}

static int test_oom_add(void)
{
    wc *w;
    int i, rc;
    TEST("OOM in wc_add");
    for (i = 1; i <= 20; i++) {
        w = wc_open(0);
        ASSERT(w != NULL);
        oom_arm(i);
        rc = wc_add(w, "testword");
        oom_reset();
        ASSERT(rc == WC_OK || rc == WC_NOMEM);
        ASSERT(invariant_cursor_sum_matches_total(w));
        wc_close(w);
    }
    PASS();
    return 0;
}

static int test_oom_scan(void)
{
    wc *w;
    const char *t = "the quick brown fox jumps over the lazy dog";
    int i, rc;
    TEST("OOM in wc_scan");
    for (i = 1; i <= 30; i++) {
        w = wc_open(0);
        ASSERT(w != NULL);
        oom_arm(i);
        rc = wc_scan(w, t, strlen(t));
        oom_reset();
        ASSERT(rc == WC_OK || rc == WC_NOMEM);
        ASSERT(invariant_cursor_sum_matches_total(w));
        wc_close(w);
    }
    PASS();
    return 0;
}

static int test_oom_results(void)
{
    wc *w;
    wc_word *r;
    size_t n;
    int i, rc;
    TEST("OOM in wc_results");
    for (i = 1; i <= 10; i++) {
        w = wc_open(0);
        ASSERT(wc_add(w, "hello") == WC_OK);
        ASSERT(wc_add(w, "world") == WC_OK);
        oom_arm(i);
        rc = wc_results(w, &r, &n);
        oom_reset();
        if (rc == WC_OK)
            wc_results_free(r);
        else
            ASSERT(rc == WC_NOMEM);
        ASSERT(invariant_cursor_sum_matches_total(w));
        wc_close(w);
    }
    PASS();
    return 0;
}

#endif /* WC_TEST_OOM && __GLIBC__ */

/* --- Fuzz harness (libFuzzer) ----------------------------------------- */

#if defined(WC_TEST_FUZZ) || defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    wc *w;
    wc_limits lim;
    size_t i = 0;

    if (!data || size == 0)
        return 0;

    memset(&lim, 0, sizeof lim);

    /* Options derived from the input prefix */
    {
        const uint8_t flags = data[i++];
        const size_t maxw = (flags & 1) ? 4u : 0u; /* 0 => default 64 */
        const int use_budget = (flags & 2) != 0;
        const int use_seed = (flags & 4) != 0;

        if (use_budget)
            lim.max_bytes = 4096u;
        if (use_seed) {
            if (size - i >= 4) {
                lim.hash_seed = (unsigned long)rd_u32(&data[i]);
                i += 4;
            } else {
                lim.hash_seed = 0x12345678UL;
            }
        }

        w = wc_open_ex(maxw, &lim);
        if (!w)
            return 0;
    }

    while (i < size) {
        const uint8_t op = data[i++];
        switch (op & 3u) {
            case 0: { /* wc_add */
                size_t n = 0;
                char word[65];
                if (i >= size)
                    break;
                n = (size_t)(data[i++] % 64u);
                if (size - i < n)
                    n = size - i;
                memcpy(word, &data[i], n);
                word[n] = '\0';
                (void)wc_add(w, word);
                i += n;
                break;
            }
            case 1: { /* wc_scan */
                size_t n = 0;
                if (i >= size)
                    break;
                n = (size_t)data[i++];
                if (size - i < n)
                    n = size - i;
                (void)wc_scan(w, (const char *)&data[i], n);
                i += n;
                break;
            }
            case 2: { /* wc_results */
                wc_word *r = NULL;
                size_t n = 0;
                int rc = wc_results(w, &r, &n);
                if (rc == WC_OK && r) {
                    /* basic sortedness check on adjacent entries */
                    size_t k;
                    for (k = 1; k < n; k++) {
                        if (r[k - 1].count < r[k].count) {
                            /* If this ever triggers, it's a correctness bug */
                            abort();
                        }
                    }
                    wc_results_free(r);
                }
                break;
            }
            case 3: { /* cursor invariant */
                if (!invariant_cursor_sum_matches_total(w))
                    abort();
                break;
            }
        }
    }

    if (!invariant_cursor_sum_matches_total(w))
        abort();

    wc_close(w);
    return 0;
}

#if defined(WC_TEST_FUZZ_STANDALONE)
int main(void)
{
    unsigned char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf, stdin);
    (void)LLVMFuzzerTestOneInput(buf, n);
    return 0;
}
#endif /* WC_TEST_FUZZ_STANDALONE */

#else /* normal unit test main */

/* --- Main (unit tests) ------------------------------------------------ */

int main(void)
{
    printf("\n=== Wordcount Tests (v%s) ===\n\n", wc_version());

    printf("Lifecycle / Limits:\n");
    test_open_close();
    test_close_null();
    test_max_word_clamp();
    test_open_ex_null_limits();
    test_open_ex_tiny_budget_fail();
    test_limits_budget_enforced();
    test_open_ex_tiny_static_fail();
    test_static_minimum_size_boundary();
    test_static_limits_enforced();
    test_static_with_tiny_max_bytes_fails();
    test_max_word_clamped_to_wc_max_word();

    printf("\nwc_add:\n");
    test_add_single();
    test_add_dup();
    test_add_empty();
    test_add_null();
    test_add_trunc();
    test_add_hash_collision_different_length();

    printf("\nwc_scan:\n");
    test_scan_simple();
    test_scan_empty();
    test_scan_null();
    test_scan_binary();
    test_scan_hash_collision_different_length();

    printf("\nwc_results:\n");
    test_results_sorted();
    test_results_empty();
    test_results_null();

    printf("\nQueries:\n");
    test_query_null();
    test_version();
    test_errstr();
    test_build_info();

    printf("\nCursor / Invariants:\n");
    test_cursor_iteration();

#if defined(WC_TEST_OOM) && defined(__GLIBC__)
    printf("\nOOM Injection (glibc-specific):\n");
    test_oom_open();
    test_oom_add();
    test_oom_scan();
    test_oom_results();
#else
    printf("\nOOM: skipped (build with -DWC_TEST_OOM on glibc)\n");
#endif

    printf("\n=== %d/%d passed", g_pass, g_run);
    if (g_fail)
        printf(", %d FAILED", g_fail);
    printf(" ===\n\n");

    return g_fail ? 1 : 0;
}

#endif /* fuzz vs unit tests */

#endif /* WC_NO_TEST_MAIN */
