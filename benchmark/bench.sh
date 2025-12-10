#!/bin/bash

# ==============================================================================
# 🚀 THE FOUR HORSEMEN: WORDCOUNT PERFORMANCE SHOWDOWN v3.3
#    - Fixed: wc.c compilation (added _GNU_SOURCE)
#    - Added: Compiler error logging
# ==============================================================================

# --- Ensure we run inside the benchmark directory ---
cd "$(dirname "$0")" || exit 1

# --- Configuration ---
RUNS=10
TARGET_SIZE_MB=500
CORPUS_DIR="bench_data"
FINAL_CORPUS="corpus_final.txt"

# --- Source Paths ---
SRC_ROOT=".."
LIB_SRC="$SRC_ROOT/wordcount.c"
LIB_MAIN="$SRC_ROOT/wc_main.c"

# --- Styling ---
BOLD='\033[1m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
PURPLE='\033[1;35m'
CYAN='\033[1;36m'
NC='\033[0m'
CHECK="${GREEN}✔${NC}"
CROSS="${RED}✘${NC}"

# ==============================================================================
# 0. Environment Check
# ==============================================================================
clear
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║               ULTIMATE WORDCOUNT BENCHMARK SUITE v3.3                ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════╝${NC}"

if [[ ! -f "$LIB_SRC" || ! -f "$LIB_MAIN" ]]; then
    echo -e "${RED}[ERROR]${NC} Library source files (wordcount.c, wc_main.c) not found in '$SRC_ROOT'."
    echo "Please ensure the project structure is correct."
    exit 1
fi

CPU_MODEL=$(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2 | xargs)
CORES=$(nproc)
echo -e "${BLUE}Hardware:${NC} $CPU_MODEL"
echo -e "${BLUE}Topology:${NC} $CORES Logical Cores"
echo -e "${BLUE}Payload:${NC}  ${TARGET_SIZE_MB}MB Text Corpus"
echo ""

# ==============================================================================
# 1. Source Fabrication
# ==============================================================================
echo -e "${BOLD}>> Stage 1: Fabricating Contenders${NC}"

# --- 1. Legacy (wc.c) ---
# Added _GNU_SOURCE to fix -std=c11 compilation errors (mmap/stat/open visibility)
cat << 'EOF' > wc.c
/* wc.c - Legacy Single-Threaded Implementation */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct E E;
struct E {
    E *next;
    size_t cnt, h;
    char w[];
};

static struct {
    E **tab;
    size_t cap, n, tot;
    char *mem;
    size_t len;
    int fd;
} G;

static void die(const char *s) { (void)fprintf(stderr, "wc: %s\n", s); exit(1); }
static inline int alpha(unsigned c) { return (c | 32) - 'a' < 26; }

static void grow(void) {
    size_t newcap = G.cap ? G.cap * 2 : 4096;
    E **newtab = calloc(newcap, sizeof(E *));
    if (!newtab) die("out of memory");
    for (size_t i = 0; i < G.cap; i++) {
        E *e = G.tab[i];
        while (e) {
            E *next = e->next;
            size_t idx = e->h & (newcap - 1);
            e->next = newtab[idx]; newtab[idx] = e; e = next;
        }
    }
    free(G.tab); G.tab = newtab; G.cap = newcap;
}

static void add(const char *w, size_t len, size_t h) {
    if (G.n >= G.cap * 7 / 10) grow();
    size_t idx = h & (G.cap - 1);
    for (E *e = G.tab[idx]; e; e = e->next) {
        if (e->h == h && !memcmp(e->w, w, len) && !e->w[len]) { e->cnt++; G.tot++; return; }
    }
    E *e = malloc(sizeof(E) + len + 1);
    if (!e) die("out of memory");
    memcpy(e->w, w, len); e->w[len] = '\0'; e->h = h; e->cnt = 1;
    e->next = G.tab[idx]; G.tab[idx] = e; G.n++; G.tot++;
}

