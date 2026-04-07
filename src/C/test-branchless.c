/*
 * test-branchless.c
 *
 * Benchmark: branched vs branchless binary search vs branchless Eytzinger.
 * Implements all three search variants locally for side-by-side comparison.
 * Tests small (2-50) and large (64-5000) arrays, sequential + random patterns.
 *
 * Build: gcc -O3 -msse2 -o branchless-test test-branchless.c -lpsapi
 *   (standalone — does NOT link cskiplist.o; self-contained search impls)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LARGE_INTEGER qpc_freq;
static void timer_init(void) { QueryPerformanceFrequency(&qpc_freq); }
static double timer_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)qpc_freq.QuadPart * 1e6;
}
#else
#include <time.h>
static void timer_init(void) {}
static double timer_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
#endif

/* ====================================================================
 * Search implementations (self-contained for benchmark isolation)
 * ==================================================================== */

typedef struct { int key; void* val; } kv_t;

/* --- 1. Branched binary search (classic if/else) --- */

static int search_branched(const kv_t* arr, int n, int key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (arr[mid].key < key)
            lo = mid + 1;
        else if (arr[mid].key > key)
            hi = mid - 1;
        else
            return mid;
    }
    return -(lo + 1);
}

/* --- 2. Branchless binary search (cmov-friendly lower bound) --- */

static int search_branchless(const kv_t* arr, int n, int key) {
    if (n == 0) return -1;
    const kv_t* base = arr;
    int len = n;
    while (len > 1) {
        int half = len >> 1;
        /* Compiler generates cmov for this ternary */
        base = (base[half].key < key) ? base + half : base;
        len -= half;
    }
    int lo = (int)(base - arr);
    if (lo < n && base->key < key) lo++;
    if (lo < n && arr[lo].key == key) return lo;
    return -(lo + 1);
}

/* --- 3. Branchless Eytzinger search --- */

/* Build Eytzinger layout from sorted array */
static void eyt_build(const kv_t* sorted, kv_t* eyt, int n, int* si, int k) {
    if (k >= n) return;
    eyt_build(sorted, eyt, n, si, 2*k + 1);
    eyt[k] = sorted[(*si)++];
    eyt_build(sorted, eyt, n, si, 2*k + 2);
}

static int search_eytzinger(const kv_t* arr, int n, int key) {
    if (n == 0) return -1;
    unsigned k = 0;
    while (k < (unsigned)n) {
        __builtin_prefetch(&arr[(k * 8 + 8) < (unsigned)n ? k * 8 + 8 : k], 0, 1);
        k = 2*k + 1 + (unsigned)(arr[k].key < key);
    }
    unsigned u = k + 1;
    int shift = __builtin_ffs((int)(~u));
    if (shift == 0) return -(n + 1);
    int j = (int)(u >> shift) - 1;
    if (j < 0) return -(n + 1);
    if (arr[j].key == key) return j;
    return -(j + 1);
}

/* ====================================================================
 * Correctness verification
 * ==================================================================== */

