// wordcount -- report the most frequent words in a file.
//
// A word is a maximal run of ASCII letters. Words are compared
// case-insensitively and printed in lower case. A run longer than the
// effective --max-word value is represented by that prefix alone. Omitting
// --max-word uses 1024; explicitly passing 0 uses 64; other values clamp to
// [4, 1024]. Results are ordered by descending count, then ascending word.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#endif

enum {
    DEFAULT_TOP = 10,
    MIN_WORD_LIMIT = 4,
    ZERO_WORD_LIMIT = 64,
    MAX_WORD_LIMIT = 1024,
    DEFAULT_WORD_LIMIT = MAX_WORD_LIMIT,
    INITIAL_CAPACITY = 16
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

// Slot words borrow storage from the input buffer. Table capacity remains a
// power of two, and insertion keeps the table at or below three-quarter load.
typedef struct {
    const unsigned char *word;
    size_t length;
    uint64_t count;
    uint64_t hash;
} Slot;

typedef struct {
    Slot *slots;
    size_t capacity;
    size_t used;
    size_t word_bytes;
    uint64_t total;
} Table;

typedef struct {
    const char *word;
    uint64_t count;
} Entry;

typedef struct {
    Entry *entries;
    size_t unique;
    uint64_t total;
} Result;

static unsigned char ascii_lower(unsigned char byte)
{
    return (unsigned char)(byte | 0x20u);
}

static bool is_ascii_letter(unsigned char byte)
{
    unsigned char lower = ascii_lower(byte);
    return lower >= (unsigned char)'a' && lower <= (unsigned char)'z';
}

static size_t normalize_word_limit(size_t value)
{
    if (value == 0) {
        return ZERO_WORD_LIMIT;
    }
    if (value < MIN_WORD_LIMIT) {
        return MIN_WORD_LIMIT;
    }
    return value > MAX_WORD_LIMIT ? MAX_WORD_LIMIT : value;
}

static int table_init(Table *table)
{
    *table = (Table){ 0 };
    table->slots = calloc(INITIAL_CAPACITY, sizeof(*table->slots));
    if (table->slots == NULL) {
        return -1;
    }
    table->capacity = INITIAL_CAPACITY;
    return 0;
}

static void table_destroy(Table *table)
{
    free(table->slots);
    *table = (Table){ 0 };
}

static bool slot_matches(const Slot *slot,
                         const unsigned char *word,
                         size_t length,
                         uint64_t hash)
{
    if (slot->hash != hash || slot->length != length) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (ascii_lower(slot->word[i]) != ascii_lower(word[i])) {
            return false;
        }
    }
    return true;
}

// Return the matching slot, or the empty slot where the word belongs.
static inline Slot *table_probe(Table *table,
                                const unsigned char *word,
                                size_t length,
                                uint64_t hash)
{
    size_t mask = table->capacity - 1;
    size_t index = (size_t)hash & mask;

    for (;;) {
        Slot *slot = &table->slots[index];
        if (slot->word == NULL || slot_matches(slot, word, length, hash)) {
            return slot;
        }
        index = (index + 1) & mask;
    }
}

static int table_grow(Table *table)
{
    if (table->capacity > SIZE_MAX / 2) {
        return -1;
    }

    size_t capacity = table->capacity * 2;
    if (capacity > SIZE_MAX / sizeof(*table->slots)) {
        return -1;
    }

    Slot *slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }

    Table grown = { .slots = slots, .capacity = capacity };
    for (size_t i = 0; i < table->capacity; i++) {
        Slot slot = table->slots[i];
        if (slot.word != NULL) {
            *table_probe(&grown, slot.word, slot.length, slot.hash) = slot;
        }
    }

    free(table->slots);
    table->slots = slots;
    table->capacity = capacity;
    return 0;
}

