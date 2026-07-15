/*-----------------------------------------------------------------------------
 * test-experiment.c — unified experiment driver for the thesis.
 *
 * Compares search (and optionally insert) performance of:
 *
 *   array       plain sorted array + classic (branchy) binary search
 *   array-bl    plain sorted array + branchless binary search
 *   array-eyt   plain array in Eytzinger layout + branchless search
 *   skiplist    classic probabilistic skip list (node per key)
 *   csl         block skip list, sorted blocks
 *   csl-eyt     block skip list, Eytzinger-laid-out blocks
 *
 * All block-based structures are swept over a list of block capacities at
 * RUNTIME (no recompilation needed).  Queries are generated per the
 * supervisor's specification: uniformly random keys drawn between the
 * minimum and maximum of the data set, with a configurable fraction of
 * hits (default 50% present / 50% absent).  Every structure answers the
 * exact same query sequence and the number of hits is cross-checked —
 * results must be identical across structures or the run FAILS.
 *
 * Results are appended as CSV rows (one row per repetition) to a file whose
 * name encodes all experiment parameters, e.g.:
 *
 *   results/exp_search_uniform_n1000000_q500000_hit50_seed42.csv
 *
 * Usage:
 *   experiment [options]
 *     -m mode      search | insert            (default search)
 *     -n N         number of keys             (default 1000000)
 *     -q Q         number of queries          (default 500000)
 *     -b caps      comma-separated block caps (default 16,32,64,128,256,512,1024,2048)
 *     -r reps      repetitions per config     (default 3)
 *     -s seed      RNG seed                   (default 42)
 *     -H pct       hit percentage 0..100      (default 50)
 *     -d dist      uniform | dense            (default uniform)
 *     -f file      read keys from file (whitespace-separated ints; overrides -n/-d)
 *     -o dir       output directory           (default results)
 *
 * Build: gcc -O3 -msse2 -o experiment cskiplist.c skiplist.c test-experiment.c -lpsapi
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cskiplist.h"
#include "skiplist.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define MKDIR(d) _mkdir(d)
static LARGE_INTEGER qpc_freq;
static void timer_init(void) { QueryPerformanceFrequency(&qpc_freq); }
static double now_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)qpc_freq.QuadPart * 1e6;
}
#else
#include <time.h>
#include <sys/stat.h>
#define MKDIR(d) mkdir(d, 0755)
static void timer_init(void) {}
static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
#endif

/* ---------------- deterministic RNG (xorshift32) ---------------- */