static int verify_correctness(void) {
    printf("=== Correctness verification ===\n");
    int sizes[] = {1, 2, 3, 5, 7, 10, 16, 31, 50, 100, 128, 200, 500};
    int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int failures = 0;

    for (int si = 0; si < nsizes; si++) {
        int n = sizes[si];
        kv_t* sorted = (kv_t*)malloc(sizeof(kv_t) * n);
        kv_t* eyt = (kv_t*)malloc(sizeof(kv_t) * n);

        for (int i = 0; i < n; i++) {
            sorted[i].key = i * 2;
            sorted[i].val = (void*)(intptr_t)(i * 2 + 1);
        }
        int idx = 0;
        eyt_build(sorted, eyt, n, &idx, 0);

        /* Test all existing keys */
        for (int i = 0; i < n; i++) {
            int key = i * 2;
            int r1 = search_branched(sorted, n, key);
            int r2 = search_branchless(sorted, n, key);
            int r3 = search_eytzinger(eyt, n, key);
            if (r1 < 0 || sorted[r1].key != key) {
                printf("  FAIL: branched  n=%d key=%d idx=%d\n", n, key, r1);
                failures++;
            }
            if (r2 < 0 || sorted[r2].key != key) {
                printf("  FAIL: branchless n=%d key=%d idx=%d\n", n, key, r2);
                failures++;
            }
            if (r3 < 0 || eyt[r3].key != key) {
                printf("  FAIL: eytzinger n=%d key=%d idx=%d\n", n, key, r3);
                failures++;
            }
        }

        /* Test missing keys (odd values) */
        for (int i = 0; i < n; i++) {
            int key = i * 2 + 1;
            int r1 = search_branched(sorted, n, key);
            int r2 = search_branchless(sorted, n, key);
            int r3 = search_eytzinger(eyt, n, key);
            if (r1 >= 0) { printf("  FAIL: branched  false hit n=%d key=%d\n", n, key); failures++; }
            if (r2 >= 0) { printf("  FAIL: branchless false hit n=%d key=%d\n", n, key); failures++; }
            if (r3 >= 0) { printf("  FAIL: eytzinger false hit n=%d key=%d\n", n, key); failures++; }
        }

        /* Test beyond-range keys */
        {
            int key = -1;
            if (search_branched(sorted, n, key) >= 0) { printf("  FAIL: branched  low n=%d\n", n); failures++; }
            if (search_branchless(sorted, n, key) >= 0) { printf("  FAIL: branchless low n=%d\n", n); failures++; }
            key = n * 2 + 10;
            if (search_branched(sorted, n, key) >= 0) { printf("  FAIL: branched  high n=%d\n", n); failures++; }
            if (search_branchless(sorted, n, key) >= 0) { printf("  FAIL: branchless high n=%d\n", n); failures++; }
        }

        free(sorted);
        free(eyt);
    }

    if (failures == 0) printf("  All correctness checks passed.\n");
    else printf("  *** %d FAILURES ***\n", failures);
    return failures;
}

/* ====================================================================
 * Benchmark
 * ==================================================================== */

typedef int (*search_fn)(const kv_t* arr, int n, int key);

static double bench_search(search_fn fn, const kv_t* arr, int n,
                           const int* queries, int nq) {
    volatile int dummy;
    double t0 = timer_us();
    for (int i = 0; i < nq; i++) {
        dummy = fn(arr, n, queries[i]);
    }
    double t1 = timer_us();
    (void)dummy;
    return (t1 - t0) * 1000.0 / nq;  /* ns per query */
}