static int
table_add(Table *table, const unsigned char *word, size_t length, uint64_t hash)
{
    Slot *slot = table_probe(table, word, length, hash);
    if (slot->word != NULL) {
        slot->count++;
        table->total++;
        return 0;
    }

    if (length == SIZE_MAX || table->word_bytes > SIZE_MAX - length - 1) {
        return -1;
    }

    if (table->used >= table->capacity - table->capacity / 4) {
        if (table_grow(table) != 0) {
            return -1;
        }
        slot = table_probe(table, word, length, hash);
    }

    *slot = (Slot){
        .word = word,
        .length = length,
        .count = 1,
        .hash = hash,
    };
    table->used++;
    table->word_bytes += length + 1;
    table->total++;
    return 0;
}

static int tally_words(Table *table,
                       const unsigned char *data,
                       size_t length,
                       size_t max_word)
{
    const unsigned char *cursor = data;
    const unsigned char *end = data + length;

    while (cursor != end) {
        while (cursor != end && !is_ascii_letter(*cursor)) {
            cursor++;
        }
        if (cursor == end) {
            break;
        }

        const unsigned char *word = cursor;
        size_t stored = 0;
        uint64_t hash = FNV_OFFSET_BASIS;

        // Consume the complete run, but retain only its configured prefix.
        while (cursor != end && is_ascii_letter(*cursor)) {
            if (stored < max_word) {
                hash = (hash ^ (uint64_t)ascii_lower(*cursor)) * FNV_PRIME;
                stored++;
            }
            cursor++;
        }

        if (table_add(table, word, stored, hash) != 0) {
            return -1;
        }
    }
    return 0;
}

static int compare_entries(const void *left_pointer, const void *right_pointer)
{
    const Entry *left = left_pointer;
    const Entry *right = right_pointer;

    if (left->count < right->count) {
        return 1;
    }
    if (left->count > right->count) {
        return -1;
    }
    return strcmp(left->word, right->word);
}

static void copy_normalized_word(char *destination, const Slot *slot)
{
    for (size_t i = 0; i < slot->length; i++) {
        destination[i] = (char)ascii_lower(slot->word[i]);
    }
    destination[slot->length] = '\0';
}

static int table_to_result(const Table *table, Result *result)
{
    Result output = { .total = table->total };
    if (table->used == 0) {
        *result = output;
        return 0;
    }

    if (table->used >
        (SIZE_MAX - table->word_bytes) / sizeof(*output.entries)) {
        return -1;
    }

    size_t allocation =
            table->used * sizeof(*output.entries) + table->word_bytes;
    Entry *entries = malloc(allocation);
    if (entries == NULL) {
        return -1;
    }

    // One allocation owns both the entries and their normalized words.
    Entry *entry = entries;
    char *word = (char *)(entries + table->used);
    for (size_t i = 0; i < table->capacity; i++) {
        const Slot *slot = &table->slots[i];
        if (slot->word == NULL) {
            continue;
        }

        copy_normalized_word(word, slot);
        *entry++ = (Entry){ .word = word, .count = slot->count };
        word += slot->length + 1;
    }

    qsort(entries, table->used, sizeof(*entries), compare_entries);
    output.entries = entries;
    output.unique = table->used;
    *result = output;
    return 0;
}

static int count_words(const unsigned char *data,
                       size_t length,
                       size_t requested_max_word,
                       Result *result)
{
    *result = (Result){ 0 };

    Table table;
    if (table_init(&table) != 0) {
        return -1;
    }

    int status = tally_words(
            &table, data, length, normalize_word_limit(requested_max_word));
    if (status == 0) {
        status = table_to_result(&table, result);
    }

    table_destroy(&table);
    return status;
}

static void result_destroy(Result *result)
{
    free(result->entries);
    *result = (Result){ 0 };
}

static size_t shown_entries(const Result *result, size_t top)
{
    return result->unique < top ? result->unique : top;
}

