/*
 * test-eytzinger.c
 *
 * Correctness test + benchmark for Eytzinger (BFS) layout in cskiplist.
 * Tests: conversion, search, iteration, insert/delete after Eytzinger enable.
 * Benchmark: sorted binary search vs Eytzinger branchless search.
 *
 * Build: gcc -O3 -msse2 -o eytzinger-test cskiplist.o test-eytzinger.c -lpsapi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cskiplist.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
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

#define PASS printf("  PASS\n")
#define FAIL(msg) do { printf("  FAIL: %s\n", msg); failures++; } while(0)

static int failures = 0;

/* ---- Build a skiplist directly (O(N), same as cache benchmark) ---- */

static cskiplist* build_skiplist(int n) {
    cskiplist* sl = csl_create();
    if (!sl) return NULL;

    int cap = CSL_BLOCK_CAP;
    int nblocks = (n + cap - 1) / cap;
    csl_block** blocks = (csl_block**)malloc(sizeof(csl_block*) * nblocks);

    /* create blocks and fill with sequential keys */
    for (int bi = 0; bi < nblocks; bi++) {
        /* use csl_append-style allocation: allocate block with 1 skip slot */
        csl_block* b = (csl_block*)calloc(1, sizeof(csl_block) + sizeof(csl_block*));
        b->skip_alloc = 1;
        int start = bi * cap;
        int end = start + cap;
        if (end > n) end = n;
        b->count = end - start;
        b->min_key = start * 2;  /* keys: 0, 2, 4, ... */
        for (int j = 0; j < b->count; j++) {
            b->items[j].key = (start + j) * 2;
            b->items[j].val = (void*)(intptr_t)((start + j) * 2 + 1);
        }
        blocks[bi] = b;
    }

    /* link blocks */
    sl->head->next[0] = blocks[0];
    for (int bi = 0; bi < nblocks; bi++) {
        blocks[bi]->next[0] = (bi + 1 < nblocks) ? blocks[bi + 1] : NULL;
        blocks[bi]->prev = (bi > 0) ? blocks[bi - 1] : NULL;
    }
    sl->nblocks = nblocks;
    sl->size = n;
    sl->level = 0;
    csl_rebuild_skips(sl);

    free(blocks);
    return sl;
}

/* ---- Test 1: Eytzinger conversion + search correctness ---- */
static void test_conversion_and_search(void) {
    printf("Test 1: Eytzinger conversion and search\n");
    int N = 500;
    cskiplist* sl = build_skiplist(N);
    if (!sl) { FAIL("build_skiplist"); return; }

    /* search all keys in sorted mode */
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2);
        if ((intptr_t)v != i * 2 + 1) { FAIL("sorted search"); csl_free(sl, NULL); return; }
    }
    /* search missing keys */
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2 + 1);
        if (v != NULL) { FAIL("sorted miss"); csl_free(sl, NULL); return; }
    }

    /* enable Eytzinger */
    csl_set_eytzinger(sl, 1);

    /* search all keys again */
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2);
        if ((intptr_t)v != i * 2 + 1) {
            printf("  key=%d expected=%d got=%d\n", i*2, i*2+1, (int)(intptr_t)v);
            FAIL("eytzinger search"); csl_free(sl, NULL); return;
        }
    }
    /* search missing keys */
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2 + 1);
        if (v != NULL) { FAIL("eytzinger miss"); csl_free(sl, NULL); return; }
    }

    /* disable Eytzinger → back to sorted */
    csl_set_eytzinger(sl, 0);
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2);
        if ((intptr_t)v != i * 2 + 1) { FAIL("roundtrip search"); csl_free(sl, NULL); return; }
    }

    csl_free(sl, NULL);
    PASS;
}

