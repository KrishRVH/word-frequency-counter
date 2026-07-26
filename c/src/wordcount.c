#if !defined(_WIN32)
// POSIX reserves this feature-test macro for applications.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

enum {
    DEFAULT_MAX_WORD = 64,
    ESTIMATED_BYTES_PER_UNIQUE_WORD = 32,
    INITIAL_CAPACITY = 16,
    MAX_WORD = 1024,
    MIN_WORD = 4
};

static const uint64_t FNV_OFFSET_BASIS = UINT64_C(0xcbf29ce484222325);
static const uint64_t FNV_PRIME = UINT64_C(0x100000001b3);
static const uint32_t CHECKSUM_OFFSET = UINT32_C(2166136261);
static const uint32_t CHECKSUM_PRIME = UINT32_C(16777619);

typedef struct {
    const char *path;
    size_t top;
    size_t max_word;
    size_t bench_runs;
    size_t bench_warmups;
    bool json;
} Options;

typedef struct {
    char *word;
    uint64_t count;
    uint64_t lex_prefix;
} Entry;

typedef struct {
    const unsigned char *word;
    size_t length;
    uint64_t count;
    uint64_t hash;
} Slot;

typedef struct {
    Slot *slots;
    size_t capacity;
    size_t length;
    size_t word_bytes;
    uint64_t total;
} Table;

typedef struct {
    Entry *entries;
    size_t unique;
    uint64_t total;
} Result;

typedef struct {
    const char *name;
    size_t *target;
} NumberOption;

typedef enum {
    SORT_PREFIX,
    SORT_COUNT
} SortKey;

// Counting

static bool is_letter(unsigned char byte)
{
    unsigned int lower = (unsigned int)byte | 0x20u;
    return lower - (unsigned int)'a' <= (unsigned int)'z' - (unsigned int)'a';
}

static size_t normalize_max_word(size_t value)
{
    if (value == 0u) {
        return DEFAULT_MAX_WORD;
    }
    if (value < MIN_WORD) {
        return MIN_WORD;
    }
    return value > MAX_WORD ? MAX_WORD : value;
}

static size_t capacity_for(size_t bytes)
{
    size_t expected = bytes / ESTIMATED_BYTES_PER_UNIQUE_WORD;
    size_t needed = (expected * 4u + 2u) / 3u;
    size_t capacity = INITIAL_CAPACITY;
    while (capacity < needed) {
        capacity *= 2u;
    }
    return capacity;
}

static int table_resize(Table *table, size_t capacity)
{
    Slot *slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        Slot slot = table->slots[i];
        if (slot.word == NULL) {
            continue;
        }

        size_t index = (size_t)slot.hash & (capacity - 1u);
        while (slots[index].word != NULL) {
            index = (index + 1u) & (capacity - 1u);
        }
        slots[index] = slot;
    }

    free(table->slots);
    table->slots = slots;
    table->capacity = capacity;
    return 0;
}

static bool
same_word(const Slot *slot, const unsigned char *word, size_t length)
{
    if (slot->length != length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if ((slot->word[i] | (unsigned char)0x20u) !=
            (word[i] | (unsigned char)0x20u)) {
            return false;
        }
    }
    return true;
}

static int
table_add(Table *table, const unsigned char *word, size_t length, uint64_t hash)
{
    size_t index = (size_t)hash & (table->capacity - 1u);
    while (table->slots[index].word != NULL) {
        Slot *slot = &table->slots[index];
        if (slot->hash == hash && same_word(slot, word, length)) {
            slot->count++;
            table->total++;
            return 0;
        }
        index = (index + 1u) & (table->capacity - 1u);
    }

    if (table->length + 1u > table->capacity - table->capacity / 4u) {
        if (table->capacity > SIZE_MAX / 2u ||
            table_resize(table, table->capacity * 2u) != 0) {
            return -1;
        }
        index = (size_t)hash & (table->capacity - 1u);
        while (table->slots[index].word != NULL) {
            index = (index + 1u) & (table->capacity - 1u);
        }
    }

    if (table->word_bytes > SIZE_MAX - length - 1u) {
        return -1;
    }
    table->slots[index] =
            (Slot){ .word = word, .length = length, .count = 1u, .hash = hash };
    table->length++;
    table->word_bytes += length + 1u;
    table->total++;
    return 0;
}

