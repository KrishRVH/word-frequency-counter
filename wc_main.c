/*
** wc_main.c - Command-line interface
**
** Public domain.
**
** DESIGN
**
**   Uses memory-mapped I/O for zero-copy file access, enabling
**   processing of files larger than physical RAM. Platform-specific
**   code is isolated in the os_* functions.
**
**   Stdin is processed in streaming chunks to keep host memory usage
**   bounded. A small carry buffer is used to handle words that span
**   chunk boundaries. The streaming scanner implements the same word
**   model as wc_scan():
**
**     - ASCII letters A–Z/a–z are word characters.
**     - All other bytes are separators.
**     - Words are lowercased and truncated to the library’s max_word
**       before insertion, via wc_add().
**
**   The net effect is equivalent to running wc_scan() on the entire
**   stdin stream as a single contiguous buffer, but without needing to
**   materialize that buffer in memory.
**
**   On Windows, command-line arguments are obtained in UTF-16 via
**   GetCommandLineW/CommandLineToArgvW, converted to UTF-8, and file
**   paths are converted back to UTF-16 for CreateFileW. This allows
**   proper handling of non-ASCII filenames.
**
**   Error handling follows the goto-cleanup canonical pattern from
**   Linux kernel and SQLite style guides.
**
** Usage: wc [file ...]
**   Reads stdin if no files given. Top 10 to stdout, summary to stderr.
**
** Environment:
**   WC_MAX_BYTES  - Optional soft cap on internal heap usage for
**                   the wc object, in bytes (e.g. "8388608" for 8MB).
**                   If unset or invalid, defaults to no explicit cap.
*/
#ifndef WC_NO_HOSTED_MAIN

#include "wordcount.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOPN 10
#define STDIN_CHUNK 65536

/*
** EFBIG is POSIX, not C99. Provide fallback for exotic toolchains.
** ERANGE is C99-guaranteed and semantically closest.
*/
#ifndef EFBIG
#define EFBIG ERANGE
#endif

/* --- Parse environment-based limits ----------------------------------- */

static int parse_wc_limits_from_env(wc_limits *lim)
{
    const char *env;
    char *end;
    unsigned long long v;

    if (!lim)
        return 0;

    memset(lim, 0, sizeof *lim);

    env = getenv("WC_MAX_BYTES");
    if (!env || !*env)
        return 0; /* no limits */

    errno = 0;
    v = strtoull(env, &end, 10);
    if (errno != 0 || end == env || *end != '\0')
        return -1;

    if (v > (unsigned long long)SIZE_MAX)
        v = (unsigned long long)SIZE_MAX;

    lim->max_bytes = (size_t)v;
    /* init_cap/block_size left at 0 => library defaults */
    return 1;
}

/* --- Streaming scanner for stdin -------------------------------------- */

/*
** Small carry buffer to hold a partial word that spans chunk
** boundaries. We size it at WC_MAX_WORD+1 to always have room for a
** terminating NUL when calling wc_add().
**
** NOTE
**
**   - The library’s runtime max_word (w->maxw) is clamped to
**     WC_MAX_WORD, so storing up to WC_MAX_WORD characters here is
**     safe: wc_add() will only consider the first max_word bytes.
**   - We lowercase characters here so that stdin scanning matches the
**     case-folding semantics of wc_scan().
*/
typedef struct {
    char buf[WC_MAX_WORD + 1];
    size_t len;
} ScanState;

static int isalpha_ascii(unsigned char c)
{
    return ((unsigned)c | 32u) - 'a' < 26;
}

/*
** Scan a single stdin chunk.
**
** This is a streaming implementation of the same word model used by
** wc_scan():
**
**   - Reads bytes in order.
**   - Treats maximal runs of ASCII letters as words.
**   - Lowercases letters using the 'c | 32' trick.
**   - Truncates each word to at most WC_MAX_WORD bytes in the carry
**     buffer; wc_add() will further clamp to the instance’s max_word.
**
** Words that cross chunk boundaries are assembled incrementally in
** ScanState and flushed exactly once when the first non-letter
** separator after the run is seen (or on EOF).
*/
static int
scan_chunk_stream(wc *w, ScanState *st, const char *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    const unsigned char *end = p + len;

    while (p < end) {
        unsigned char c = *p++;

        if (isalpha_ascii(c)) {
            c = (unsigned char)(c | 32u);
            if (st->len < WC_MAX_WORD) {
                st->buf[st->len++] = (char)c;
            }
        } else {
            if (st->len > 0) {
                int rc;

                st->buf[st->len] = '\0';
                rc = wc_add(w, st->buf);
                if (rc != WC_OK)
                    return rc;
                st->len = 0;
            }
        }
    }

    return WC_OK;
}