/* ---- Test 2: Iterator in Eytzinger mode ---- */
static void test_eytzinger_iteration(void) {
    printf("Test 2: Eytzinger iteration (forward + reverse)\n");
    int N = 300;
    cskiplist* sl = build_skiplist(N);
    if (!sl) { FAIL("build"); return; }

    csl_set_eytzinger(sl, 1);

    /* forward iteration should produce sorted order */
    csl_iter it;
    int count = 0;
    int prev_key = -1;
    if (csl_iter_first(sl, &it)) {
        do {
            csl_kv* kv = csl_iter_get(&it);
            if (!kv) { FAIL("iter_get NULL"); csl_free(sl, NULL); return; }
            if (kv->key <= prev_key) {
                printf("  out of order: prev=%d curr=%d at count=%d\n", prev_key, kv->key, count);
                FAIL("forward order"); csl_free(sl, NULL); return;
            }
            prev_key = kv->key;
            count++;
        } while (csl_iter_next(&it));
    }
    if (count != N) {
        printf("  expected %d items, got %d\n", N, count);
        FAIL("forward count"); csl_free(sl, NULL); return;
    }

    /* reverse iteration from end */
    /* seek to last key */
    int exact = 0;
    csl_iter_seek(sl, (N - 1) * 2, &it, &exact);
    if (!exact) { FAIL("seek last"); csl_free(sl, NULL); return; }
    count = 0;
    prev_key = (N - 1) * 2 + 1;
    do {
        csl_kv* kv = csl_iter_get(&it);
        if (!kv) break;
        if (kv->key >= prev_key) {
            printf("  reverse out of order: prev=%d curr=%d\n", prev_key, kv->key);
            FAIL("reverse order"); csl_free(sl, NULL); return;
        }
        prev_key = kv->key;
        count++;
    } while (csl_iter_prev(sl, &it));
    if (count != N) {
        printf("  expected %d items reverse, got %d\n", N, count);
        FAIL("reverse count"); csl_free(sl, NULL); return;
    }

    csl_free(sl, NULL);
    PASS;
}

/* ---- Test 3: Seek in Eytzinger mode ---- */
static void test_eytzinger_seek(void) {
    printf("Test 3: Eytzinger seek (exact + lower bound)\n");
    int N = 200;
    cskiplist* sl = build_skiplist(N);
    if (!sl) { FAIL("build"); return; }

    csl_set_eytzinger(sl, 1);

    /* exact seeks */
    for (int i = 0; i < N; i += 10) {
        csl_iter it;
        int exact = 0;
        csl_iter_seek(sl, i * 2, &it, &exact);
        if (!exact) { printf("  key=%d not exact\n", i*2); FAIL("exact seek"); csl_free(sl, NULL); return; }
        csl_kv* kv = csl_iter_get(&it);
        if (!kv || kv->key != i * 2) { FAIL("seek value"); csl_free(sl, NULL); return; }
    }

    /* lower-bound seeks (odd keys don't exist, should find next even) */
    for (int i = 0; i < N - 1; i += 10) {
        csl_iter it;
        int exact = 0;
        csl_iter_seek(sl, i * 2 + 1, &it, &exact);
        if (exact) { FAIL("should not be exact"); csl_free(sl, NULL); return; }
        csl_kv* kv = csl_iter_get(&it);
        if (!kv || kv->key != (i + 1) * 2) {
            printf("  seek %d: expected %d got %d\n", i*2+1, (i+1)*2, kv ? kv->key : -1);
            FAIL("lower_bound"); csl_free(sl, NULL); return;
        }
    }

    csl_free(sl, NULL);
    PASS;
}