static void scan(void) {
    const unsigned char *s = (const unsigned char *)G.mem;
    const unsigned char *end = s + G.len;
    char buf[256];
    while (s < end) {
        while (s < end && !alpha(*s)) s++;
        if (s >= end) break;
        size_t h = 5381u; size_t n = 0;
        while (s < end && alpha(*s)) {
            unsigned c = *s++ | 32;
            if (n < sizeof(buf) - 1) buf[n++] = c;
            h = ((h << 5) + h) + c;
        }
        add(buf, n, h);
    }
}

static int cmp(const void *a, const void *b) {
    const E *x = *(const E **)a; const E *y = *(const E **)b;
    if (x->cnt != y->cnt) return x->cnt < y->cnt ? 1 : -1;
    return strcmp(x->w, y->w);
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    G.fd = open(argv[1], O_RDONLY);
    struct stat st; fstat(G.fd, &st); G.len = st.st_size;
    G.mem = mmap(NULL, G.len, PROT_READ, MAP_PRIVATE, G.fd, 0);
    madvise(G.mem, G.len, MADV_SEQUENTIAL);
    scan();
    // Output simplified for benchmark (count only)
    fprintf(stderr, "Total: %zu Unique: %zu\n", G.tot, G.n);
    munmap(G.mem, G.len); close(G.fd);
    return 0;
}
EOF
echo -e "   $CHECK wc.c (Legacy v0) generated"

# --- 2. Parallel System (wc2.c) ---
cat << 'EOF' > wc2.c
/* wc2.c - Parallel Systems Implementation */
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_THREADS 128
#define INITIAL_CAP 4096
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL
typedef struct { char *data; size_t len; } Arena;
static void arena_init(Arena *a, size_t cap) { a->data = malloc(cap); a->len = 0; }
static char *arena_alloc(Arena *a, size_t size) { char *p = a->data + a->len; a->len += size; return p; }
typedef struct { char *key; size_t count; uint64_t hash; } Entry;
typedef struct { Entry *entries; size_t cap; size_t len; Arena arena; } Table;
static void table_init(Table *t, size_t cap, size_t arena_cap) { t->entries = calloc(cap, sizeof(Entry)); t->cap = cap; t->len = 0; arena_init(&t->arena, arena_cap); }
static void table_grow(Table *t) {
    size_t new_cap = t->cap * 2; Entry *new_ent = calloc(new_cap, sizeof(Entry));
    for(size_t i=0; i<t->cap; i++) {
        if(!t->entries[i].key) continue;
        size_t idx = t->entries[i].hash & (new_cap - 1);
        while(new_ent[idx].key) idx = (idx + 1) & (new_cap - 1);
        new_ent[idx] = t->entries[i];
    }
    free(t->entries); t->entries = new_ent; t->cap = new_cap;
}
static void table_add(Table *t, const char *word, size_t len, uint64_t hash) {
    if(t->len * 10 >= t->cap * 7) table_grow(t);
    size_t idx = hash & (t->cap - 1);
    while(1) {
        Entry *e = &t->entries[idx];
        if(!e->key) {
            char *s = arena_alloc(&t->arena, len+1); memcpy(s, word, len); s[len] = 0;
            e->key = s; e->count = 1; e->hash = hash; t->len++; return;
        }
        if(e->hash == hash && strcmp(e->key, word) == 0) { e->count++; return; }
        idx = (idx + 1) & (t->cap - 1);
    }
}
typedef struct { const char *data; size_t len; Table table; } Worker;
static inline bool is_alpha_u(unsigned c) { return ((c | 32) - 'a') < 26u; }
void *worker_entry(void *arg) {
    Worker *w = (Worker*)arg;
    const unsigned char *p = (const unsigned char*)w->data, *end = p + w->len; char word[64];
    table_init(&w->table, INITIAL_CAP, w->len / 4 + 4096);
    while(p < end) {
        while(p < end && !is_alpha_u(*p)) p++; if(p >= end) break;
        uint64_t h = FNV_OFFSET; size_t len = 0;
        while(p < end && is_alpha_u(*p)) { unsigned c = *p++ | 32; h = (h ^ c) * FNV_PRIME; if(len < 63) word[len++] = c; }
        word[len] = 0; table_add(&w->table, word, len, h);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if(argc < 2) return 1; int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
    char *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); madvise(data, st.st_size, MADV_SEQUENTIAL);
    int nthreads = sysconf(_SC_NPROCESSORS_ONLN); if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;
    pthread_t th[MAX_THREADS]; Worker w[MAX_THREADS]; size_t chunk = st.st_size / nthreads;
    for(int i=0; i<nthreads; i++) {
        size_t start = i * chunk, end = (i==nthreads-1) ? st.st_size : (i+1)*chunk;
        if(i > 0) while(start < end && is_alpha_u(data[start])) start++;
        if(i < nthreads-1) while(end < st.st_size && is_alpha_u(data[end])) end++;
        w[i].data = data + start; w[i].len = end - start; pthread_create(&th[i], NULL, worker_entry, &w[i]);
    }
    Table merged; table_init(&merged, INITIAL_CAP, st.st_size/10);
    for(int i=0; i<nthreads; i++) {
        pthread_join(th[i], NULL);
        for(size_t j=0; j<w[i].table.cap; j++) if(w[i].table.entries[j].key) {
             if(merged.len * 10 >= merged.cap * 7) table_grow(&merged);
             size_t idx = w[i].table.entries[j].hash & (merged.cap - 1);
             while(1) {
                 Entry *m = &merged.entries[idx];
                 if(!m->key) { *m = w[i].table.entries[j]; merged.len++; break; }
                 if(m->hash == w[i].table.entries[j].hash && strcmp(m->key, w[i].table.entries[j].key)==0) { m->count += w[i].table.entries[j].count; break; }
                 idx = (idx+1) & (merged.cap-1);
             }
        }
        free(w[i].table.entries);
    }
    size_t total = 0; for(size_t i=0; i<merged.cap; i++) if(merged.entries[i].key) total += merged.entries[i].count;
    fprintf(stderr, "Total: %zu Unique: %zu\n", total, merged.len); return 0;
}
EOF
echo -e "   $CHECK wc2.c (Parallel v2) generated"

