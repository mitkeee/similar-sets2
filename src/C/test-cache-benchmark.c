/*-----------------------------------------------------------------------------
 * Cache Performance Benchmark for cskiplist block sizes
 * Issue #7: Benchmark cache performance (L1/L2/L3)
 *
 * Measures how different CSL_BLOCK_CAP values affect performance.
 * Since CSL_BLOCK_CAP is compile-time, this binary tests ONE block size.
 * Use run-cache-bench.ps1 to sweep multiple sizes automatically.
 *
 * What we measure:
 *   - Struct/block memory layout vs cache line boundaries
 *   - Sequential search latency (cache-friendly access pattern)
 *   - Random search latency (cache-hostile access pattern)
 *   - Iteration throughput (streaming access)
 *   - Per-operation cycle counts via __rdtsc() when available
 *
 * Why timing-based inference works:
 *   L1 hit  ~ 1-4 cycles    (~1 ns)
 *   L2 hit  ~ 10-20 cycles  (~5 ns)
 *   L3 hit  ~ 40-70 cycles  (~20 ns)
 *   DRAM    ~ 200+ cycles   (~80 ns)
 *   If a block fits in L1, random search within it stays ~1ns/key.
 *   When it spills to L2/L3, random search jumps to ~5-20ns/key.
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ---- Platform-specific high-resolution timing ---- */

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>     /* GetProcessMemoryInfo for memory tracking */

/* QueryPerformanceCounter gives sub-microsecond wall-clock timing */
static double get_time_sec(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}