// Ordering

static int compare_words(const void *left_pointer, const void *right_pointer)
{
    const Entry *left = left_pointer;
    const Entry *right = right_pointer;
    return strcmp(left->word, right->word);
}

static void
radix_sort(Entry *entries, Entry *scratch, size_t length, SortKey sort_key)
{
    if (length < 2u) {
        return;
    }
    uint64_t first_key =
            sort_key == SORT_COUNT ? ~entries[0].count : entries[0].lex_prefix;
    uint64_t varying_bits = 0u;
    for (size_t i = 1u; i < length; i++) {
        uint64_t key = sort_key == SORT_COUNT ? ~entries[i].count
                                              : entries[i].lex_prefix;
        varying_bits |= first_key ^ key;
    }

    Entry *source = entries;
    Entry *target = scratch;
    for (unsigned int shift = 0u; varying_bits != 0u;
         shift += 8u, varying_bits >>= 8u) {
        size_t offsets[256] = { 0 };
        for (size_t i = 0u; i < length; i++) {
            uint64_t key = sort_key == SORT_COUNT ? ~source[i].count
                                                  : source[i].lex_prefix;
            offsets[(key >> shift) & UINT64_C(0xff)]++;
        }

        size_t total = 0u;
        for (size_t i = 0u; i < 256u; i++) {
            size_t count = offsets[i];
            offsets[i] = total;
            total += count;
        }
        for (size_t i = 0u; i < length; i++) {
            uint64_t key = sort_key == SORT_COUNT ? ~source[i].count
                                                  : source[i].lex_prefix;
            target[offsets[(key >> shift) & UINT64_C(0xff)]++] = source[i];
        }

        Entry *swap = source;
        source = target;
        target = swap;
    }
    if (source != entries) {
        for (size_t i = 0u; i < length; i++) {
            entries[i] = source[i];
        }
    }
}

static void sort_entries(Entry *entries, Entry *scratch, size_t length)
{
    // Stable prefix passes establish lexical order; qsort resolves words with
    // equal eight-byte prefixes. Stable count passes then preserve those ties.
    radix_sort(entries, scratch, length, SORT_PREFIX);
    for (size_t first = 0u; first < length;) {
        size_t last = first + 1u;
        while (last < length &&
               entries[last].lex_prefix == entries[first].lex_prefix) {
            last++;
        }
        if (last - first > 1u) {
            qsort(entries + first,
                  last - first,
                  sizeof(*entries),
                  compare_words);
        }
        first = last;
    }
    radix_sort(entries, scratch, length, SORT_COUNT);
}