/* ---- Test 4: Insert and delete after Eytzinger enable ---- */
static void test_eytzinger_insert_delete(void) {
    printf("Test 4: Insert/delete with Eytzinger active\n");
    int N = 100;
    cskiplist* sl = build_skiplist(N);
    if (!sl) { FAIL("build"); return; }

    csl_set_eytzinger(sl, 1);

    /* insert new keys (odd values) */
    for (int i = 0; i < 50; i++) {
        int r = csl_insert(sl, i * 4 + 1, (void*)(intptr_t)(i * 4 + 2));
        if (r != 1) { printf("  insert key=%d returned %d\n", i*4+1, r); FAIL("insert"); csl_free(sl, NULL); return; }
    }

    /* verify all old + new keys */
    for (int i = 0; i < N; i++) {
        csl_val_t v = csl_search(sl, i * 2);
        if ((intptr_t)v != i * 2 + 1) { FAIL("old key after insert"); csl_free(sl, NULL); return; }
    }
    for (int i = 0; i < 50; i++) {
        csl_val_t v = csl_search(sl, i * 4 + 1);
        if ((intptr_t)v != i * 4 + 2) { FAIL("new key search"); csl_free(sl, NULL); return; }
    }

    /* delete some keys */
    for (int i = 0; i < 25; i++) {
        int r = csl_delete(sl, i * 4 + 1, NULL);
        if (r != 1) { FAIL("delete"); csl_free(sl, NULL); return; }
    }
    for (int i = 0; i < 25; i++) {
        csl_val_t v = csl_search(sl, i * 4 + 1);
        if (v != NULL) { FAIL("deleted key found"); csl_free(sl, NULL); return; }
    }

    /* iteration should still produce sorted order */
    csl_iter it;
    int count = 0, prev_key = -1;
    if (csl_iter_first(sl, &it)) {
        do {
            csl_kv* kv = csl_iter_get(&it);
            if (!kv) break;
            if (kv->key <= prev_key) { FAIL("order after modify"); csl_free(sl, NULL); return; }
            prev_key = kv->key;
            count++;
        } while (csl_iter_next(&it));
    }
    int expected = N + 50 - 25;
    if (count != expected) {
        printf("  expected %d, got %d\n", expected, count);
        FAIL("count after modify"); csl_free(sl, NULL); return;
    }

    csl_free(sl, NULL);
    PASS;
}

/* ---- Test 5: Edge cases ---- */
static void test_eytzinger_edges(void) {
    printf("Test 5: Edge cases (empty, 1-element, 2-element)\n");
    /* empty */
    cskiplist* sl = csl_create();
    csl_set_eytzinger(sl, 1);
    if (csl_search(sl, 42) != NULL) { FAIL("empty search"); return; }
    csl_iter it;
    if (csl_iter_first(sl, &it)) { FAIL("empty iter"); return; }
    csl_free(sl, NULL);

    /* 1 element */
    sl = csl_create();
    csl_insert(sl, 10, (void*)20);
    csl_set_eytzinger(sl, 1);
    if ((intptr_t)csl_search(sl, 10) != 20) { FAIL("1-elem search"); return; }
    if (csl_search(sl, 11) != NULL) { FAIL("1-elem miss"); return; }
    csl_free(sl, NULL);

    /* 2 elements */
    sl = csl_create();
    csl_insert(sl, 5, (void*)50);
    csl_insert(sl, 15, (void*)150);
    csl_set_eytzinger(sl, 1);
    if ((intptr_t)csl_search(sl, 5) != 50) { FAIL("2-elem search 1"); return; }
    if ((intptr_t)csl_search(sl, 15) != 150) { FAIL("2-elem search 2"); return; }
    if (csl_search(sl, 10) != NULL) { FAIL("2-elem miss"); return; }
    /* iterate */
    int count = 0;
    if (csl_iter_first(sl, &it)) { do { count++; } while (csl_iter_next(&it)); }
    if (count != 2) { FAIL("2-elem iter count"); return; }
    csl_free(sl, NULL);

    PASS;
}