# --- 3. AI Optimized (wcai.c) ---
cat << 'EOF' > wcai.c
/* wcai.c - Map-Reduce High-Performance Counter (Swiss+FxHash) */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_THREADS 256
#define INITIAL_CAP 4096
#define ARENA_BLOCK_SIZE (8 * 1024 * 1024)
static inline uint64_t hash_step(uint64_t h, uint64_t c) { return (h ^ c) * 0x517cc1b727220a95; }
typedef struct ArenaBlock { struct ArenaBlock *next; char data[]; } ArenaBlock;
typedef struct { ArenaBlock *head; char *ptr; char *end; } Arena;
static void arena_init(Arena *a) { a->head = malloc(sizeof(ArenaBlock) + ARENA_BLOCK_SIZE); a->head->next = NULL; a->ptr = a->head->data; a->end = a->ptr + ARENA_BLOCK_SIZE; }
static inline char *arena_alloc(Arena *a, size_t len) {
    if (a->ptr + len > a->end) {
        size_t size = (len > ARENA_BLOCK_SIZE) ? len + sizeof(ArenaBlock) : sizeof(ArenaBlock) + ARENA_BLOCK_SIZE;
        ArenaBlock *new_block = malloc(size); new_block->next = a->head; a->head = new_block; a->ptr = new_block->data; a->end = a->ptr + (size - sizeof(ArenaBlock));
    }
    char *ret = a->ptr; a->ptr += len; return ret;
}
typedef struct { uint64_t hash; char *key; uint32_t count; uint32_t len; } Entry;
typedef struct { Entry *slots; size_t mask; size_t count; size_t threshold; Arena arena; } Map;
static void map_init(Map *m) { m->mask = INITIAL_CAP - 1; m->slots = calloc(INITIAL_CAP, sizeof(Entry)); m->count = 0; m->threshold = (INITIAL_CAP * 3) / 4; arena_init(&m->arena); }
static void map_resize(Map *m) {
    size_t new_cap = (m->mask + 1) * 2; Entry *new_slots = calloc(new_cap, sizeof(Entry)); size_t new_mask = new_cap - 1;
    for (size_t i = 0; i <= m->mask; i++) {
        Entry *e = &m->slots[i]; if (!e->key) continue; size_t idx = e->hash & new_mask;
        while (new_slots[idx].key) idx = (idx + 1) & new_mask; new_slots[idx] = *e;
    }
    free(m->slots); m->slots = new_slots; m->mask = new_mask; m->threshold = (new_cap * 3) / 4;
}
static inline void map_put(Map *m, const char *word, size_t len, uint64_t h) {
    size_t idx = h & m->mask;
    while (1) {
        Entry *e = &m->slots[idx];
        if (!e->key) {
            if (m->count >= m->threshold) { map_resize(m); map_put(m, word, len, h); return; }
            char *s = arena_alloc(&m->arena, len + 1); memcpy(s, word, len); s[len] = 0;
            e->key = s; e->len = len; e->hash = h; e->count = 1; m->count++; return;
        }
        if (e->hash == h && e->len == len && memcmp(e->key, word, len) == 0) { e->count++; return; }
        idx = (idx + 1) & m->mask;
    }
}
typedef struct { const char *start; const char *end; Map map; } ThreadCtx;
static inline bool is_word(uint8_t c) { return ((c | 32) - 'a') < 26; }
void *worker(void *arg) {
    ThreadCtx *ctx = (ThreadCtx *)arg; map_init(&ctx->map); const char *p = ctx->start; const char *end = ctx->end;
    while (p < end) {
        while (p < end && !is_word(*p)) p++; if (p >= end) break;
        const char *wstart = p; uint64_t h = 0; while (p < end && is_word(*p)) { h = hash_step(h, *p++ | 32); }
        map_put(&ctx->map, wstart, p - wstart, h);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if (argc < 2) return 1; int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st); size_t fsize = st.st_size;
    char *data = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0); madvise(data, fsize, MADV_SEQUENTIAL);
    long nthreads = sysconf(_SC_NPROCESSORS_ONLN); if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;
    pthread_t threads[MAX_THREADS]; ThreadCtx ctx[MAX_THREADS]; size_t chunk = fsize / nthreads;
    for (int i = 0; i < nthreads; i++) {
        ctx[i].start = data + i * chunk; ctx[i].end = (i == nthreads - 1) ? data + fsize : data + (i + 1) * chunk;
        if (i > 0) while (ctx[i].start < ctx[i].end && is_word(*ctx[i].start)) ctx[i].start++;
        if (i < nthreads - 1) while (ctx[i].end < data + fsize && is_word(*ctx[i].end)) ctx[i].end++;
        pthread_create(&threads[i], NULL, worker, &ctx[i]);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(threads[i], NULL);
    Map *global = &ctx[0].map;
    for (int i = 1; i < nthreads; i++) {
        Map *src = &ctx[i].map;
        for (size_t j = 0; j <= src->mask; j++) {
            Entry *e = &src->slots[j]; if (!e->key) continue;
            size_t idx = e->hash & global->mask;
            while (1) {
                Entry *g = &global->slots[idx];
                if (!g->key) { if (global->count >= global->threshold) { map_resize(global); idx = e->hash & global->mask; continue; } *g = *e; global->count++; break; }
                if (g->hash == e->hash && g->len == e->len && memcmp(g->key, e->key, e->len) == 0) { g->count += e->count; break; }
                idx = (idx + 1) & global->mask;
            }
        }
    }
    size_t total = 0; for(size_t i=0; i<=global->mask; i++) if(global->slots[i].key) total += global->slots[i].count;
    fprintf(stderr, "Total: %zu Unique: %zu\n", total, global->count); return 0;
}
EOF
echo -e "   $CHECK wcai.c (AI Optimized) generated"