static int count_words(const unsigned char *data,
                       size_t length,
                       size_t requested_max_word,
                       Result *result)
{
    *result = (Result){ 0 };
    size_t capacity = capacity_for(length);
    Table table = { 0 };
    if (table_resize(&table, capacity) != 0) {
        return -1;
    }

    size_t max_word = normalize_max_word(requested_max_word);
    size_t cursor = 0u;
    while (cursor < length) {
        while (cursor < length && !is_letter(data[cursor])) {
            cursor++;
        }
        if (cursor == length) {
            break;
        }

        size_t start = cursor;
        size_t stored = 0u;
        uint64_t hash = FNV_OFFSET_BASIS;
        while (cursor < length && is_letter(data[cursor])) {
            if (stored < max_word) {
                unsigned char lower = data[cursor] | (unsigned char)0x20u;
                hash = (hash ^ (uint64_t)lower) * FNV_PRIME;
                stored++;
            }
            cursor++;
        }

        if (table_add(&table, data + start, stored, hash) < 0) {
            free(table.slots);
            return -1;
        }
    }

    result->total = table.total;
    if (table.length == 0u) {
        free(table.slots);
        return 0;
    }
    if (table.length >
        (SIZE_MAX - table.word_bytes) / (2u * sizeof(*result->entries))) {
        free(table.slots);
        return -1;
    }
    // One allocation owns [entries][radix scratch][normalized words].
    result->entries = malloc(2u * table.length * sizeof(*result->entries) +
                             table.word_bytes);
    if (result->entries == NULL) {
        free(table.slots);
        return -1;
    }

    Entry *scratch = result->entries + table.length;
    size_t output = 0u;
    char *word = (char *)(scratch + table.length);
    for (size_t i = 0; i < table.capacity; i++) {
        Slot *slot = &table.slots[i];
        if (slot->word != NULL) {
            uint64_t lex_prefix = 0u;
            for (size_t byte = 0; byte < slot->length; byte++) {
                unsigned char lower = slot->word[byte] | (unsigned char)0x20u;
                word[byte] = (char)lower;
                if (byte < sizeof(lex_prefix)) {
                    lex_prefix = lex_prefix << 8u | (uint64_t)lower;
                }
            }
            for (size_t byte = slot->length; byte < sizeof(lex_prefix);
                 byte++) {
                lex_prefix <<= 8u;
            }
            word[slot->length] = '\0';
            result->entries[output++] = (Entry){ .word = word,
                                                 .count = slot->count,
                                                 .lex_prefix = lex_prefix };
            word += slot->length + 1u;
        }
    }

    result->unique = output;
    free(table.slots);
    sort_entries(result->entries, scratch, output);
    return 0;
}

// CLI and benchmark adapter

static void print_json(const Result *result, size_t top)
{
    size_t limit = result->unique < top ? result->unique : top;
    printf("{\"total\":%" PRIu64 ",\"unique\":%zu,\"top\":[",
           result->total,
           result->unique);
    for (size_t i = 0; i < limit; i++) {
        printf("%s{\"word\":\"%s\",\"count\":%" PRIu64 "}",
               i == 0u ? "" : ",",
               result->entries[i].word,
               result->entries[i].count);
    }
    puts("]}");
}

static void print_table(const Result *result, size_t top)
{
    size_t limit = result->unique < top ? result->unique : top;
    puts("count word");
    for (size_t i = 0; i < limit; i++) {
        printf("%" PRIu64 " %s\n",
               result->entries[i].count,
               result->entries[i].word);
    }
    printf("total %" PRIu64 "\nunique %zu\n", result->total, result->unique);
}

static uint32_t mix_byte(uint32_t checksum, unsigned char byte)
{
    return (checksum ^ (uint32_t)byte) * CHECKSUM_PRIME;
}

static uint32_t mix_integer(uint32_t checksum, uint64_t value, size_t width)
{
    for (size_t i = 0; i < width; i++) {
        checksum = mix_byte(checksum, (unsigned char)(value & UINT64_C(0xff)));
        value >>= 8u;
    }
    return checksum;
}

static uint32_t checksum_result(const Result *result, size_t top)
{
    size_t limit = result->unique < top ? result->unique : top;
    uint32_t checksum = CHECKSUM_OFFSET;
    checksum = mix_integer(checksum, result->total, 8u);
    checksum = mix_integer(checksum, (uint64_t)result->unique, 8u);

    for (size_t i = 0; i < limit; i++) {
        const unsigned char *word =
                (const unsigned char *)result->entries[i].word;
        while (*word != '\0') {
            checksum = mix_byte(checksum, *word++);
        }
        checksum = mix_integer(checksum, result->entries[i].count, 8u);
    }
    return checksum;
}

static double now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    (void)QueryPerformanceFrequency(&frequency);
    (void)QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec time;
    (void)clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec * 1000.0 + (double)time.tv_nsec / 1000000.0;
#endif
}