/* ---- Benchmark: sorted vs Eytzinger search ---- */
static void benchmark(int N, int nqueries) {
    printf("\n=== Benchmark: N=%d, queries=%d, block_cap=%d ===\n", N, nqueries, CSL_BLOCK_CAP);

    cskiplist* sl_sorted = build_skiplist(N);
    cskiplist* sl_eyt = build_skiplist(N);
    if (!sl_sorted || !sl_eyt) { printf("build failed\n"); return; }

    csl_set_eytzinger(sl_eyt, 1);

    /* generate random query keys (mix of hits and misses) */
    int* queries = (int*)malloc(sizeof(int) * nqueries);
    for (int i = 0; i < nqueries; i++) {
        queries[i] = rand() % (N * 2 + 10);  /* some hits (even), some misses (odd, beyond range) */
    }

    /* --- Sequential search benchmark --- */
    int seq_queries = nqueries;
    int* seq_q = (int*)malloc(sizeof(int) * seq_queries);
    for (int i = 0; i < seq_queries; i++) seq_q[i] = (i % N) * 2;  /* all hits, sequential */

    double t0, t1;
    volatile csl_val_t dummy;

    /* sorted sequential */
    t0 = timer_us();
    for (int i = 0; i < seq_queries; i++) dummy = csl_search(sl_sorted, seq_q[i]);
    t1 = timer_us();
    double sorted_seq_us = t1 - t0;

    /* eytzinger sequential */
    t0 = timer_us();
    for (int i = 0; i < seq_queries; i++) dummy = csl_search(sl_eyt, seq_q[i]);
    t1 = timer_us();
    double eyt_seq_us = t1 - t0;

    printf("  Sequential: sorted=%.1f us  eytzinger=%.1f us  speedup=%.2fx\n",
           sorted_seq_us, eyt_seq_us, sorted_seq_us / eyt_seq_us);
    printf("    per-query: sorted=%.1f ns  eytzinger=%.1f ns\n",
           sorted_seq_us * 1000.0 / seq_queries, eyt_seq_us * 1000.0 / seq_queries);

    /* --- Random search benchmark --- */
    t0 = timer_us();
    for (int i = 0; i < nqueries; i++) dummy = csl_search(sl_sorted, queries[i]);
    t1 = timer_us();
    double sorted_rnd_us = t1 - t0;

    t0 = timer_us();
    for (int i = 0; i < nqueries; i++) dummy = csl_search(sl_eyt, queries[i]);
    t1 = timer_us();
    double eyt_rnd_us = t1 - t0;

    printf("  Random:     sorted=%.1f us  eytzinger=%.1f us  speedup=%.2fx\n",
           sorted_rnd_us, eyt_rnd_us, sorted_rnd_us / eyt_rnd_us);
    printf("    per-query: sorted=%.1f ns  eytzinger=%.1f ns\n",
           sorted_rnd_us * 1000.0 / nqueries, eyt_rnd_us * 1000.0 / nqueries);

    (void)dummy;
    free(queries);
    free(seq_q);
    csl_free(sl_sorted, NULL);
    csl_free(sl_eyt, NULL);
}

/* ---- Benchmark: iterator comparison ---- */
static void benchmark_iteration(int N) {
    printf("\n=== Iteration benchmark: N=%d, block_cap=%d ===\n", N, CSL_BLOCK_CAP);

    cskiplist* sl_sorted = build_skiplist(N);
    cskiplist* sl_eyt = build_skiplist(N);
    csl_set_eytzinger(sl_eyt, 1);

    csl_iter it;
    volatile int sum;
    int rounds = 20;
    double t0, t1;

    /* sorted iteration */
    t0 = timer_us();
    for (int r = 0; r < rounds; r++) {
        sum = 0;
        if (csl_iter_first(sl_sorted, &it))
            do { sum += csl_iter_get(&it)->key; } while (csl_iter_next(&it));
    }
    t1 = timer_us();
    double sorted_us = (t1 - t0) / rounds;

    /* eytzinger iteration */
    t0 = timer_us();
    for (int r = 0; r < rounds; r++) {
        sum = 0;
        if (csl_iter_first(sl_eyt, &it))
            do { sum += csl_iter_get(&it)->key; } while (csl_iter_next(&it));
    }
    t1 = timer_us();
    double eyt_us = (t1 - t0) / rounds;

    printf("  Iteration: sorted=%.1f us  eytzinger=%.1f us  ratio=%.2fx\n",
           sorted_us, eyt_us, eyt_us / sorted_us);

    (void)sum;
    csl_free(sl_sorted, NULL);
    csl_free(sl_eyt, NULL);
}

/* ======================================================================
 * Cache miss analysis
 * ======================================================================
 *
 * Two approaches:
 *   1. Analytical: simulate search paths, count distinct cache lines touched
 *   2. Empirical:  sweep data sizes across L1/L2/L3 boundaries, measure
 *                  per-query latency jumps that reveal cache misses
 * ====================================================================== */

#define CACHE_LINE_BYTES 64
#define ITEMS_PER_CL     (CACHE_LINE_BYTES / (int)sizeof(csl_kv))  /* 8 */

/*
 * Simulate sorted binary search on a block of size `n`.
 * Returns the number of distinct cache lines touched for a search
 * targeting sorted position `target_sorted_idx` (0..n-1).
 */
