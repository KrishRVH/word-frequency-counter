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

int wf_count_bytes(const unsigned char *data,
                   size_t len,
                   size_t max_word,
                   WfResult *result);
void wf_result_free(WfResult *result);
void wf_result_sort(WfResult *result);

#endif