static int print_benchmark(const unsigned char *data,
                           size_t length,
                           const Options *options)
{
    for (size_t i = 0; i < options->bench_warmups; i++) {
        Result result = { 0 };
        if (count_words(data, length, options->max_word, &result) != 0) {
            return -1;
        }
        (void)checksum_result(&result, options->top);
        free(result.entries);
    }

    uint32_t checksum = CHECKSUM_OFFSET;
    double started = now_ms();
    for (size_t i = 0; i < options->bench_runs; i++) {
        Result result = { 0 };
        if (count_words(data, length, options->max_word, &result) != 0) {
            return -1;
        }
        uint32_t run_checksum = checksum_result(&result, options->top);
        free(result.entries);
        checksum = mix_integer(checksum, run_checksum, 4u);
    }

    double mean_ms = (now_ms() - started) / (double)options->bench_runs;
    printf("{\"mean_ms\":%.6f,\"checksum\":%" PRIu32 "}\n", mean_ms, checksum);
    return 0;
}

static int parse_size(const char *text, size_t *value)
{
    if (*text == '\0' || strspn(text, "0123456789") != strlen(text)) {
        return -1;
    }
    errno = 0;
    unsigned long long parsed = strtoull(text, NULL, 10);
    if (errno == ERANGE || parsed > SIZE_MAX) {
        return -1;
    }
    *value = (size_t)parsed;
    return 0;
}

static int parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){ .path = NULL,
                          .top = 10u,
                          .max_word = 1024u,
                          .bench_runs = 0u,
                          .bench_warmups = 0u,
                          .json = false };
    NumberOption numbers[] = {
        { "--top", &options->top },
        { "--max-word", &options->max_word },
        { "--bench-runs", &options->bench_runs },
        { "--bench-warmups", &options->bench_warmups },
    };

    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (strcmp(argument, "--json") == 0) {
            options->json = true;
            continue;
        }

        bool matched = false;
        for (size_t option = 0; option < sizeof(numbers) / sizeof(numbers[0]);
             option++) {
            size_t name_length = strlen(numbers[option].name);
            const char *value = NULL;
            if (strcmp(argument, numbers[option].name) == 0) {
                if (++i == argc) {
                    return -1;
                }
                value = argv[i];
            } else if (strncmp(argument, numbers[option].name, name_length) ==
                               0 &&
                       argument[name_length] == '=') {
                value = argument + name_length + 1u;
            }

            if (value != NULL) {
                if (parse_size(value, numbers[option].target) != 0) {
                    return -1;
                }
                matched = true;
                break;
            }
        }

        if (!matched) {
            if (argument[0] == '-' || options->path != NULL) {
                return -1;
            }
            options->path = argument;
        }
    }

    return options->path == NULL || options->top == 0u ? -1 : 0;
}

static int read_file(const char *path, unsigned char **data, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }

    *length = (size_t)file_size;
    *data = malloc(*length == 0u ? 1u : *length);
    if (*data == NULL) {
        (void)fclose(file);
        return -1;
    }

    bool read_ok = *length == 0u || fread(*data, 1u, *length, file) == *length;
    bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Options options;
    if (parse_options(argc, argv, &options) != 0) {
        (void)fputs("usage: ", stderr);
        (void)fputs(argv[0], stderr);
        (void)fputs(" [--json] [--top N] [--max-word N] <file>\n", stderr);
        return 2;
    }

    unsigned char *data = NULL;
    size_t length = 0u;
    if (read_file(options.path, &data, &length) != 0) {
        (void)fputs("wordcount_c: cannot read ", stderr);
        (void)fputs(options.path, stderr);
        (void)fputc('\n', stderr);
        return 1;
    }

    if (options.bench_runs > 0u) {
        int status = print_benchmark(data, length, &options);
        free(data);
        if (status != 0) {
            (void)fputs("wordcount_c: out of memory\n", stderr);
            return 1;
        }
        return 0;
    }

    Result result = { 0 };
    if (count_words(data, length, options.max_word, &result) != 0) {
        free(data);
        (void)fputs("wordcount_c: out of memory\n", stderr);
        return 1;
    }

    if (options.json) {
        print_json(&result, options.top);
    } else {
        print_table(&result, options.top);
    }

    free(result.entries);
    free(data);
    return 0;
}