/* --- Platform abstraction for memory-mapped files --------------------- */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

typedef struct {
    void *data;
    size_t size;
    HANDLE hFile;
    HANDLE hMap;
} MappedFile;

/*
** Map Win32 error to errno. Uses common mappings for portability.
*/
static void set_errno_from_win32(void)
{
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND ||
        err == ERROR_PATH_NOT_FOUND) {
        errno = ENOENT;
    } else if (err == ERROR_ACCESS_DENIED) {
        errno = EACCES;
    } else if (err == ERROR_NOT_ENOUGH_MEMORY ||
               err == ERROR_OUTOFMEMORY) {
        errno = ENOMEM;
    } else {
        errno = EIO;
    }
}

/* Helper: Convert UTF-8 path to UTF-16 for Windows APIs. */
static wchar_t *utf8_to_wide(const char *utf8)
{
    int n;
    wchar_t *wstr;

    if (!utf8)
        return NULL;

    n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0)
        return NULL;

    wstr = (wchar_t *)malloc((size_t)n * sizeof *wstr);
    if (!wstr)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8,
                            0,
                            utf8,
                            -1,
                            wstr,
                            n) <= 0) {
        free(wstr);
        return NULL;
    }

    return wstr;
}

/*
** On Windows, standard argv is ANSI (codepage dependent). Fetch the
** command line in UTF-16 and convert arguments to UTF-8.
*/
static char **win32_get_args_utf8(int *argc_out)
{
    int wargc = 0;
    wchar_t **wargv;
    char **uargv;
    int i;

    if (!argc_out)
        return NULL;

    wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv)
        return NULL;

    /* Allocate pointer array (+1 for NULL terminator). */
    uargv = (char **)malloc(
            ((size_t)wargc + 1u) * sizeof *uargv);
    if (!uargv) {
        LocalFree(wargv);
        return NULL;
    }

    for (i = 0; i < wargc; i++) {
        int n = WideCharToMultiByte(CP_UTF8,
                                    0,
                                    wargv[i],
                                    -1,
                                    NULL,
                                    0,
                                    NULL,
                                    NULL);
        if (n <= 0) {
            /* Fallback: treat as empty string on conversion
               failure. */
            n = 1;
        }

        uargv[i] = (char *)malloc((size_t)n);
        if (!uargv[i]) {
            int j;
            for (j = 0; j < i; j++)
                free(uargv[j]);
            free(uargv);
            LocalFree(wargv);
            return NULL;
        }

        if (WideCharToMultiByte(CP_UTF8,
                                0,
                                wargv[i],
                                -1,
                                uargv[i],
                                n,
                                NULL,
                                NULL) <= 0) {
            /* Treat as empty string on failure. */
            uargv[i][0] = '\0';
        }
    }

    uargv[wargc] = NULL;
    LocalFree(wargv);
    *argc_out = wargc;
    return uargv;
}

static void win32_free_args_utf8(char **argv, int argc)
{
    int i;

    if (!argv)
        return;

    for (i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);
}