static int sim_sorted_search_cls(int n, int target_sorted_idx) {
    /* Build a mock sorted array: keys = 0, 2, 4, ..., 2*(n-1).
     * Target key = 2*target_sorted_idx. */
    int target_key = target_sorted_idx * 2;
    int cl_seen[256];  /* enough for n <= 2048 items = 32 CLs max */
    int ncl = 0;

    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int cl = mid / ITEMS_PER_CL;
        /* deduplicate */
        int found = 0;
        for (int c = 0; c < ncl; c++) if (cl_seen[c] == cl) { found = 1; break; }
        if (!found) cl_seen[ncl++] = cl;

        if (mid < target_sorted_idx) lo = mid + 1;
        else if (mid > target_sorted_idx) hi = mid - 1;
        else break;
    }
    return ncl;
}

/*
 * Build Eytzinger mapping: eyt_pos[k] = sorted index of element at
 * Eytzinger position k.  Uses in-order recursive build.
 */
static void build_eyt_map(int* eyt_map, int n, int* si, int k) {
    if (k >= n) return;
    build_eyt_map(eyt_map, n, si, 2*k + 1);
    eyt_map[k] = (*si)++;
    build_eyt_map(eyt_map, n, si, 2*k + 2);
}

/*
 * Simulate Eytzinger branchless search on a block of size `n`.
 * `target_sorted_idx` is the sorted position of the key we're looking for.
 * `eyt_map[k]` = sorted index stored at Eytzinger position k.
 * Returns distinct cache lines touched.
 */
static int sim_eytzinger_search_cls(int n, int target_sorted_idx, const int* eyt_map) {
    int cl_seen[256];
    int ncl = 0;

    unsigned k = 0;
    while (k < (unsigned)n) {
        int cl = (int)k / ITEMS_PER_CL;
        int found = 0;
        for (int c = 0; c < ncl; c++) if (cl_seen[c] == cl) { found = 1; break; }
        if (!found) cl_seen[ncl++] = cl;
        /* branchless: go left if items[k] >= target, right if items[k] < target */
        k = 2*k + 1 + (unsigned)(eyt_map[k] < target_sorted_idx);
    }
    return ncl;
}

/*
 * Analytical cache line analysis: for each block size, compute average
 * cache lines touched per search for both layouts.
 */
static void cache_line_analysis(void) {
    printf("\n=== Analytical cache line analysis ===\n");
    printf("  Cache line = %d bytes, items/CL = %d, sizeof(csl_kv) = %d\n\n",
           CACHE_LINE_BYTES, ITEMS_PER_CL, (int)sizeof(csl_kv));
    printf("  %6s  %6s  %10s  %10s  %10s  %6s\n",
           "BlkSz", "CLs", "Sorted", "Eytzinger", "Saved", "Saved%");
    printf("  %6s  %6s  %10s  %10s  %10s  %6s\n",
           "------", "------", "----------", "----------", "----------", "------");

    int sizes[] = {4, 8, 16, 32, 64, 128, 256};
    int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int si = 0; si < nsizes; si++) {
        int n = sizes[si];
        if (n > CSL_BLOCK_CAP && n > 256) continue;

        /* build Eytzinger map for this size */
        int* eyt_map = (int*)malloc(sizeof(int) * n);
        int idx = 0;
        build_eyt_map(eyt_map, n, &idx, 0);

        /* average over ALL possible search targets */
        double sorted_total = 0, eyt_total = 0;
        for (int t = 0; t < n; t++) {
            sorted_total += sim_sorted_search_cls(n, t);
            eyt_total    += sim_eytzinger_search_cls(n, t, eyt_map);
        }
        double sorted_avg = sorted_total / n;
        double eyt_avg    = eyt_total / n;
        double saved      = sorted_avg - eyt_avg;
        double pct        = 100.0 * saved / sorted_avg;
        int total_cls     = (n + ITEMS_PER_CL - 1) / ITEMS_PER_CL;

        printf("  %6d  %6d  %10.2f  %10.2f  %10.2f  %5.1f%%\n",
               n, total_cls, sorted_avg, eyt_avg, saved, pct);

        free(eyt_map);
    }

    printf("\n  Interpretation: Eytzinger touches slightly MORE unique cache\n");
    printf("  lines per block search because BFS indices grow outward.\n");
    printf("  The speedup comes from: (1) branchless search eliminates\n");
    printf("  branch mispredictions (~15 cycles each), (2) forward-only\n");
    printf("  access pattern enables hardware prefetch, (3) explicit\n");
    printf("  __builtin_prefetch of grandchild area hides latency.\n");
}

