#include "wordfreq.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEFAULT_MAX_WORD = 64,
    INITIAL_CAPACITY = 1024,
    MAX_WORD = 1024,
    MIN_WORD = 4
};

typedef struct {
    char *word;
    size_t len;
    uint64_t count;
    uint64_t hash;
} Slot;

typedef struct {
    Slot *slots;
    size_t cap;
    size_t len;
    uint64_t total;
} Table;

static bool is_letter(unsigned char byte)
{
    return (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
           (byte >= (unsigned char)'a' && byte <= (unsigned char)'z');
}

static unsigned char lower_ascii(unsigned char byte)
{
    return (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z')
                   ? (unsigned char)(byte + 32u)
                   : byte;
}

static size_t normalize_max_word(size_t max_word)
{
    if (max_word == 0u) {
        return DEFAULT_MAX_WORD;
    }
    if (max_word < MIN_WORD) {
        return MIN_WORD;
    }
    if (max_word > MAX_WORD) {
        return MAX_WORD;
    }
    return max_word;
}

static uint64_t hash_word(const unsigned char *bytes, size_t len)
{
    uint64_t hash = 14695981039346656037ull;

    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)lower_ascii(bytes[i]);
        hash *= 1099511628211ull;
    }

    return hash;
}

static bool same_word(const Slot *slot, const unsigned char *bytes, size_t len)
{
    if (slot->len != len) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)slot->word[i] != lower_ascii(bytes[i])) {
            return false;
        }
    }

    return true;
}

static int table_grow(Table *table)
{
    size_t next_cap = table->cap == 0 ? INITIAL_CAPACITY : table->cap * 2u;
    Slot *next = calloc(next_cap, sizeof(*next));

    if (next == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table->cap; i++) {
        Slot slot = table->slots[i];

        if (slot.word == NULL) {
            continue;
        }

        size_t index = (size_t)slot.hash & (next_cap - 1u);
        while (next[index].word != NULL) {
            index = (index + 1u) & (next_cap - 1u);
        }
        next[index] = slot;
    }

    free(table->slots);
    table->slots = next;
    table->cap = next_cap;
    return 0;
}

static int table_insert(Table *table, const unsigned char *bytes, size_t len)
{
    uint64_t hash = hash_word(bytes, len);

    if (table->cap == 0 || (table->len + 1u) * 10u >= table->cap * 7u) {
        if (table_grow(table) != 0) {
            return -1;
        }
    }

    size_t index = (size_t)hash & (table->cap - 1u);
    while (table->slots[index].word != NULL) {
        Slot *slot = &table->slots[index];

        if (slot->hash == hash && same_word(slot, bytes, len)) {
            slot->count++;
            table->total++;
            return 0;
        }

        index = (index + 1u) & (table->cap - 1u);
    }

    char *word = malloc(len + 1u);
    if (word == NULL) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        word[i] = (char)lower_ascii(bytes[i]);
    }
    word[len] = '\0';

    table->slots[index] =
            (Slot){ .word = word, .len = len, .count = 1u, .hash = hash };
    table->len++;
    table->total++;
    return 0;
}

static void table_free(Table *table)
{
    for (size_t i = 0; i < table->cap; i++) {
        free(table->slots[i].word);
    }
    free(table->slots);
    *table = (Table){ 0 };
}

static int finish(Table *table, WfResult *result)
{
    size_t unique = table->len;
    uint64_t total = table->total;
    WfEntry *entries = NULL;

    if (unique == 0) {
        result->unique = 0;
        result->total = total;
        free(table->slots);
        *table = (Table){ 0 };
        return 0;
    }

    entries = calloc(unique, sizeof(*entries));
    if (entries == NULL) {
        return -1;
    }

    size_t out = 0;
    for (size_t i = 0; i < table->cap && out < unique; i++) {
        Slot *slot = &table->slots[i];

        if (slot->word == NULL) {
            continue;
        }

        entries[out++] = (WfEntry){ .word = slot->word, .count = slot->count };
    }

    free(table->slots);
    *table = (Table){ 0 };
    result->entries = entries;
    result->unique = unique;
    result->total = total;
    wf_result_sort(result);
    return 0;
}

int wf_count_bytes(const unsigned char *data,
                   size_t len,
                   size_t max_word,
                   WfResult *result)
{
    Table table = { 0 };
    size_t cursor = 0;

    *result = (WfResult){ 0 };
    max_word = normalize_max_word(max_word);

    while (cursor < len) {
        while (cursor < len && !is_letter(data[cursor])) {
            cursor++;
        }

        size_t start = cursor;
        while (cursor < len && is_letter(data[cursor])) {
            cursor++;
        }

        size_t word_len = cursor - start;
        size_t stored_len = word_len < max_word ? word_len : max_word;

        if (stored_len > 0 &&
            table_insert(&table, data + start, stored_len) != 0) {
            table_free(&table);
            return -1;
        }
    }

    if (finish(&table, result) != 0) {
        table_free(&table);
        wf_result_free(result);
        return -1;
    }

    return 0;
}

static int compare_entries(const void *left, const void *right)
{
    const WfEntry *a = left;
    const WfEntry *b = right;

    if (a->count < b->count) {
        return 1;
    }
    if (a->count > b->count) {
        return -1;
    }
    return strcmp(a->word, b->word);
}

void wf_result_sort(WfResult *result)
{
    qsort(result->entries,
          result->unique,
          sizeof(*result->entries),
          compare_entries);
}

void wf_result_free(WfResult *result)
{
    for (size_t i = 0; i < result->unique; i++) {
        free(result->entries[i].word);
    }
    free(result->entries);
    *result = (WfResult){ 0 };
}
