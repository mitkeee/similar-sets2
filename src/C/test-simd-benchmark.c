/*-----------------------------------------------------------------------------
 * SIMD Search Benchmark for cskiplist
 * Issue #9 / #26: Compare scalar vs SIMD-accelerated intra-block search
 *
 * Compiles the same cskiplist code twice (with and without SIMD) and
 * measures the search performance difference.  Since CSL_USE_SIMD is
 * baked into cskiplist.c, we benchmark by compiling two separate
 * executables.  This file tests the CURRENTLY compiled variant.
 *
 * Usage:
 *   Compile with SIMD:    gcc -O3 -msse2 -o simd-bench.exe ...
 *   Compile without SIMD: gcc -O3 -DCSL_USE_SIMD=0 -o scalar-bench.exe ...
 *   Or use: run-simd-bench.ps1 which does both automatically.
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "cskiplist.h"

/*-----------------------------------------------------------------------------
 * High-resolution timing (same approach as cache benchmark).
 * QueryPerformanceCounter on Windows, rdtsc for cycle counts.
 *----------------------------------------------------------------------------*/
#ifdef _WIN32
static double get_time_sec(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}
#endif

/* Read CPU timestamp counter via inline asm */
static uint64_t get_cycles(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

/*-----------------------------------------------------------------------------
 * Fisher-Yates shuffle for generating random access patterns.
 *----------------------------------------------------------------------------*/
static void shuffle(int* arr, int n) {
    for (int i = n - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/*-----------------------------------------------------------------------------
 * Build a skip list with N sequential keys using direct block construction.
 * This is O(N) — bypasses the O(N^2) tail-walk in csl_append.
 *----------------------------------------------------------------------------*/
static cskiplist* build_skiplist(int N) {
    cskiplist* sl = csl_create();
    if (!sl) return NULL;

    /* Direct block construction: allocate and fill blocks manually */
    int nblocks = (N + CSL_BLOCK_CAP - 1) / CSL_BLOCK_CAP;
    csl_block* prev_blk = sl->head;
    int key = 0;

    for (int bi = 0; bi < nblocks; bi++) {
        int items_in_block = CSL_BLOCK_CAP;
        if (bi == nblocks - 1 && N % CSL_BLOCK_CAP != 0) {
            items_in_block = N % CSL_BLOCK_CAP;
        }

        /* Allocate block with 1 skip pointer slot */
        size_t blk_sz = sizeof(csl_block) + 1 * sizeof(csl_block*);
        csl_block* b = (csl_block*)calloc(1, blk_sz);
        if (!b) { csl_free(sl, NULL); return NULL; }
        b->skip_alloc = 1;
        b->min_key = key;
        b->count = items_in_block;
        b->prev = (prev_blk == sl->head) ? NULL : prev_blk;

        /* Fill items with sequential keys */
        for (int j = 0; j < items_in_block; j++) {
            b->items[j].key = key;
            b->items[j].val = (csl_val_t)(intptr_t)key;
            key++;
        }

        b->next[0] = NULL;
        prev_blk->next[0] = b;
        prev_blk = b;

        sl->nblocks++;
        sl->size += items_in_block;
    }

    /* Rebuild skip pointers for O(log N) block-level search */
    csl_rebuild_skips(sl);
    return sl;
}

/*-----------------------------------------------------------------------------
 * Benchmark search at a specific data size.
 * Measures both sequential (cache-friendly) and random (cache-hostile) search.
 * Returns results as a struct.
 *----------------------------------------------------------------------------*/
typedef struct {
    int N;                  /* number of items */
    int nblocks;            /* number of blocks */
    double seq_ns;          /* sequential search: nanoseconds per operation */
    double rnd_ns;          /* random search: nanoseconds per operation */
    double seq_cyc;         /* sequential search: cycles per operation */
    double rnd_cyc;         /* random search: cycles per operation */
    int search_hits;        /* number of successful searches (sanity check) */
} simd_result;

static simd_result bench_search(int N) {
    simd_result r;
    memset(&r, 0, sizeof(r));
    r.N = N;

    /* Build the skip list */
    cskiplist* sl = build_skiplist(N);
    if (!sl) {
        fprintf(stderr, "ERROR: build_skiplist(%d) failed\n", N);
        return r;
    }
    r.nblocks = (int)sl->nblocks;

    /* --- Sequential search: all N keys in order --- */
    /* This access pattern is cache-friendly: we search keys 0,1,2,...
     * so each block's items stay hot in L1 while we search within it. */
    {
        volatile csl_val_t sink;
        int hits = 0;
        uint64_t cyc0 = get_cycles();
        double t0 = get_time_sec();

        for (int i = 0; i < N; i++) {
            sink = csl_search(sl, i);
            if (sink) hits++;
        }

        double elapsed = get_time_sec() - t0;
        uint64_t cyc_total = get_cycles() - cyc0;
        r.seq_ns = elapsed / N * 1e9;
        r.seq_cyc = cyc_total > 0 ? (double)cyc_total / N : 0;
        r.search_hits = hits;
        (void)sink;
    }

    /* --- Random search: all N keys in shuffled order --- */
    /* This pattern forces CPU to jump between different blocks,
     * stressing the cache.  SIMD helps here by reducing the number
     * of instructions per block once we arrive at it. */
    {
        int* keys = (int*)malloc(N * sizeof(int));
        if (!keys) {
            csl_free(sl, NULL);
            return r;
        }
        for (int i = 0; i < N; i++) keys[i] = i;
        srand(42);
        shuffle(keys, N);

        volatile csl_val_t sink;
        uint64_t cyc0 = get_cycles();
        double t0 = get_time_sec();

        for (int i = 0; i < N; i++) {
            sink = csl_search(sl, keys[i]);
        }

        double elapsed = get_time_sec() - t0;
        uint64_t cyc_total = get_cycles() - cyc0;
        r.rnd_ns = elapsed / N * 1e9;
        r.rnd_cyc = cyc_total > 0 ? (double)cyc_total / N : 0;
        (void)sink;
        free(keys);
    }

    csl_free(sl, NULL);
    return r;
}

/*-----------------------------------------------------------------------------
 * Main: run benchmarks at multiple sizes, print results.
 *
 * The output includes a tag line identifying whether SIMD is enabled,
 * so run-simd-bench.ps1 can compare the two compilations.
 *----------------------------------------------------------------------------*/
int main(int argc, char* argv[]) {
    /* Detect SIMD status: replicate the same auto-detection logic
     * used in cskiplist.c so our report matches the actual codepath. */
    int simd_enabled = 0;
#if defined(CSL_USE_SIMD)
    /* Explicitly set by -DCSL_USE_SIMD=0 or -DCSL_USE_SIMD=1 */
    simd_enabled = CSL_USE_SIMD;
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64) || \
      (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__i386__)
    /* Auto-detect: same conditions as cskiplist.c */
    simd_enabled = 1;
#endif

    /* Allow --csv-only flag for script parsing */
    int csv_only = 0;
    if (argc > 1 && strcmp(argv[1], "--csv-only") == 0) {
        csv_only = 1;
    }

    if (!csv_only) {
        printf("================================================================\n");
        printf("  SIMD Search Benchmark - CSL_BLOCK_CAP = %d\n", CSL_BLOCK_CAP);
        printf("  SIMD status: %s\n", simd_enabled ? "ENABLED (SSE2)" : "DISABLED (scalar only)");
#if defined(CSL_SIMD_SCAN_THRESHOLD)
        printf("  SIMD scan threshold: %d items (SIMD scan if count <= this)\n",
               CSL_SIMD_SCAN_THRESHOLD);
#else
        /* CSL_SIMD_SCAN_THRESHOLD is defined internally in cskiplist.c */
        printf("  SIMD scan threshold: 32 items (default, set in cskiplist.c)\n");
#endif
        printf("================================================================\n\n");
    }

    /* Test sizes: small blocks exercise SIMD scan, large blocks exercise binary search */
    int sizes[] = { 1000, 5000, 10000, 50000, 100000, 500000 };
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    if (!csv_only) {
        printf("%-8s %-8s %-10s %-10s %-10s %-10s %-8s\n",
               "N", "Blocks", "Seq(ns)", "Rnd(ns)", "Seq(cyc)", "Rnd(cyc)", "Hits");
        printf("%-8s %-8s %-10s %-10s %-10s %-10s %-8s\n",
               "------", "------", "--------", "--------", "--------", "--------", "------");
    }

    for (int s = 0; s < n_sizes; s++) {
        simd_result r = bench_search(sizes[s]);

        if (!csv_only) {
            printf("%-8d %-8d %-10.1f %-10.1f %-10.1f %-10.1f %-8d\n",
                   r.N, r.nblocks, r.seq_ns, r.rnd_ns,
                   r.seq_cyc, r.rnd_cyc, r.search_hits);
        }

        /* CSV: simd_flag, block_cap, N, nblocks, seq_ns, rnd_ns, seq_cyc, rnd_cyc */
        printf("CSV,%s,%d,%d,%d,%.1f,%.1f,%.1f,%.1f\n",
               simd_enabled ? "SIMD" : "SCALAR",
               CSL_BLOCK_CAP, r.N, r.nblocks,
               r.seq_ns, r.rnd_ns, r.seq_cyc, r.rnd_cyc);
    }

    if (!csv_only) {
        printf("\n");
        printf("=== Notes ===\n");
#if defined(CSL_SIMD_SCAN_THRESHOLD)
        printf("  - SIMD (SSE2) scans 4 keys per cycle for blocks <= %d items\n",
               CSL_SIMD_SCAN_THRESHOLD);
#else
        printf("  - SIMD (SSE2) scans 4 keys per cycle for blocks <= 32 items (default)\n");
#endif
        printf("  - Larger blocks use binary search with __builtin_expect hints\n");
        printf("  - Compare SIMD vs SCALAR by compiling with/without -DCSL_USE_SIMD=0\n");
        printf("  - Lower ns/op = faster.  Cycles/op removes clock speed variation.\n");
        printf("\n");
    }

    return 0;
}