# ==============================================================================
# 2. Data Preparation
# ==============================================================================
echo -e "${BOLD}>> Stage 2: Building Corpus (${TARGET_SIZE_MB}MB)${NC}"
mkdir -p "$CORPUS_DIR"
BOOKS=(
    "https://www.gutenberg.org/files/1342/1342-0.txt"
    "https://www.gutenberg.org/files/84/84-0.txt"
    "https://www.gutenberg.org/files/2701/2701-0.txt"
)

# Download logic
for url in "${BOOKS[@]}"; do
    file="$CORPUS_DIR/$(basename "$url")"
    if [ ! -f "$file" ]; then curl -s -L -f -o "$file" "$url"; fi
done

# Scaling logic
CUR_SIZE=$(stat -c%s "$FINAL_CORPUS" 2>/dev/null || echo 0)
REQ_SIZE=$((TARGET_SIZE_MB * 1024 * 1024))

if [ "$CUR_SIZE" -lt "$REQ_SIZE" ]; then
    echo -n "   Concatenating..."
    rm -f "$FINAL_CORPUS"
    cat "$CORPUS_DIR"/*.txt > temp.txt
    while [ $(stat -c%s "$FINAL_CORPUS" 2>/dev/null || echo 0) -lt "$REQ_SIZE" ]; do 
        cat temp.txt >> "$FINAL_CORPUS"
        echo -n "."
    done
    rm temp.txt
    echo -e " Done."
else
    echo -e "   $CHECK Corpus ready ($((CUR_SIZE/1024/1024)) MB)"
fi
ACTUAL_MB=$(du -m "$FINAL_CORPUS" | cut -f1)

# ==============================================================================
# 3. Compilation
# ==============================================================================
echo -e "${BOLD}>> Stage 3: Compilation (-O3 -march=native)${NC}"

compile() {
    local cmd=$1
    local name=$2
    if $cmd 2> /tmp/build_err.log; then
        echo -e "   $CHECK $name"
    else
        echo -e "   $CROSS $name Failed"
        echo "   Compiler Output:"
        cat /tmp/build_err.log
        exit 1
    fi
}

# wc.c needs _GNU_SOURCE for mmap/madvise, handled in file content now.
compile "gcc -O3 -std=c11 -o bin_legacy wc.c" "Legacy (v0)"
compile "gcc -O3 -std=c99 -DWC_OMIT_ASSERT -I$SRC_ROOT -o bin_lib $LIB_SRC $LIB_MAIN" "Library (vF)"
compile "gcc -O3 -pthread -march=native -o bin_par wc2.c -lpthread" "Parallel (v2)"
compile "gcc -O3 -pthread -march=native -o bin_ai wcai.c -lpthread" "AI Optimized"

# ==============================================================================
# 4. Benchmarking
# ==============================================================================
echo -e "${BOLD}>> Stage 4: Execution ($RUNS runs)${NC}"

declare -a T_V0 T_LIB T_PAR T_AI

run_bench() {
    local bin=$1
    local name=$2
    local tmp="/tmp/wc_bench.tmp"
    local arr_name=$3
    
    printf "   %-16s " "$name"
    for ((i=1; i<=RUNS; i++)); do
        /usr/bin/time -f "%e" -o "$tmp" ./$bin "$FINAL_CORPUS" >/dev/null 2>&1
        local res=$(cat "$tmp")
        if [ -z "$res" ]; then res="FAIL"; fi
        eval "$arr_name+=($res)"
        printf "${BLUE}▪${NC}"
    done
    echo ""
}

run_bench "bin_legacy" "Legacy (v0)" "T_V0"
run_bench "bin_lib"    "Library (vF)" "T_LIB"
run_bench "bin_par"    "Parallel (v2)" "T_PAR"
run_bench "bin_ai"     "AI Optimized" "T_AI"

# ==============================================================================
# 5. Analysis
# ==============================================================================
echo -e "\n${BOLD}>> Stage 5: The Stats${NC}"

# Math Helpers
get_mean() {
    local arr=("${!1}")
    echo "${arr[@]}" | awk '{sum=0; cnt=0; for(i=1;i<=NF;i++) if($i!="FAIL"){sum+=$i; cnt++} if(cnt==0) print "0"; else printf "%.4f", sum/cnt}'
}

get_sd() {
    local arr=("${!1}")
    local mean=$2
    echo "${arr[@]}" | awk -v m=$mean '{sumsq=0; cnt=0; for(i=1;i<=NF;i++) if($i!="FAIL"){sumsq+=($i-m)^2; cnt++} if(cnt==0) print "0"; else printf "%.4f", sqrt(sumsq/cnt)}'
}

MEAN_V0=$(get_mean T_V0[@]); SD_V0=$(get_sd T_V0[@] $MEAN_V0)
MEAN_LIB=$(get_mean T_LIB[@]); SD_LIB=$(get_sd T_LIB[@] $MEAN_LIB)
MEAN_PAR=$(get_mean T_PAR[@]); SD_PAR=$(get_sd T_PAR[@] $MEAN_PAR)
MEAN_AI=$(get_mean T_AI[@]); SD_AI=$(get_sd T_AI[@] $MEAN_AI)

calc_gbps() {
    local t=$1
    if (( $(echo "$t > 0" | bc -l) )); then echo "scale=2; ($ACTUAL_MB / 1024) / $t" | bc; else echo "0"; fi
}
GBPS_V0=$(calc_gbps $MEAN_V0); GBPS_LIB=$(calc_gbps $MEAN_LIB)
GBPS_PAR=$(calc_gbps $MEAN_PAR); GBPS_AI=$(calc_gbps $MEAN_AI)

calc_x() {
    local base=$1; local new=$2
    if (( $(echo "$new > 0" | bc -l) )); then echo "scale=2; $base / $new" | bc; else echo "0"; fi
}
X_LIB=$(calc_x $MEAN_V0 $MEAN_LIB)
X_PAR=$(calc_x $MEAN_V0 $MEAN_PAR)
X_AI=$(calc_x $MEAN_V0 $MEAN_AI)
X_PAR_VS_LIB=$(calc_x $MEAN_LIB $MEAN_PAR)
X_AI_VS_PAR=$(calc_x $MEAN_PAR $MEAN_AI)

# --- Table Render ---
echo -e ""
printf "${BOLD}%-16s | %-10s | %-10s | %-12s | %-10s | %-10s${NC}\n" "Implementation" "Time (s)" "StdDev" "Throughput" "vs Legacy" "vs Library"
echo -e "----------------------------------------------------------------------------------"
printf "%-16s | %-10s | %-10s | %-12s | %-10s | %-10s\n" "Legacy (v0)" "${MEAN_V0}s" "±$SD_V0" "${GBPS_V0} GB/s" "1.00x" "-"
printf "${BLUE}%-16s${NC} | %-10s | %-10s | %-12s | %-10s | %-10s\n" "Library (vF)" "${MEAN_LIB}s" "±$SD_LIB" "${GBPS_LIB} GB/s" "${X_LIB}x" "1.00x"
printf "${YELLOW}%-16s${NC} | %-10s | %-10s | %-12s | %-10s | %-10s\n" "Parallel (v2)" "${MEAN_PAR}s" "±$SD_PAR" "${GBPS_PAR} GB/s" "${X_PAR}x" "${X_PAR_VS_LIB}x"
printf "${PURPLE}%-16s${NC} | ${GREEN}%-10s${NC} | %-10s | ${GREEN}%-12s${NC} | ${BOLD}%-10s${NC} | ${BOLD}%-10s${NC}\n" "AI Optimized" "${MEAN_AI}s" "±$SD_AI" "${GBPS_AI} GB/s" "${X_AI}x" "$(calc_x $MEAN_LIB $MEAN_AI)x"
echo -e "----------------------------------------------------------------------------------"

echo -e ""
echo -e "${BOLD}Verdict:${NC}"
echo -e "1. ${BLUE}Library${NC} is the single-threaded baseline."
echo -e "2. ${YELLOW}Parallel (v2)${NC} shows raw core scaling."
echo -e "3. ${PURPLE}AI Optimized${NC} is ${BOLD}${X_AI_VS_PAR}x${NC} faster than Parallel v2."
echo ""
