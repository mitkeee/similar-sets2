/* bench.c: benchmark on generated keys, same input for every version.
 * insert: n keys in random order; search: q lookups, hit% present;
 * walk: seek + next w keys.  ns per operation, answers checked.
 * Usage: bench-<version> [-n keys] [-q queries] [-w walk] [-H hit%]
 *                        [-r reps] [-s seed] [-o out.csv] */
#define _POSIX_C_SOURCE 200809L
#define __USE_MINGW_ANSI_STDIO 1   /* MinGW: C99 printf */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "skiplist.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now_ns(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1e9 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
#endif

/* splitmix64 */
static uint64_t rng_state;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* keys are >= 1, so never NULL */
#define VAL(k) ((sl_val_t)(intptr_t)(k))

/* first i with a[i] >= x */
static size_t lower_index(const int* a, size_t n, int x)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static void usage(const char* prog)
{
    fprintf(stderr, "usage: %s [-n keys] [-q queries] [-w walk] [-H hit%%] "
                    "[-r repetitions] [-s seed] [-o results.csv]\n", prog);
    exit(2);
}

int main(int argc, char** argv)
{
    long n = 1000000, q = 1000000, w = 16, hit = 50, reps = 3;
    uint64_t seed = 42;
    const char* out = NULL;

    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc || argv[i][0] != '-') usage(argv[0]);
        const char* v = argv[++i];
        switch (argv[i - 1][1]) {
        case 'n': n = atol(v); break;
        case 'q': q = atol(v); break;
        case 'w': w = atol(v); break;
        case 'H': hit = atol(v); break;
        case 'r': reps = atol(v); break;
        case 's': seed = strtoull(v, NULL, 10); break;
        case 'o': out = v; break;
        default: usage(argv[0]);
        }
    }
    if (n < 1 || q < 1 || w < 1 || hit < 0 || hit > 100 || reps < 1) usage(argv[0]);

#ifdef _WIN32
    /* pin to one core */
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    /* sorted distinct keys, gaps 1..4 */
    rng_state = seed;
    int* keys = (int*)malloc((size_t)n * sizeof(int));
    int* order = (int*)malloc((size_t)n * sizeof(int));
    int* qs = (int*)malloc((size_t)q * sizeof(int));
    if (!keys || !order || !qs) { fprintf(stderr, "out of memory\n"); return 1; }
    int k = 0;
    for (long i = 0; i < n; i++) {
        k += 1 + (int)(rnd() % 4);
        keys[i] = k;
    }

    /* insert order: Fisher-Yates shuffle */
    memcpy(order, keys, (size_t)n * sizeof(int));
    for (long i = n - 1; i > 0; i--) {
        long j = (long)(rnd() % (uint64_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    /* queries and expected answers */
    long expect_hits = 0;
    uint64_t expect_sum = 0;
    int lo_key = keys[0], hi_key = keys[n - 1];
    for (long i = 0; i < q; i++) {
        int x;
        if ((long)(rnd() % 100) < hit) {
            x = keys[rnd() % (uint64_t)n];
            expect_hits++;
        } else {
            /* miss (hi_key + 1 is always one) */
            size_t p;
            do {
                x = lo_key + (int)(rnd() % (uint64_t)(hi_key - lo_key + 2));
                p = lower_index(keys, (size_t)n, x);
            } while (p < (size_t)n && keys[p] == x);
        }
        qs[i] = x;
        size_t p = lower_index(keys, (size_t)n, x);
        for (long c = 0; c < w && p < (size_t)n; c++, p++)
            expect_sum += (uint64_t)keys[p];
    }

    printf("# %s  n=%ld  queries=%ld  walk=%ld  hit=%ld%%  seed=%llu  "
           "pointer=%d bytes  compiler %s\n", SL_VERSION, n, q, w, hit,
           (unsigned long long)seed, (int)sizeof(void*), __VERSION__);
    printf("%-12s %4s %12s %12s %12s %14s\n",
           "version", "rep", "insert ns", "search ns", "walk ns", "bytes/key");

    FILE* csv = NULL;
    if (out) {
        csv = fopen(out, "a");
        if (!csv) { perror(out); return 1; }
        fseek(csv, 0, SEEK_END);
        if (ftell(csv) == 0)
            fprintf(csv, "version,n,queries,walk,hit_pct,seed,rep,"
                         "insert_ns,search_ns,walk_ns,bytes_per_key\n");
    }

    for (long r = 1; r <= reps; r++) {
        skiplist* sl = sl_create();
        if (!sl) { fprintf(stderr, "out of memory\n"); return 1; }

        double t0 = now_ns();
        for (long i = 0; i < n; i++)
            if (sl_insert(sl, order[i], VAL(order[i])) < 0) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }
        double t1 = now_ns();

        long hits = 0;
        for (long i = 0; i < q; i++)
            hits += sl_search(sl, qs[i]) == VAL(qs[i]);
        double t2 = now_ns();

        uint64_t sum = 0;
        sl_iter it;
        for (long i = 0; i < q; i++) {
            if (!sl_seek(sl, qs[i], &it)) continue;
            long c = 0;
            do sum += (uint64_t)sl_iter_key(&it);
            while (++c < w && sl_next(&it));
        }
        double t3 = now_ns();

        double bytes_per_key = (double)sl_memory(sl) / (double)n;
        int ok = sl_size(sl) == (size_t)n && hits == expect_hits && sum == expect_sum;
        sl_free(sl);
        if (!ok) {
            fprintf(stderr, "WRONG RESULT in %s: size, hits (%ld, expected %ld) "
                            "or walk checksum differ\n", SL_VERSION, hits, expect_hits);
            return 1;
        }

        double ins = (t1 - t0) / n, sea = (t2 - t1) / q, wal = (t3 - t2) / q;
        printf("%-12s %4ld %12.1f %12.1f %12.1f %14.1f\n",
               SL_VERSION, r, ins, sea, wal, bytes_per_key);
        if (csv)
            fprintf(csv, "%s,%ld,%ld,%ld,%ld,%llu,%ld,%.2f,%.2f,%.2f,%.2f\n",
                    SL_VERSION, n, q, w, hit, (unsigned long long)seed, r,
                    ins, sea, wal, bytes_per_key);
    }
    if (csv) fclose(csv);
    free(keys); free(order); free(qs);
    return 0;
}
