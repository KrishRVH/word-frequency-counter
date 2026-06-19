#include "wordfreq.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *path;
    size_t top;
    size_t max_word;
    bool json;
} Options;

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s [--json] [--top N] [--max-word N] <file>\n",
                  program);
}

static int parse_size(const char *text, size_t *out)
{
    char *end = NULL;
    errno = 0;

    if (text[0] == '\0') {
        return -1;
    }
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }
    }

    unsigned long long value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == NULL || *end != '\0' || value > SIZE_MAX) {
        return -1;
    }

    *out = (size_t)value;
    return 0;
}

static int parse_separate_size(int argc, char **argv, int *index, size_t *out)
{
    *index += 1;
    if (*index >= argc) {
        return -1;
    }
    return parse_size(argv[*index], out);
}

static int parse_prefixed_size(const char *arg, size_t *out)
{
    if (strncmp(arg, "--top=", 6u) == 0) {
        return parse_size(arg + 6u, out);
    }
    if (strncmp(arg, "--max-word=", 11u) == 0) {
        return parse_size(arg + 11u, out);
    }
    return 1;
}

static int parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){
        .path = NULL, .top = 10u, .max_word = 1024u, .json = false
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            options->json = true;
        } else if (strcmp(argv[i], "--top") == 0 ||
                   strcmp(argv[i], "--max-word") == 0) {
            size_t *target = strcmp(argv[i], "--top") == 0 ? &options->top
                                                           : &options->max_word;
            if (parse_separate_size(argc, argv, &i, target) != 0) {
                return -1;
            }
        } else if (strncmp(argv[i], "--top=", 6u) == 0 ||
                   strncmp(argv[i], "--max-word=", 11u) == 0) {
            size_t *target = strncmp(argv[i], "--top=", 6u) == 0
                                     ? &options->top
                                     : &options->max_word;
            if (parse_prefixed_size(argv[i], target) != 0) {
                return -1;
            }
        } else if (options->path == NULL && argv[i][0] != '-') {
            options->path = argv[i];
        } else {
            return -1;
        }
    }

    return options->path == NULL || options->top == 0u ? -1 : 0;
}

static int read_file(const char *path, unsigned char **data, size_t *len)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return -1;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }

    *len = (size_t)size;
    *data = malloc(*len == 0 ? 1u : *len);
    if (*data == NULL) {
        (void)fclose(file);
        return -1;
    }

    if (*len > 0 && fread(*data, 1u, *len, file) != *len) {
        free(*data);
        (void)fclose(file);
        return -1;
    }

    (void)fclose(file);
    return 0;
}

static void print_json(const WfResult *result, size_t top)
{
    size_t limit = result->unique < top ? result->unique : top;

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

static void print_table(const WfResult *result, size_t top)
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

int main(int argc, char **argv)
{
    Options options;
    unsigned char *data = NULL;
    size_t len = 0;
    WfResult result = { 0 };

    if (parse_options(argc, argv, &options) != 0) {
        usage(argv[0]);
        return 2;
    }

    if (read_file(options.path, &data, &len) != 0) {
        (void)fprintf(stderr,
                      "wordcount_c: cannot read %s: %s\n",
                      options.path,
                      strerror(errno));
        return 1;
    }

    if (wf_count_bytes(data, len, options.max_word, &result) != 0) {
        (void)fprintf(stderr, "wordcount_c: out of memory\n");
        free(data);
        return 1;
    }

    if (options.json) {
        print_json(&result, options.top);
    } else {
        print_table(&result, options.top);
    }

    wf_result_free(&result);
    free(data);
    return 0;
}
