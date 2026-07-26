#ifndef WORDFREQ_H
#define WORDFREQ_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *word;
    uint64_t count;
} WfEntry;

typedef struct {
    WfEntry *entries;
    size_t unique;
    uint64_t total;
} WfResult;

/*
 * Count case-insensitive ASCII words in data. A zero max_word selects the
 * default; other values are clamped to the supported range. On success, result
 * owns its entries and must be released with wf_result_free().
 */
int wf_count_bytes(const unsigned char *data,
                   size_t len,
                   size_t max_word,
                   WfResult *result);

/* Release a successfully initialized result and reset it to zero. */
void wf_result_free(WfResult *result);

/* Sort entries by descending count, then ascending word. */
void wf_result_sort(WfResult *result);

#endif