/* Read CPU timestamp counter via inline asm (MinGW doesn't have intrin.h) */
static uint64_t get_cycles(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

/* Return working set size in bytes (resident memory) */
static size_t get_memory_bytes(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize;
    return 0;
}

#else
#include <sys/time.h>

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

/* Fallback: no rdtsc on non-x86 or non-Windows */
static uint64_t get_cycles(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

static size_t get_memory_bytes(void) {
    return 0; /* not implemented for non-Windows */
}
#endif

/* ---- Include cskiplist (CSL_BLOCK_CAP defined at compile time) ---- */  
#include "cskiplist.h"

/*-----------------------------------------------------------------------------
 * Shuffle utility: Fisher-Yates shuffle for randomizing access order.
 * Produces a uniform random permutation of arr[0..n-1].
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
 * Print cache line analysis for the current block size.
 * Cache line = 64 bytes on x86. We compute how many cache lines one block
 * spans, and what fraction of a cache line is "useful data" vs padding.
 *----------------------------------------------------------------------------*/
static void print_cache_analysis(void) {
    /* Typical x86 cache line size */
    const int CACHE_LINE = 64;

    /* Size of items[] array portion (the useful payload) */
    size_t items_size = CSL_BLOCK_CAP * sizeof(csl_kv);

    /* Size of block metadata (min_key, count, skip_alloc, prev pointer) */
    size_t meta_size = sizeof(int)              /* min_key */
                     + sizeof(int)              /* count */
                     + sizeof(int)              /* skip_alloc */
                     + sizeof(csl_block*);      /* prev */

    /* Total block size with 1 skip pointer (minimum allocation) */
    size_t block_size_min = meta_size + items_size + 1 * sizeof(csl_block*);

    /* Total block size with full skip pointers (head/max allocation) */
    size_t block_size_max = meta_size + items_size + CSL_MAX_LEVEL * sizeof(csl_block*);

    /* How many cache lines does one block span? */
    int cache_lines_min = (int)((block_size_min + CACHE_LINE - 1) / CACHE_LINE);
    int cache_lines_max = (int)((block_size_max + CACHE_LINE - 1) / CACHE_LINE);

    /* How many items fit in one cache line? */
    int items_per_cacheline = CACHE_LINE / (int)sizeof(csl_kv);

    /* Typical L1/L2/L3 sizes for AMD Ryzen 9 (per-core) */
    size_t L1d_size = 32 * 1024;       /* 32 KB L1 data cache per core */
    size_t L2_size  = 512 * 1024;      /* 512 KB L2 per core (varies) */
    size_t L3_size  = 32 * 1024 * 1024;/* 32 MB L3 shared (varies) */

    /* How many blocks fit entirely in each cache level? */
    int blocks_in_L1 = (int)(L1d_size / block_size_min);
    int blocks_in_L2 = (int)(L2_size / block_size_min);
    int blocks_in_L3 = (int)(L3_size / block_size_min);

    printf("=== Cache Line Analysis (CSL_BLOCK_CAP = %d) ===\n", CSL_BLOCK_CAP);
    printf("  Cache line size:          %d bytes\n", CACHE_LINE);
    printf("  sizeof(csl_kv):           %u bytes (key=%u + val=%u)\n",
           (unsigned)sizeof(csl_kv), (unsigned)sizeof(csl_key_t), (unsigned)sizeof(csl_val_t));
    printf("  Items per cache line:     %d\n", items_per_cacheline);
    printf("  Block metadata:           %u bytes\n", (unsigned)meta_size);
    printf("  Items payload:            %u bytes (%d items)\n",
           (unsigned)items_size, CSL_BLOCK_CAP);
    printf("  Block size (1 skip ptr):  %u bytes = %d cache lines\n",
           (unsigned)block_size_min, cache_lines_min);
    printf("  Block size (max skips):   %u bytes = %d cache lines\n",
           (unsigned)block_size_max, cache_lines_max);
    printf("  --- Capacity per cache level (data blocks only) ---\n");
    printf("  L1d (%u KB):  %d blocks = %d items\n",
           (unsigned)(L1d_size/1024), blocks_in_L1, blocks_in_L1 * CSL_BLOCK_CAP);
    printf("  L2  (%u KB):  %d blocks = %d items\n",
           (unsigned)(L2_size/1024), blocks_in_L2, blocks_in_L2 * CSL_BLOCK_CAP);
    printf("  L3  (%u MB):  %d blocks = %d items\n",
           (unsigned)(L3_size/(1024*1024)), blocks_in_L3, blocks_in_L3 * CSL_BLOCK_CAP);
    printf("\n");
}

/*-----------------------------------------------------------------------------
 * Run a single benchmark pass: insert N keys, rebuild skips, then
 * measure sequential search, random search, and iteration.
 *
 * Returns results via the output parameters.
 *----------------------------------------------------------------------------*/
typedef struct {
    double insert_sec;        /* wall-clock seconds for all inserts */
    double rebuild_sec;       /* wall-clock seconds for skip rebuild */
    double seq_search_sec;    /* wall-clock seconds for N sequential searches */
    double rnd_search_sec;    /* wall-clock seconds for N random searches */
    double iter_sec;          /* wall-clock seconds to iterate all items */
    uint64_t seq_search_cyc;  /* total CPU cycles for sequential searches */
    uint64_t rnd_search_cyc;  /* total CPU cycles for random searches */
    size_t memory_after;      /* working set bytes after all inserts */
    int n_items;              /* number of items inserted */
    int n_blocks;             /* number of blocks created */
} bench_result;

static bench_result run_benchmark(int N) {
    bench_result r;
    memset(&r, 0, sizeof(r));
    r.n_items = N;

    /* --- Phase 1: Build skip list with N sorted keys --- */
    /* We construct blocks directly to avoid O(N^2) tail-walk in csl_append.
     * This benchmark measures SEARCH and CACHE performance, not insert speed.
     * Direct construction is O(N) regardless of block size. */
    cskiplist* sl = csl_create();
    if (!sl) {
        fprintf(stderr, "ERROR: csl_create() failed\n");
        return r;
    }

    double t0 = get_time_sec();
    {
        /* Calculate number of full blocks + possible partial last block */
        int nblocks = (N + CSL_BLOCK_CAP - 1) / CSL_BLOCK_CAP;

        /* Allocate all blocks, fill them with sequential keys, and link them */
        csl_block* prev_blk = sl->head;
        int key = 0;

        for (int bi = 0; bi < nblocks; bi++) {
            /* How many items go in this block */
            int items_in_block = CSL_BLOCK_CAP;
            if (bi == nblocks - 1 && N % CSL_BLOCK_CAP != 0) {
                items_in_block = N % CSL_BLOCK_CAP;
            }

            /* Allocate block with 1 skip pointer slot (minimum) */
            size_t blk_sz = sizeof(csl_block) + 1 * sizeof(csl_block*);
            csl_block* b = (csl_block*)calloc(1, blk_sz);
            if (!b) {
                fprintf(stderr, "ERROR: block alloc failed\n");
                csl_free(sl, NULL);
                return r;
            }
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

            /* Link into chain */
            b->next[0] = NULL;
            prev_blk->next[0] = b;
            prev_blk = b;

            sl->nblocks++;
            sl->size += items_in_block;
        }
    }
    r.insert_sec = get_time_sec() - t0;

    /* --- Phase 2: Rebuild skip pointers --- */
    /* After bulk insert, skip pointers need rebuilding for O(log N) search */
    t0 = get_time_sec();
    csl_rebuild_skips(sl);
    r.rebuild_sec = get_time_sec() - t0;

    /* Record block count and memory usage */
    r.n_blocks = (int)sl->nblocks;
    r.memory_after = get_memory_bytes();

    /* --- Phase 3: Sequential search (cache-friendly) --- */
    /* Searching keys 0, 1, 2, ... accesses blocks in order.
     * Each block stays in cache while we search all its keys.
     * This should show near-L1 latency for blocks that fit in L1. */
    {
        volatile csl_val_t sink;  /* prevent compiler from optimizing away */
        uint64_t cyc0 = get_cycles();
        t0 = get_time_sec();

        for (int i = 0; i < N; i++) {
            sink = csl_search(sl, i);
        }

        r.seq_search_sec = get_time_sec() - t0;
        r.seq_search_cyc = get_cycles() - cyc0;
        (void)sink;
    }

    /* --- Phase 4: Random search (cache-hostile) --- */
    /* Random access forces the CPU to fetch different blocks each time.
     * If the total data exceeds L1/L2, this triggers cache misses.
     * The latency difference vs sequential reveals cache pressure. */
    {
        /* Build a random permutation of keys to search */
        int* keys = (int*)malloc(N * sizeof(int));
        if (!keys) {
            fprintf(stderr, "ERROR: malloc failed for random keys\n");
            csl_free(sl, NULL);
            return r;
        }
        for (int i = 0; i < N; i++) keys[i] = i;
        srand(42);  /* fixed seed for reproducibility */
        shuffle(keys, N);

        volatile csl_val_t sink;
        uint64_t cyc0 = get_cycles();
        t0 = get_time_sec();

        for (int i = 0; i < N; i++) {
            sink = csl_search(sl, keys[i]);
        }

        r.rnd_search_sec = get_time_sec() - t0;
        r.rnd_search_cyc = get_cycles() - cyc0;
        (void)sink;
        free(keys);
    }

    /* --- Phase 5: Full iteration (streaming access) --- */
    /* Iterate every item in order. This is the best case for cache:
     * purely sequential memory access, prefetcher should handle it. */
    {
        volatile int sink = 0;
        t0 = get_time_sec();

        csl_iter it;
        if (csl_iter_first(sl, &it)) {
            do {
                csl_kv* kv = csl_iter_get(&it);
                if (kv) sink += kv->key;  /* touch data to prevent skip */
            } while (csl_iter_next(&it));
        }

        r.iter_sec = get_time_sec() - t0;
        (void)sink;
    }

    csl_free(sl, NULL);
    return r;
}

/*-----------------------------------------------------------------------------
 * Print results for one benchmark run in a readable format.
 *----------------------------------------------------------------------------*/
static void print_results(const char* label, bench_result* r) {
    printf("--- %s (N=%d, blocks=%d, block_cap=%d) ---\n",
           label, r->n_items, r->n_blocks, CSL_BLOCK_CAP);

    /* Items per block (average fill rate) */
    double avg_fill = r->n_blocks > 0
        ? (double)r->n_items / r->n_blocks : 0.0;
    printf("  Avg items/block:     %.1f / %d (%.0f%% fill)\n",
           avg_fill, CSL_BLOCK_CAP,
           CSL_BLOCK_CAP > 0 ? avg_fill / CSL_BLOCK_CAP * 100 : 0);

    /* Insert + rebuild timing */
    printf("  Insert:              %.4f sec (%.0f ns/op)\n",
           r->insert_sec,
           r->insert_sec / r->n_items * 1e9);
    printf("  Rebuild skips:       %.4f sec\n", r->rebuild_sec);

    /* Sequential search: should be fast (good locality) */
    double seq_ns = r->seq_search_sec / r->n_items * 1e9;
    double seq_cyc = r->seq_search_cyc > 0
        ? (double)r->seq_search_cyc / r->n_items : 0;
    printf("  Seq search:          %.4f sec (%.1f ns/op, %.1f cyc/op)\n",
           r->seq_search_sec, seq_ns, seq_cyc);

    /* Random search: may be slower due to cache misses */
    double rnd_ns = r->rnd_search_sec / r->n_items * 1e9;
    double rnd_cyc = r->rnd_search_cyc > 0
        ? (double)r->rnd_search_cyc / r->n_items : 0;
    printf("  Rnd search:          %.4f sec (%.1f ns/op, %.1f cyc/op)\n",
           r->rnd_search_sec, rnd_ns, rnd_cyc);

    /* The ratio reveals cache pressure:
     * ratio ~1.0 = data fits in cache, random ~= sequential
     * ratio >2.0 = significant cache misses on random access */
    double ratio = seq_ns > 0 ? rnd_ns / seq_ns : 0;
    printf("  Random/Sequential:   %.2fx ", ratio);
    if (ratio < 1.5)
        printf("(excellent - data fits in cache)\n");
    else if (ratio < 3.0)
        printf("(moderate - some cache pressure)\n");
    else
        printf("(high - significant cache misses)\n");

    /* Iteration throughput */
    double iter_ns = r->iter_sec / r->n_items * 1e9;
    printf("  Iteration:           %.4f sec (%.1f ns/item)\n",
           r->iter_sec, iter_ns);

    /* Memory usage if available */
    if (r->memory_after > 0) {
        printf("  Working set:         %.2f MB\n",
               r->memory_after / (1024.0 * 1024.0));
        /* Bytes per item (including all overhead) */
        double bytes_per_item = (double)r->memory_after / r->n_items;
        printf("  Bytes/item (approx): %.1f\n", bytes_per_item);
    }

    printf("\n");
}

/*-----------------------------------------------------------------------------
 * Print a CSV-style summary line for easy comparison across block sizes.
 * Format: block_cap, N, blocks, seq_ns, rnd_ns, ratio, iter_ns, mem_MB
 *----------------------------------------------------------------------------*/
static void print_csv_line(bench_result* r) {
    double seq_ns = r->seq_search_sec / r->n_items * 1e9;
    double rnd_ns = r->rnd_search_sec / r->n_items * 1e9;
    double ratio  = seq_ns > 0 ? rnd_ns / seq_ns : 0;
    double iter_ns = r->iter_sec / r->n_items * 1e9;
    double mem_mb = r->memory_after / (1024.0 * 1024.0);

    /* CSV output that run-cache-bench.ps1 parses */
    printf("CSV,%d,%d,%d,%.1f,%.1f,%.2f,%.1f,%.2f\n",
           CSL_BLOCK_CAP, r->n_items, r->n_blocks,
           seq_ns, rnd_ns, ratio, iter_ns, mem_mb);
}

/*-----------------------------------------------------------------------------
 * Main: run benchmarks at several data sizes to reveal cache transitions.
 *
 * Small N  -> everything fits in L1, both patterns fast
 * Medium N -> spills to L2, random starts slowing down
 * Large N  -> spills to L3/DRAM, random much slower
 *
 * Usage: test-cache-benchmark [--csv-only]
 *   --csv-only: print only CSV lines (for script parsing)
 *----------------------------------------------------------------------------*/
int main(int argc, char* argv[]) {

    /* Check if we should only output CSV (for the sweep script) */
    int csv_only = 0;
    if (argc > 1 && strcmp(argv[1], "--csv-only") == 0) {
        csv_only = 1;
    }

    if (!csv_only) {
        printf("================================================================\n");
        printf("  Cache Performance Benchmark - CSL_BLOCK_CAP = %d\n", CSL_BLOCK_CAP);
        printf("================================================================\n\n");

        /* Print theoretical cache analysis first */
        print_cache_analysis();
    }

    /*
     * Test sizes chosen to cross cache boundaries:
     *   10K   -> ~120 KB with cap=128, fits in L1+L2
     *   50K   -> ~600 KB, starts pushing L2
     *   200K  -> ~2.4 MB, solidly in L3
     *   500K  -> ~6 MB, may spill past L3 on small caches
     *   1M    -> ~12 MB, tests large working sets
     */
    int sizes[] = { 10000, 50000, 200000, 500000, 1000000 };
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    if (!csv_only) {
        printf("=== Performance Results ===\n\n");
    }

    for (int s = 0; s < n_sizes; s++) {
        bench_result r = run_benchmark(sizes[s]);

        if (!csv_only) {
            char label[64];
            /* Label with human-readable size (e.g., "10K", "1M") */
            if (sizes[s] >= 1000000)
                sprintf(label, "%dM keys", sizes[s] / 1000000);
            else
                sprintf(label, "%dK keys", sizes[s] / 1000);
            print_results(label, &r);
        }

        /* Always print CSV line for script consumption */
        print_csv_line(&r);
    }

    if (!csv_only) {
        printf("=== Interpretation Guide ===\n");
        printf("  - Random/Sequential ratio ~1.0: block fits in cache (ideal)\n");
        printf("  - Ratio 1.5-3.0: moderate cache pressure, consider smaller blocks\n");
        printf("  - Ratio >3.0: significant cache misses, block too large for cache\n");
        printf("  - Optimal block size: largest size where ratio stays near 1.0\n");
        printf("  - Cache line = 64 bytes; block should be a multiple of this\n");
        printf("  - For L1 (32KB): ~%d items of csl_kv fit\n",
               (int)(32*1024 / sizeof(csl_kv)));
        printf("  - For L2 (512KB): ~%d items of csl_kv fit\n",
               (int)(512*1024 / sizeof(csl_kv)));
        printf("\n");
    }

    return 0;
}