static void benchmark_size(int n, int nqueries, const char* label) {
    kv_t* sorted = (kv_t*)malloc(sizeof(kv_t) * n);
    kv_t* eyt = (kv_t*)malloc(sizeof(kv_t) * n);

    for (int i = 0; i < n; i++) {
        sorted[i].key = i * 2;
        sorted[i].val = (void*)(intptr_t)(i * 2 + 1);
    }
    int idx = 0;
    eyt_build(sorted, eyt, n, &idx, 0);

    /* Sequential queries (all hits, in order) */
    int* seq_q = (int*)malloc(sizeof(int) * nqueries);
    for (int i = 0; i < nqueries; i++) seq_q[i] = (i % n) * 2;

    /* Random queries (mix of hits and misses) */
    int* rnd_q = (int*)malloc(sizeof(int) * nqueries);
    for (int i = 0; i < nqueries; i++) rnd_q[i] = rand() % (n * 2 + 10);

    /* Random hits only (worst case for branch predictor) */
    int* rnd_hits = (int*)malloc(sizeof(int) * nqueries);
    for (int i = 0; i < nqueries; i++) rnd_hits[i] = (rand() % n) * 2;

    double branched_seq  = bench_search(search_branched,  sorted, n, seq_q, nqueries);
    double brless_seq    = bench_search(search_branchless, sorted, n, seq_q, nqueries);
    double eyt_seq       = bench_search(search_eytzinger,  eyt,   n, seq_q, nqueries);

    double branched_rnd  = bench_search(search_branched,  sorted, n, rnd_q, nqueries);
    double brless_rnd    = bench_search(search_branchless, sorted, n, rnd_q, nqueries);
    double eyt_rnd       = bench_search(search_eytzinger,  eyt,   n, rnd_q, nqueries);

    double branched_rhit = bench_search(search_branched,  sorted, n, rnd_hits, nqueries);
    double brless_rhit   = bench_search(search_branchless, sorted, n, rnd_hits, nqueries);
    double eyt_rhit      = bench_search(search_eytzinger,  eyt,   n, rnd_hits, nqueries);

    printf("  %5d  %s\n", n, label);
    printf("    Sequential:   branched=%6.1f ns  branchless=%6.1f ns  eytzinger=%6.1f ns  "
           "bl-speedup=%.2fx  eyt-speedup=%.2fx\n",
           branched_seq, brless_seq, eyt_seq,
           branched_seq / brless_seq, branched_seq / eyt_seq);
    printf("    Random(mix):  branched=%6.1f ns  branchless=%6.1f ns  eytzinger=%6.1f ns  "
           "bl-speedup=%.2fx  eyt-speedup=%.2fx\n",
           branched_rnd, brless_rnd, eyt_rnd,
           branched_rnd / brless_rnd, branched_rnd / eyt_rnd);
    printf("    Random(hits): branched=%6.1f ns  branchless=%6.1f ns  eytzinger=%6.1f ns  "
           "bl-speedup=%.2fx  eyt-speedup=%.2fx\n",
           branched_rhit, brless_rhit, eyt_rhit,
           branched_rhit / brless_rhit, branched_rhit / eyt_rhit);

    free(sorted);
    free(eyt);
    free(seq_q);
    free(rnd_q);
    free(rnd_hits);
}

int main(void) {
    timer_init();
    srand(42);

    printf("=== Branchless Binary Search Benchmark (Issue #16) ===\n");
    printf("sizeof(kv_t) = %d bytes\n\n", (int)sizeof(kv_t));

    if (verify_correctness() != 0) return 1;

    /* --- Small arrays (set-trie leaf nodes, millions of lookups) --- */
    printf("\n=== Small arrays (2-50 elements) ===\n");
    printf("  (These dominate set-trie search: millions of tiny nodes)\n\n");

    int small_sizes[] = {2, 3, 5, 7, 10, 15, 20, 30, 50};
    int nsmall = (int)(sizeof(small_sizes) / sizeof(small_sizes[0]));
    int small_q = 2000000;

    for (int i = 0; i < nsmall; i++) {
        benchmark_size(small_sizes[i], small_q, "(small)");
    }

    /* --- Medium arrays (typical block sizes) --- */
    printf("\n=== Medium arrays (64-256 elements, typical block sizes) ===\n\n");

    int med_sizes[] = {64, 100, 128, 200, 256};
    int nmed = (int)(sizeof(med_sizes) / sizeof(med_sizes[0]));
    int med_q = 1000000;

    for (int i = 0; i < nmed; i++) {
        benchmark_size(med_sizes[i], med_q, "(medium)");
    }

    /* --- Large arrays (stress test) --- */
    printf("\n=== Large arrays (512-5000 elements) ===\n\n");

    int large_sizes[] = {512, 1000, 2000, 5000};
    int nlarge = (int)(sizeof(large_sizes) / sizeof(large_sizes[0]));
    int large_q = 500000;

    for (int i = 0; i < nlarge; i++) {
        benchmark_size(large_sizes[i], large_q, "(large)");
    }

    /* --- Assembly verification hint --- */
    printf("\n=== Assembly verification ===\n");
    printf("  To verify cmov generation:\n");
    printf("    gcc -S -O3 -msse2 -o test-branchless.s test-branchless.c\n");
    printf("    grep cmov test-branchless.s\n");
    printf("  Expected: cmovg/cmovge in search_branchless loop\n");
    printf("  The branched search should show jl/jg/je (conditional jumps)\n");

    return 0;
}