/*
** Windows implementation using CreateFileMapping.
*/
static int os_map(MappedFile *mf, const char *path)
{
    LARGE_INTEGER sz;
    wchar_t *wpath;

    if (!mf || !path) {
        errno = EINVAL;
        return -1;
    }

    memset(mf, 0, sizeof *mf);
    mf->hFile = INVALID_HANDLE_VALUE;
    mf->hMap = NULL;

    wpath = utf8_to_wide(path);
    if (!wpath) {
        errno = ENOMEM;
        return -1;
    }

    mf->hFile = CreateFileW(wpath,
                            GENERIC_READ,
                            FILE_SHARE_READ,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    free(wpath);

    if (mf->hFile == INVALID_HANDLE_VALUE) {
        set_errno_from_win32();
        return -1;
    }

    if (!GetFileSizeEx(mf->hFile, &sz)) {
        set_errno_from_win32();
        CloseHandle(mf->hFile);
        mf->hFile = INVALID_HANDLE_VALUE;
        return -1;
    }

    if (sz.QuadPart == 0) {
        CloseHandle(mf->hFile);
        mf->hFile = INVALID_HANDLE_VALUE;
        return 0; /* empty file is ok */
    }

    /* Reject files larger than size_t can represent. */
    if (sz.QuadPart < 0 ||
        (unsigned long long)sz.QuadPart >
                (unsigned long long)SIZE_MAX) {
        CloseHandle(mf->hFile);
        mf->hFile = INVALID_HANDLE_VALUE;
        errno = EFBIG;
        return -1;
    }

    mf->hMap = CreateFileMappingW(mf->hFile,
                                  NULL,
                                  PAGE_READONLY,
                                  0,
                                  0,
                                  NULL);
    if (!mf->hMap) {
        set_errno_from_win32();
        CloseHandle(mf->hFile);
        mf->hFile = INVALID_HANDLE_VALUE;
        return -1;
    }

    mf->data = MapViewOfFile(mf->hMap,
                             FILE_MAP_READ,
                             0,
                             0,
                             0);
    if (!mf->data) {
        set_errno_from_win32();
        CloseHandle(mf->hMap);
        CloseHandle(mf->hFile);
        mf->hMap = NULL;
        mf->hFile = INVALID_HANDLE_VALUE;
        return -1;
    }

    mf->size = (size_t)sz.QuadPart;
    return 0;
}

static void os_unmap(MappedFile *mf)
{
    if (!mf)
        return;

    if (mf->data)
        UnmapViewOfFile(mf->data);
    if (mf->hMap)
        CloseHandle(mf->hMap);
    if (mf->hFile != INVALID_HANDLE_VALUE)
        CloseHandle(mf->hFile);

    memset(mf, 0, sizeof *mf);
    mf->hFile = INVALID_HANDLE_VALUE;
}

#else /* !_WIN32 */

/*
** POSIX implementation using mmap.
*/
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    void *data;
    size_t size;
    int fd;
} MappedFile;

static int os_map(MappedFile *mf, const char *path)
{
    struct stat st;
    int saved_errno;

    if (!mf || !path) {
        errno = EINVAL;
        return -1;
    }

    memset(mf, 0, sizeof *mf);
    mf->fd = -1;

    mf->fd = open(path, O_RDONLY);
    if (mf->fd < 0)
        return -1;

    if (fstat(mf->fd, &st) < 0) {
        saved_errno = errno;
        close(mf->fd);
        mf->fd = -1;
        errno = saved_errno;
        return -1;
    }

    if (st.st_size == 0) {
        close(mf->fd);
        mf->fd = -1;
        return 0; /* empty file is ok */
    }

    /* Reject files larger than size_t can represent (32-bit builds). */
    if ((off_t)(size_t)st.st_size != st.st_size) {
        close(mf->fd);
        mf->fd = -1;
        errno = EFBIG;
        return -1;
    }

    mf->data = mmap(NULL,
                    (size_t)st.st_size,
                    PROT_READ,
                    MAP_PRIVATE,
                    mf->fd,
                    0);
    if (mf->data == MAP_FAILED) {
        saved_errno = errno;
        mf->data = NULL;
        close(mf->fd);
        mf->fd = -1;
        errno = saved_errno;
        return -1;
    }

#ifdef MADV_SEQUENTIAL
    (void)madvise(mf->data,
                  (size_t)st.st_size,
                  MADV_SEQUENTIAL);
#endif

    mf->size = (size_t)st.st_size;
    return 0;
}

static void os_unmap(MappedFile *mf)
{
    if (!mf)
        return;

    if (mf->data && mf->size > 0)
        munmap(mf->data, mf->size);
    if (mf->fd >= 0)
        close(mf->fd);

    memset(mf, 0, sizeof *mf);
    mf->fd = -1;
}

#endif /* _WIN32 */

/* --- Processing -------------------------------------------------------- */

static int
process_mapped(wc *w, const char *data, size_t size, const char *name)
{
    int rc;

    rc = wc_scan(w, data, size);
    if (rc == WC_NOMEM) {
        (void)fprintf(stderr,
                      "wc: %s: %s\n",
                      name,
                      wc_errstr(rc));
        return -1;
    }
    if (rc != WC_OK) {
        (void)fprintf(stderr,
                      "wc: %s: %s\n",
                      name,
                      wc_errstr(rc));
        return -1;
    }

    return 0;
}