/*
 * Evict data from CPU caches by reading a large buffer.
 * Size should exceed L3 cache (typically 6-16 MB).
 */
#define EVICT_SIZE (16 * 1024 * 1024)  /* 16 MB */
static volatile int evict_sink;

static void cache_evict(void) {
    static char* evict_buf = NULL;
    if (!evict_buf) {
        evict_buf = (char*)malloc(EVICT_SIZE);
        if (!evict_buf) return;
        memset(evict_buf, 1, EVICT_SIZE);
    }
    /* Read through entire buffer to flush earlier data from all cache levels */
    int sum = 0;
    for (int i = 0; i < EVICT_SIZE; i += 64) {
        sum += evict_buf[i];
    }
    evict_sink = sum;
}

/*
 * Empirical cache measurement: sweep across data sizes to reveal
 * latency jumps at L1/L2/L3 cache boundaries.
 *
 * For each size N, build a skiplist, do random single-key searches
 * with cache eviction between batches, measure per-query latency.
 */
static void cache_latency_sweep(void) {
    printf("\n=== Empirical cache latency sweep ===\n");
    printf("  Measures per-search latency across data sizes.\n");
    printf("  Latency jumps indicate cache level transitions.\n");
    printf("  Typical L1=32KB, L2=256KB, L3=6-16MB\n\n");

    printf("  %8s  %8s  %12s  %12s  %10s\n",
           "N", "DataKB", "Sorted(ns)", "Eytzing(ns)", "Speedup");
    printf("  %8s  %8s  %12s  %12s  %10s\n",
           "--------", "--------", "------------", "------------", "----------");

    /* sizes chosen to straddle typical L1/L2/L3 boundaries */
    int sizes[] = {
        500,      /* ~4 KB  - fits in L1 */
        2000,     /* ~16 KB - fits in L1 */
        4000,     /* ~32 KB - L1 boundary */
        8000,     /* ~64 KB - L2 */
        16000,    /* ~128 KB - L2 */
        32000,    /* ~256 KB - L2 boundary */
        64000,    /* ~512 KB - L3 */
        128000,   /* ~1 MB  - L3 */
        500000,   /* ~4 MB  - L3 */
        1000000,  /* ~8 MB  - L3 boundary */
        2000000,  /* ~16 MB - RAM */
    };
    int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int queries_per_size = 100000;

    for (int si = 0; si < nsizes; si++) {
        int N = sizes[si];
        double data_kb = (double)N * sizeof(csl_kv) / 1024.0;

        cskiplist* sl_sorted = build_skiplist(N);
        cskiplist* sl_eyt = build_skiplist(N);
        if (!sl_sorted || !sl_eyt) { printf("  build failed N=%d\n", N); continue; }
        csl_set_eytzinger(sl_eyt, 1);

        /* generate random queries */
        int nq = queries_per_size;
        int* queries = (int*)malloc(sizeof(int) * nq);
        for (int i = 0; i < nq; i++) queries[i] = (rand() % N) * 2;  /* all hits */

        volatile csl_val_t dummy;
        double t0, t1;

        /* warm up, then measure sorted */
        cache_evict();
        for (int i = 0; i < 1000; i++) dummy = csl_search(sl_sorted, queries[i % nq]);
        cache_evict();
        t0 = timer_us();
        for (int i = 0; i < nq; i++) dummy = csl_search(sl_sorted, queries[i]);
        t1 = timer_us();
        double sorted_ns = (t1 - t0) * 1000.0 / nq;

        /* warm up, then measure eytzinger */
        cache_evict();
        for (int i = 0; i < 1000; i++) dummy = csl_search(sl_eyt, queries[i % nq]);
        cache_evict();
        t0 = timer_us();
        for (int i = 0; i < nq; i++) dummy = csl_search(sl_eyt, queries[i]);
        t1 = timer_us();
        double eyt_ns = (t1 - t0) * 1000.0 / nq;

        printf("  %8d  %7.0fKB  %10.1f ns  %10.1f ns  %9.2fx\n",
               N, data_kb, sorted_ns, eyt_ns, sorted_ns / eyt_ns);

        (void)dummy;
        free(queries);
        csl_free(sl_sorted, NULL);
        csl_free(sl_eyt, NULL);
    }

    printf("\n  Interpretation: larger speedup at bigger sizes means Eytzinger\n");
    printf("  layout reduces cache misses more as data exceeds cache capacity.\n");
}