// Result words contain only lowercase ASCII letters, so their JSON strings
// require no escaping.
static void print_json(const Result *result, size_t top)
{
    size_t limit = shown_entries(result, top);

    printf("{\"total\":%" PRIu64 ",\"unique\":%zu,\"top\":[",
           result->total,
           result->unique);
    for (size_t i = 0; i < limit; i++) {
        printf("%s{\"word\":\"%s\",\"count\":%" PRIu64 "}",
               i == 0 ? "" : ",",
               result->entries[i].word,
               result->entries[i].count);
    }
    puts("]}");
}

static void print_table(const Result *result, size_t top)
{
    size_t limit = shown_entries(result, top);

    puts("count word");
    for (size_t i = 0; i < limit; i++) {
        printf("%" PRIu64 " %s\n",
               result->entries[i].count,
               result->entries[i].word);
    }
    printf("total %" PRIu64 "\nunique %zu\n", result->total, result->unique);
}

static int
print_report(const unsigned char *data, size_t length, const Options *options)
{
    Result result;
    if (count_words(data, length, options->max_word, &result) != 0) {
        return -1;
    }

    if (options->json) {
        print_json(&result, options->top);
    } else {
        print_table(&result, options->top);
    }

    result_destroy(&result);
    return 0;
}

static uint32_t mix_byte(uint32_t checksum, unsigned char byte)
{
    return (checksum ^ (uint32_t)byte) * CHECKSUM_PRIME;
}

static uint32_t mix_integer(uint32_t checksum, uint64_t value, size_t width)
{
    for (size_t i = 0; i < width; i++) {
        checksum = mix_byte(checksum, (unsigned char)(value & UINT64_C(0xff)));
        value >>= 8;
    }
    return checksum;
}

static uint32_t checksum_result(const Result *result, size_t top)
{
    uint32_t checksum = CHECKSUM_OFFSET;
    checksum = mix_integer(checksum, result->total, 8);
    checksum = mix_integer(checksum, (uint64_t)result->unique, 8);

    size_t limit = shown_entries(result, top);
    for (size_t i = 0; i < limit; i++) {
        const unsigned char *word =
                (const unsigned char *)result->entries[i].word;
        while (*word != '\0') {
            checksum = mix_byte(checksum, *word++);
        }
        checksum = mix_integer(checksum, result->entries[i].count, 8);
    }
    return checksum;
}

static double now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER frequency = { 0 };
    LARGE_INTEGER counter = { 0 };

    (void)QueryPerformanceFrequency(&frequency);
    (void)QueryPerformanceCounter(&counter);
    if (frequency.QuadPart == 0) {
        return 0.0;
    }
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec now = { 0 };

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
#endif
}

static int benchmark_once(const unsigned char *data,
                          size_t length,
                          const Options *options,
                          uint32_t *checksum)
{
    Result result;
    if (count_words(data, length, options->max_word, &result) != 0) {
        return -1;
    }

    *checksum = checksum_result(&result, options->top);
    result_destroy(&result);
    return 0;
}

static int print_benchmark(const unsigned char *data,
                           size_t length,
                           const Options *options)
{
    uint32_t run_checksum = 0;
    for (size_t i = 0; i < options->bench_warmups; i++) {
        if (benchmark_once(data, length, options, &run_checksum) != 0) {
            return -1;
        }
    }

    uint32_t checksum = CHECKSUM_OFFSET;
    double started = now_ms();
    for (size_t i = 0; i < options->bench_runs; i++) {
        if (benchmark_once(data, length, options, &run_checksum) != 0) {
            return -1;
        }
        checksum = mix_integer(checksum, run_checksum, 4);
    }

    double mean_ms = (now_ms() - started) / (double)options->bench_runs;
    printf("{\"mean_ms\":%.6f,\"checksum\":%" PRIu32 "}\n", mean_ms, checksum);
    return 0;
}