static int process_file(wc *w, const char *path)
{
    MappedFile mf;
    int rc = -1;

    memset(&mf, 0, sizeof mf);

    if (os_map(&mf, path) < 0) {
        (void)fprintf(stderr,
                      "wc: %s: %s\n",
                      path,
                      strerror(errno));
        goto cleanup;
    }

    if (mf.size == 0) {
        rc = 0;
        goto cleanup;
    }

    rc = process_mapped(w,
                        (const char *)mf.data,
                        mf.size,
                        path);

cleanup:
    os_unmap(&mf);
    return rc;
}

static int process_stdin(wc *w)
{
    char buf[STDIN_CHUNK];
    ScanState st;
    int rc;

    memset(&st, 0, sizeof st);

    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, stdin);
        if (n > 0) {
            rc = scan_chunk_stream(w, &st, buf, n);
            if (rc != WC_OK) {
                (void)fprintf(stderr,
                              "wc: <stdin>: %s\n",
                              wc_errstr(rc));
                return -1;
            }
        }
        if (n < sizeof buf) {
            if (ferror(stdin)) {
                (void)fprintf(stderr,
                              "wc: <stdin>: %s\n",
                              strerror(errno));
                return -1;
            }
            break; /* EOF */
        }
    }

    /* Flush any remaining partial word at EOF. */
    if (st.len > 0) {
        st.buf[st.len] = '\0';
        rc = wc_add(w, st.buf);
        if (rc != WC_OK) {
            (void)fprintf(stderr,
                          "wc: <stdin>: %s\n",
                          wc_errstr(rc));
            return -1;
        }
    }

    return 0;
}

/* --- Output ------------------------------------------------------------ */

static void output(const wc *w)
{
    wc_word *words = NULL;
    size_t len = 0;
    size_t i;
    int rc;

    rc = wc_results(w, &words, &len);
    if (rc == WC_NOMEM) {
        (void)fprintf(stderr, "wc: %s\n", wc_errstr(rc));
        goto cleanup;
    }
    if (rc != WC_OK) {
        (void)fprintf(stderr, "wc: %s\n", wc_errstr(rc));
        goto cleanup;
    }

    if (len == 0) {
        (void)fprintf(stderr, "No words found.\n");
        goto cleanup;
    }
    {
        size_t n = len < TOPN ? len : TOPN;

        printf("\n%7s  %-20s  %s\n", "Count", "Word", "%");
        printf("-------  --------------------  ------\n");

        for (i = 0; i < n; i++) {
            double pct = 100.0 *
                         (double)words[i].count /
                         (double)wc_total(w);
            printf("%7zu  %-20s  %5.2f\n",
                   words[i].count,
                   words[i].word,
                   pct);
        }
    }
    (void)fprintf(stderr,
                  "\nTotal: %zu  Unique: %zu\n",
                  wc_total(w),
                  wc_unique(w));

cleanup:
    wc_results_free(words);
}

/* --- Main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    wc *w = NULL;
    int i;
    int err = 0;
    int rc = 1;
    wc_limits lim;
    int have_limits;
#ifdef _WIN32
    int argc_win = 0;
    char **argv_win = NULL;
#endif

#ifdef _WIN32
    /* Re-acquire argv in UTF-8 for correct Unicode handling. */
    argv_win = win32_get_args_utf8(&argc_win);
    if (!argv_win) {
        (void)fprintf(stderr,
                      "wc: initialization failed (OOM)\n");
        return 1;
    }
    argc = argc_win;
    argv = argv_win;
#endif

    have_limits = parse_wc_limits_from_env(&lim);
    if (have_limits < 0) {
        (void)fprintf(stderr,
                      "wc: invalid WC_MAX_BYTES value "
                      "(must be integer)\n");
        goto cleanup;
    }

    if (have_limits > 0) {
        w = wc_open_ex(0, &lim);
    } else {
        w = wc_open(0);
    }

    if (!w) {
        (void)fprintf(stderr,
                      "wc: %s\n",
                      wc_errstr(WC_NOMEM));
        goto cleanup;
    }

    if (argc < 2) {
        if (process_stdin(w) < 0)
            err = 1;
    } else {
        for (i = 1; i < argc; i++) {
            if (process_file(w, argv[i]) < 0)
                err = 1;
        }
    }

    if (wc_unique(w) > 0)
        output(w);

    rc = err ? 1 : 0;

cleanup:
    wc_close(w);
#ifdef _WIN32
    if (argv_win)
        win32_free_args_utf8(argv_win, argc_win);
#endif
    return rc;
}

#endif /* WC_NO_HOSTED_MAIN */