static uint32_t g_rng;
static uint32_t xrand(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rng = x ? x : 0x9E3779B9u;
    return g_rng;
}
static void shuffle(int* a, int n) {
    for (int i = n - 1; i > 0; --i) {
        int j = (int)(xrand() % (uint32_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* ---------------- plain-array baselines (kv pairs) ---------------- */

/* classic branchy binary search (K&R style) */
static int arr_search_branchy(const csl_kv* a, int n, int key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (a[mid].key < key) lo = mid + 1;
        else if (a[mid].key > key) hi = mid - 1;
        else return mid;
    }
    return -1;
}

/* branchless lower-bound search (Khuong & Morin §3.1: cmov advance) */
static int arr_search_branchless(const csl_kv* a, int n, int key) {
    if (n <= 0) return -1;
    const csl_kv* base = a;
    int m = n;
    while (m > 1) {
        int half = m >> 1;
        base = (base[half].key < key) ? base + half : base;
        m -= half;
    }
    /* base is the last element < key (or a[0]); step to the lower bound */
    int idx = (int)(base - a) + (base->key < key);
    return (idx < n && a[idx].key == key) ? idx : -1;
}

/* Eytzinger (BFS) layout of the whole array + branchless descent
 * (Khuong & Morin §4).  8 = items per 64-byte cache line for csl_kv. */
static void eyt_build_kv(const csl_kv* sorted, csl_kv* eyt, int n, int* si, int k) {
    if (k >= n) return;
    eyt_build_kv(sorted, eyt, n, si, 2*k + 1);
    eyt[k] = sorted[(*si)++];
    eyt_build_kv(sorted, eyt, n, si, 2*k + 2);
}
static int arr_search_eytzinger(const csl_kv* a, int n, int key) {
    unsigned k = 0;
    while (k < (unsigned)n) {
        __builtin_prefetch(&a[k * 8 + 8], 0, 1);
        k = 2*k + 1 + (unsigned)(a[k].key < key);
    }
    unsigned u = k + 1;
    int shift = __builtin_ffs((int)(~u));
    if (shift == 0) return -1;
    int j = (int)(u >> shift) - 1;
    if (j >= 0 && j < n && a[j].key == key) return j;
    return -1;
}

/* ---------------- exact structural memory accounting ---------------- */

static size_t mem_csl(const cskiplist* sl) {
    size_t bytes = sizeof(cskiplist);
    for (const csl_block* b = sl->head; b; b = b->next[0]) {
        bytes += sizeof(csl_block)
               + (size_t)b->item_cap * sizeof(csl_kv)
               + (size_t)b->skip_alloc * sizeof(csl_block*);
    }
    return bytes;
}
static size_t mem_skiplist(const skiplist* sl) {
    size_t bytes = sizeof(skiplist);
    for (const sl_node* n = sl->head; n; n = n->next[0])
        bytes += sizeof(sl_node) + (size_t)n->height * sizeof(sl_node*);
    return bytes;
}

/* ---------------- experiment configuration ---------------- */

typedef struct {
    const char* mode;    /* search | insert */
    int n;               /* keys */
    int q;               /* queries */
    int caps[64];        /* block-cap sweep */
    int ncaps;
    int reps;
    uint32_t seed;
    int hit_pct;         /* 0..100 */
    const char* dist;    /* uniform | dense | file */
    const char* keyfile;
    const char* outdir;
} config;

typedef struct {
    const char* structure;  /* csv column values */
    const char* layout;
    int block_cap;          /* 0 = not applicable */
    double build_ms;
    double prep_ms;         /* rebuild + layout conversion */
    double search_ns;       /* per query, this repetition */
    double insert_ns;       /* per key (insert mode), else 0 */
    long   hits;
    size_t mem_bytes;
} row;

static FILE* g_csv;
static config g_cfg;

static void csv_write(const row* r, int rep, long expected_hits) {
    fprintf(g_csv,
        "%s,%s,%d,%d,%d,%s,%d,%u,%d,%.3f,%.3f,%.2f,%.2f,%ld,%ld,%lu,%.2f\n",
        r->structure, r->layout, r->block_cap, g_cfg.n, g_cfg.q,
        g_cfg.dist, g_cfg.hit_pct, g_cfg.seed, rep,
        r->build_ms, r->prep_ms, r->search_ns, r->insert_ns,
        r->hits, expected_hits, (unsigned long)r->mem_bytes,
        (double)r->mem_bytes / (double)g_cfg.n);
    if (r->hits != expected_hits)
        printf("  !! %s(%s,cap=%d): hits=%ld expected=%ld\n",
               r->structure, r->layout, r->block_cap, r->hits, expected_hits);
    fflush(g_csv);
}

static void print_row(const row* r, double best_ns) {
    printf("  %-10s %-6s cap=%-5d search=%8.2f ns  (best %7.2f)  "
           "build=%8.1f ms  mem=%6.2f B/key\n",
           r->structure, r->layout, r->block_cap,
           r->search_ns, best_ns, r->build_ms,
           (double)r->mem_bytes / (double)g_cfg.n);
}

/* ---------------- query loop (identical for every structure) ----------------
 * Returns hits; *out_ns gets ns per query.  The found-count is a data
 * dependency, so the compiler cannot elide the searches. */

#define TIMED_QUERY_LOOP(EXPR_FOUND)                                   \
    do {                                                               \
        long hits = 0;                                                 \
        double t0 = now_us();                                          \
        for (int qi = 0; qi < nq; ++qi) {                              \
            int key = qk[qi];                                          \
            hits += (EXPR_FOUND);                                      \
        }                                                              \
        *out_ns = (now_us() - t0) * 1000.0 / (double)nq;               \
        return hits;                                                   \
    } while (0)

static long run_q_arr_branchy(const csl_kv* a, int n, const int* qk, int nq, double* out_ns) {
    TIMED_QUERY_LOOP(arr_search_branchy(a, n, key) >= 0);
}
static long run_q_arr_branchless(const csl_kv* a, int n, const int* qk, int nq, double* out_ns) {
    TIMED_QUERY_LOOP(arr_search_branchless(a, n, key) >= 0);
}
static long run_q_arr_eyt(const csl_kv* a, int n, const int* qk, int nq, double* out_ns) {
    TIMED_QUERY_LOOP(arr_search_eytzinger(a, n, key) >= 0);
}
static long run_q_skiplist(skiplist* sl, const int* qk, int nq, double* out_ns) {
    TIMED_QUERY_LOOP(sl_search(sl, key) != NULL);
}
static long run_q_csl(cskiplist* sl, const int* qk, int nq, double* out_ns) {
    TIMED_QUERY_LOOP(csl_search(sl, key) != NULL);
}

/* ---------------- key & query generation ---------------- */

/* Returns sorted array of n distinct present keys; *absent gets n_absent
 * distinct keys guaranteed NOT in the set (for miss queries). */
static int* gen_keys(config* cfg, int** absent, int* n_absent) {
    int n = cfg->n;

    if (cfg->keyfile) {
        /* load whitespace-separated ints; main() sorts, dedupes and
         * generates the absent-key pool afterwards */
        FILE* f = fopen(cfg->keyfile, "r");
        if (!f) { fprintf(stderr, "cannot open keyfile %s\n", cfg->keyfile); exit(1); }
        int cap = 1 << 20, cnt = 0, v;
        int* keys = (int*)malloc((size_t)cap * sizeof(int));
        while (fscanf(f, "%d", &v) == 1) {
            if (cnt == cap) { cap *= 2; keys = (int*)realloc(keys, (size_t)cap * sizeof(int)); }
            keys[cnt++] = v;
        }
        fclose(f);
        if (cnt == 0) { fprintf(stderr, "keyfile is empty\n"); exit(1); }
        *absent = NULL; *n_absent = 0;
        cfg->n = cnt;
        return keys;
    }

    int* universe;
    if (strcmp(cfg->dist, "dense") == 0) {
        /* present = even numbers, absent = odd numbers */
        int* keys = (int*)malloc((size_t)n * sizeof(int));
        int* miss = (int*)malloc((size_t)n * sizeof(int));
        for (int i = 0; i < n; ++i) { keys[i] = 2*i; miss[i] = 2*i + 1; }
        *absent = miss; *n_absent = n;
        return keys;
    }
    if (strcmp(cfg->dist, "cluster") == 0) {
        /* runs of consecutive keys separated by random gaps — models the
         * prefix-heavy key streams of set-trie nodes.  Keys come out sorted
         * and distinct; the absent pool is generated in main() by rejection
         * sampling from the gaps. */
        int* keys = (int*)malloc((size_t)n * sizeof(int));
        int v = 0, run = 0;
        for (int i = 0; i < n; ++i) {
            if (run == 0) {
                v += 2 + (int)(xrand() % 997u);      /* gap */
                run = 8 + (int)(xrand() % 57u);      /* run length 8..64 */
            }
            keys[i] = v++;
            run--;
        }
        *absent = NULL; *n_absent = 0;
        return keys;
    }
    /* uniform: shuffle universe [0, 2n); first half present, second absent */
    universe = (int*)malloc((size_t)(2*n) * sizeof(int));
    for (int i = 0; i < 2*n; ++i) universe[i] = i;
    shuffle(universe, 2*n);
    int* keys = (int*)malloc((size_t)n * sizeof(int));
    int* miss = (int*)malloc((size_t)n * sizeof(int));
    memcpy(keys, universe, (size_t)n * sizeof(int));
    memcpy(miss, universe + n, (size_t)n * sizeof(int));
    free(universe);
    *absent = miss; *n_absent = n;
    return keys;
}

static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

/* Build the query sequence: hit_pct% present keys, rest absent keys.
 * Returns expected number of hits. */
static long gen_queries(int* qk, int nq, int hit_pct,
                        const int* present, int n,
                        const int* absent, int n_absent) {
    long expected = 0;
    for (int i = 0; i < nq; ++i) {
        int is_hit = (int)(xrand() % 100u) < hit_pct || n_absent == 0;
        if (is_hit) {
            qk[i] = present[xrand() % (uint32_t)n];
            expected++;
        } else {
            qk[i] = absent[xrand() % (uint32_t)n_absent];
        }
    }
    return expected;
}

/* ---------------- structure builders ---------------- */

static cskiplist* build_csl(const int* sorted, int n, int cap, int eyt,
                            double* build_ms, double* prep_ms) {
    double t0 = now_us();
    cskiplist* sl = csl_create_with_block_cap(cap);
    for (int i = 0; i < n; ++i)
        csl_append(sl, sorted[i], (void*)(intptr_t)(sorted[i] + 1));
    *build_ms = (now_us() - t0) / 1000.0;

    t0 = now_us();
    csl_rebuild_skips(sl);
    if (eyt) csl_set_eytzinger(sl, 1);
    *prep_ms = (now_us() - t0) / 1000.0;
    return sl;
}

static skiplist* build_skiplist(const int* sorted, int n, double* build_ms) {
    double t0 = now_us();
    skiplist* sl = sl_create();
    for (int i = 0; i < n; ++i)
        sl_insert(sl, sorted[i], (void*)(intptr_t)(sorted[i] + 1));
    *build_ms = (now_us() - t0) / 1000.0;
    return sl;
}

/* ---------------- main experiment ---------------- */

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [-m search|insert] [-n keys] [-q queries] [-b cap,cap,...]\n"
        "          [-r reps] [-s seed] [-H hit_pct] [-d uniform|dense|cluster]\n"
        "          [-f keyfile] [-o outdir]\n", prog);
    exit(1);
}

int main(int argc, char** argv) {
    timer_init();

    config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = "search";
    cfg.n = 1000000;
    cfg.q = 500000;
    cfg.reps = 3;
    cfg.seed = 42;
    cfg.hit_pct = 50;
    cfg.dist = "uniform";
    cfg.keyfile = NULL;
    cfg.outdir = "results";
    {
        int defaults[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
        memcpy(cfg.caps, defaults, sizeof(defaults));
        cfg.ncaps = 8;
    }

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-m") && i+1 < argc) cfg.mode = argv[++i];
        else if (!strcmp(argv[i], "-n") && i+1 < argc) cfg.n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-q") && i+1 < argc) cfg.q = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i+1 < argc) cfg.reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i+1 < argc) cfg.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-H") && i+1 < argc) cfg.hit_pct = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) cfg.dist = argv[++i]; /* uniform|dense|cluster */
        else if (!strcmp(argv[i], "-f") && i+1 < argc) { cfg.keyfile = argv[++i]; cfg.dist = "file"; }
        else if (!strcmp(argv[i], "-o") && i+1 < argc) cfg.outdir = argv[++i];
        else if (!strcmp(argv[i], "-b") && i+1 < argc) {
            cfg.ncaps = 0;
            char* tok = strtok(argv[++i], ",");
            while (tok && cfg.ncaps < 64) { cfg.caps[cfg.ncaps++] = atoi(tok); tok = strtok(NULL, ","); }
        }
        else usage(argv[0]);
    }
    if (cfg.n < 1 || cfg.q < 1 || cfg.reps < 1) usage(argv[0]);
    g_cfg = cfg;
    g_rng = cfg.seed ? cfg.seed : 42;

    /* ---- keys ---- */
    int* absent = NULL; int n_absent = 0;
    int* present = gen_keys(&cfg, &absent, &n_absent);
    g_cfg = cfg; /* n may have changed for file input */
    int n = cfg.n;

    if (cfg.keyfile) {
        qsort(present, (size_t)n, sizeof(int), cmp_int);
        /* dedupe */
        int w = 1;
        for (int i = 1; i < n; ++i)
            if (present[i] != present[w-1]) present[w++] = present[i];
        n = cfg.n = g_cfg.n = w;
    }

    if (!absent) {
        /* keyfile/cluster: present[] is sorted & distinct here.
         * Generate absent keys: random values in [min,max] not present;
         * fall back to values beyond max if the range is (nearly) dense */
        n_absent = n;
        absent = (int*)malloc((size_t)n_absent * sizeof(int));
        int lo = present[0], hi = present[n-1];
        long range = (long)hi - lo + 1;
        for (int i = 0; i < n_absent; ) {
            if (range <= n + 1) { absent[i] = hi + 1 + i; i++; continue; }
            int v = lo + (int)(xrand() % (uint32_t)range);
            int a = 0, b = n - 1, found = 0;
            while (a <= b) { int m = (a+b)>>1;
                if (present[m] < v) a = m+1;
                else if (present[m] > v) b = m-1;
                else { found = 1; break; } }
            if (!found) absent[i++] = v;
        }
    }

    int* sorted = (int*)malloc((size_t)n * sizeof(int));
    memcpy(sorted, present, (size_t)n * sizeof(int));
    qsort(sorted, (size_t)n, sizeof(int), cmp_int);

    /* ---- queries ---- */
    int nq = cfg.q;
    int* qk = (int*)malloc((size_t)nq * sizeof(int));
    long expected_hits = gen_queries(qk, nq, cfg.hit_pct, present, n, absent, n_absent);

    /* ---- output file: parameters encoded in the name ---- */
    MKDIR(cfg.outdir);
    char path[512];
    snprintf(path, sizeof(path), "%s/exp_%s_%s_n%d_q%d_hit%d_seed%u.csv",
             cfg.outdir, cfg.mode, cfg.dist, n, nq, cfg.hit_pct, cfg.seed);
    g_csv = fopen(path, "w");
    if (!g_csv) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fprintf(g_csv, "structure,layout,block_cap,n,q,dist,hit_pct,seed,rep,"
                   "build_ms,prep_ms,search_ns,insert_ns,hits,expected_hits,"
                   "mem_bytes,bytes_per_key\n");

    printf("=== experiment: mode=%s dist=%s n=%d q=%d hit=%d%% seed=%u reps=%d ===\n",
           cfg.mode, cfg.dist, n, nq, cfg.hit_pct, cfg.seed, cfg.reps);
    printf("    output: %s\n\n", path);

    int verify_ok = 1;

    if (strcmp(cfg.mode, "insert") == 0) {
        /* ------- INSERT MODE: random-order inserts, incremental skips ------ */
        int* rnd = (int*)malloc((size_t)n * sizeof(int));
        memcpy(rnd, present, (size_t)n * sizeof(int));

        for (int rep = 0; rep < cfg.reps; ++rep) {
            shuffle(rnd, n);

            /* classic skip list */
            {
                row r; memset(&r, 0, sizeof(r));
                r.structure = "skiplist"; r.layout = "nodes"; r.block_cap = 0;
                double t0 = now_us();
                skiplist* sl = sl_create();
                for (int i = 0; i < n; ++i)
                    sl_insert(sl, rnd[i], (void*)(intptr_t)(rnd[i] + 1));
                r.insert_ns = (now_us() - t0) * 1000.0 / n;
                r.build_ms = r.insert_ns * n / 1e6;
                r.mem_bytes = mem_skiplist(sl);
                long h = run_q_skiplist(sl, qk, nq, &r.search_ns);
                r.hits = h;
                if (h != expected_hits) verify_ok = 0;
                csv_write(&r, rep, expected_hits);
                print_row(&r, r.search_ns);
                sl_free(sl, NULL);
            }
            /* block skip list per cap (skips maintained incrementally!) */
            for (int ci = 0; ci < cfg.ncaps; ++ci) {
                row r; memset(&r, 0, sizeof(r));
                r.structure = "csl"; r.layout = "sorted"; r.block_cap = cfg.caps[ci];
                double t0 = now_us();
                cskiplist* sl = csl_create_with_block_cap(cfg.caps[ci]);
                for (int i = 0; i < n; ++i)
                    csl_insert(sl, rnd[i], (void*)(intptr_t)(rnd[i] + 1));
                r.insert_ns = (now_us() - t0) * 1000.0 / n;
                r.build_ms = r.insert_ns * n / 1e6;
                r.mem_bytes = mem_csl(sl);
                long h = run_q_csl(sl, qk, nq, &r.search_ns);
                r.hits = h;
                if (h != expected_hits) verify_ok = 0;
                csv_write(&r, rep, expected_hits);
                print_row(&r, r.search_ns);
                csl_free(sl, NULL);
            }
        }
    } else {
        /* ------- SEARCH MODE: bulk load sorted, then query ------- */

        /* sorted kv array shared by the three array baselines */
        csl_kv* akv = (csl_kv*)malloc((size_t)n * sizeof(csl_kv));
        for (int i = 0; i < n; ++i) {
            akv[i].key = sorted[i];
            akv[i].val = (void*)(intptr_t)(sorted[i] + 1);
        }
        csl_kv* ekv = (csl_kv*)malloc((size_t)n * sizeof(csl_kv));
        { int si = 0; eyt_build_kv(akv, ekv, n, &si, 0); }

        for (int rep = 0; rep < cfg.reps; ++rep) {
            /* --- array baselines (no block cap) --- */
            struct { const char* s; const char* l;
                     long (*fn)(const csl_kv*, int, const int*, int, double*);
                     const csl_kv* data; }
            arrs[] = {
                { "array",     "sorted", run_q_arr_branchy,    akv },
                { "array-bl",  "sorted", run_q_arr_branchless, akv },
                { "array-eyt", "eyt",    run_q_arr_eyt,        ekv },
            };
            for (int ai = 0; ai < 3; ++ai) {
                row r; memset(&r, 0, sizeof(r));
                r.structure = arrs[ai].s; r.layout = arrs[ai].l; r.block_cap = 0;
                r.mem_bytes = (size_t)n * sizeof(csl_kv);
                long h = arrs[ai].fn(arrs[ai].data, n, qk, nq, &r.search_ns);
                r.hits = h;
                if (h != expected_hits) verify_ok = 0;
                csv_write(&r, rep, expected_hits);
                print_row(&r, r.search_ns);
            }

            /* --- classic skip list --- */
            {
                row r; memset(&r, 0, sizeof(r));
                r.structure = "skiplist"; r.layout = "nodes"; r.block_cap = 0;
                skiplist* sl = build_skiplist(sorted, n, &r.build_ms);
                r.mem_bytes = mem_skiplist(sl);
                long h = run_q_skiplist(sl, qk, nq, &r.search_ns);
                r.hits = h;
                if (h != expected_hits) verify_ok = 0;
                csv_write(&r, rep, expected_hits);
                print_row(&r, r.search_ns);
                sl_free(sl, NULL);
            }

            /* --- block skip list: cap sweep x {sorted, eytzinger} --- */
            for (int ci = 0; ci < cfg.ncaps; ++ci) {
                for (int eyt = 0; eyt <= 1; ++eyt) {
                    row r; memset(&r, 0, sizeof(r));
                    r.structure = eyt ? "csl-eyt" : "csl";
                    r.layout = eyt ? "eyt" : "sorted";
                    r.block_cap = cfg.caps[ci];
                    cskiplist* sl = build_csl(sorted, n, cfg.caps[ci], eyt,
                                              &r.build_ms, &r.prep_ms);
                    r.mem_bytes = mem_csl(sl);
                    long h = run_q_csl(sl, qk, nq, &r.search_ns);
                    r.hits = h;
                    if (h != expected_hits) verify_ok = 0;
                    csv_write(&r, rep, expected_hits);
                    print_row(&r, r.search_ns);
                    csl_free(sl, NULL);
                }
            }
            printf("  --- rep %d done ---\n", rep);
        }
        free(akv);
        free(ekv);
    }

    fclose(g_csv);

    printf("\n[VERIFY] expected hits per structure: %ld -> %s\n",
           expected_hits, verify_ok ? "ALL STRUCTURES AGREE" : "MISMATCH DETECTED!");
    printf("[OUTPUT] %s\n", path);

    free(sorted);
    free(present);
    free(absent);
    free(qk);
    return verify_ok ? 0 : 1;
}