static int parse_size(const char *text, size_t *value)
{
    if (*text == '\0') {
        return -1;
    }

    size_t parsed = 0;
    while (*text != '\0') {
        unsigned char byte = (unsigned char)*text++;
        if (byte < (unsigned char)'0' || byte > (unsigned char)'9') {
            return -1;
        }

        size_t digit = (size_t)(byte - (unsigned char)'0');
        if (parsed > (SIZE_MAX - digit) / 10) {
            return -1;
        }
        parsed = parsed * 10 + digit;
    }

    *value = parsed;
    return 0;
}

// Accept --name=value and --name value. Return NULL for another option.
static const char *option_value(const char *name,
                                const char *argument,
                                int argc,
                                char **argv,
                                int *index)
{
    size_t length = strlen(name);

    if (strncmp(argument, name, length) != 0) {
        return NULL;
    }
    if (argument[length] == '=') {
        return argument + length + 1;
    }
    if (argument[length] != '\0' || *index + 1 == argc) {
        return NULL;
    }
    return argv[++*index];
}

static int parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){
        .top = DEFAULT_TOP,
        .max_word = DEFAULT_WORD_LIMIT,
    };

    const struct {
        const char *name;
        size_t *target;
    } number_options[] = {
        { "--top", &options->top },
        { "--max-word", &options->max_word },
        { "--bench-runs", &options->bench_runs },
        { "--bench-warmups", &options->bench_warmups },
    };

    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (argument == NULL) {
            return -1;
        }
        if (strcmp(argument, "--json") == 0) {
            options->json = true;
            continue;
        }

        bool matched = false;
        for (size_t option = 0;
             option < sizeof(number_options) / sizeof(*number_options);
             option++) {
            const char *value = option_value(
                    number_options[option].name, argument, argc, argv, &i);
            if (value == NULL) {
                continue;
            }
            if (parse_size(value, number_options[option].target) != 0) {
                return -1;
            }
            matched = true;
            break;
        }
        if (matched) {
            continue;
        }

        if (argument[0] == '-' || options->path != NULL) {
            return -1;
        }
        options->path = argument;
    }

    return options->path != NULL && options->top != 0 ? 0 : -1;
}

static int file_length(FILE *file, size_t *length)
{
#if defined(_WIN32)
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        return -1;
    }
    int64_t end = _ftelli64(file);
    if (end < 0 || (uintmax_t)end > (uintmax_t)SIZE_MAX ||
        _fseeki64(file, 0, SEEK_SET) != 0) {
        return -1;
    }
#else
    if (fseeko(file, 0, SEEK_END) != 0) {
        return -1;
    }
    off_t end = ftello(file);
    if (end < 0 || (uintmax_t)end > (uintmax_t)SIZE_MAX ||
        fseeko(file, 0, SEEK_SET) != 0) {
        return -1;
    }
#endif

    *length = (size_t)end;
    return 0;
}

static int read_file(const char *path, unsigned char **data, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    unsigned char *buffer = NULL;
    size_t size;
    if (file_length(file, &size) != 0) {
        goto fail;
    }

    buffer = malloc(size == 0 ? 1 : size);
    if (buffer == NULL) {
        goto fail;
    }

    if (size != 0 && fread(buffer, 1, size, file) != size) {
        goto fail;
    }

    if (fclose(file) != 0) {
        free(buffer);
        return -1;
    }

    *data = buffer;
    *length = size;
    return 0;

fail:
    free(buffer);
    (void)fclose(file);
    return -1;
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

    unsigned char *data;
    size_t length;
    if (read_file(options.path, &data, &length) != 0) {
        (void)fputs("wordcount_c: cannot read ", stderr);
        (void)fputs(options.path, stderr);
        (void)fputc('\n', stderr);
        return 1;
    }

    int status;
    if (options.bench_runs > 0) {
        status = print_benchmark(data, length, &options);
    } else {
        status = print_report(data, length, &options);
    }
    free(data);

    if (status != 0) {
        (void)fputs("wordcount_c: out of memory\n", stderr);
        return 1;
    }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        (void)fputs("wordcount_c: cannot write output\n", stderr);
        return 1;
    }
    return 0;
}