/*
 * Cold-cache search: evict caches, then time a single search.
 * Repeat many times and average.  Shows worst-case (all-miss) latency.
 */
static void cold_cache_measurement(void) {
    printf("\n=== Cold-cache single-search latency ===\n");
    printf("  Each search is preceded by a full cache eviction.\n");
    printf("  This measures worst-case latency (all cache lines are misses).\n\n");

    int N = 200000;  /* ~1.6 MB, exceeds L1+L2 */
    int trials = 500;

    cskiplist* sl_sorted = build_skiplist(N);
    cskiplist* sl_eyt = build_skiplist(N);
    csl_set_eytzinger(sl_eyt, 1);

    /* Pre-generate random keys for trials */
    int* keys = (int*)malloc(sizeof(int) * trials);
    for (int i = 0; i < trials; i++) keys[i] = (rand() % N) * 2;

    volatile csl_val_t dummy;
    double t0, t1;
    double sorted_total_us = 0, eyt_total_us = 0;

    for (int i = 0; i < trials; i++) {
        cache_evict();
        t0 = timer_us();
        dummy = csl_search(sl_sorted, keys[i]);
        t1 = timer_us();
        sorted_total_us += (t1 - t0);

        cache_evict();
        t0 = timer_us();
        dummy = csl_search(sl_eyt, keys[i]);
        t1 = timer_us();
        eyt_total_us += (t1 - t0);
    }

    double sorted_avg_ns = sorted_total_us * 1000.0 / trials;
    double eyt_avg_ns    = eyt_total_us * 1000.0 / trials;

    printf("  N = %d  (%d KB data),  %d trials\n",
           N, (int)(N * sizeof(csl_kv) / 1024), trials);
    printf("  Sorted:    avg = %.0f ns per cold search\n", sorted_avg_ns);
    printf("  Eytzinger: avg = %.0f ns per cold search\n", eyt_avg_ns);
    printf("  Speedup:   %.2fx\n", sorted_avg_ns / eyt_avg_ns);

    /* Estimate cache misses:
     * Assume ~100 ns per L3/RAM miss on modern hardware.
     * Estimated misses ≈ total_latency / miss_penalty  (rough) */
    double miss_penalty_ns = 100.0;  /* ~100 ns typical DRAM latency */
    double sorted_est_misses = sorted_avg_ns / miss_penalty_ns;
    double eyt_est_misses    = eyt_avg_ns / miss_penalty_ns;
    printf("\n  Estimated cache misses per search (assuming %.0f ns/miss):\n", miss_penalty_ns);
    printf("    Sorted:    ~%.1f misses\n", sorted_est_misses);
    printf("    Eytzinger: ~%.1f misses\n", eyt_est_misses);
    printf("    Saved:     ~%.1f misses per search\n", sorted_est_misses - eyt_est_misses);

    (void)dummy;
    free(keys);
    csl_free(sl_sorted, NULL);
    csl_free(sl_eyt, NULL);
}

int main(int argc, char* argv[]) {
    timer_init();

    printf("=== Eytzinger Layout Tests & Benchmark ===\n");
    printf("CSL_BLOCK_CAP=%d sizeof(csl_kv)=%d\n\n", CSL_BLOCK_CAP, (int)sizeof(csl_kv));

    /* Correctness tests */
    test_conversion_and_search();
    test_eytzinger_iteration();
    test_eytzinger_seek();
    test_eytzinger_insert_delete();
    test_eytzinger_edges();

    if (failures > 0) {
        printf("\n*** %d test(s) FAILED ***\n", failures);
        return 1;
    }
    printf("\nAll correctness tests passed.\n");

    /* Benchmarks */
    int N = 100000;
    int Q = 500000;
    if (argc >= 2) N = atoi(argv[1]);
    if (argc >= 3) Q = atoi(argv[2]);

    benchmark(N, Q);
    benchmark_iteration(N);

    /* Cache miss analysis */
    cache_line_analysis();
    cache_latency_sweep();
    cold_cache_measurement();

    return 0;
}
