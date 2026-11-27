/* bench.c: experiment 1, one skip list on generated data (as in Pugh 1990).
 *   Sigma = {1, ..., 2n}   the alphabet
 *   X = n random elements of Sigma, put into the skip list in random order
 *   T = m random elements of Sigma, the test elements (about half are in X)
 * Measured, in ns per operation:
 *   insert  every element of X
 *   search  every element of T
 *   walk    for every t in T: the first key >= t and the next keys, 16 in all
 *   delete  every element of X, in random order
 * Every answer is checked.  One run prints one CSV line:
 *   version,n,seed,insert_ns,search_ns,walk_ns,delete_ns
 * Usage: bench-<version> -n n [-m m] [-s seed] */
#define _POSIX_C_SOURCE 200809L
#define __USE_MINGW_ANSI_STDIO 1   /* MinGW: C99 printf */
#include <stdio.h>
#include <stdlib.h>
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

#define WALK 16

/* splitmix64 */
static uint64_t rng_state;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void shuffle(int* a, long n)
{
    for (long i = n - 1; i > 0; i--) {
        long j = (long)(rnd() % (uint64_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* keys are >= 1, so never NULL */
#define VAL(k) ((sl_val_t)(intptr_t)(k))

static void usage(const char* prog)
{
    fprintf(stderr, "usage: %s -n n [-m m] [-s seed]\n", prog);
    exit(2);
}

int main(int argc, char** argv)
{
    long n = 0, m = 1000000;
    uint64_t seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (argv[i][0] != '-') usage(argv[0]);
        switch (argv[i][1]) {
        case 'n': n = atol(argv[i + 1]); break;
        case 'm': m = atol(argv[i + 1]); break;
        case 's': seed = strtoull(argv[i + 1], NULL, 10); break;
        default: usage(argv[0]);
        }
    }
    if (argc % 2 == 0 || n < 1 || m < 1) usage(argv[0]);

#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1);   /* one core */
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    /* X = the first n of Sigma shuffled, T = m random elements of Sigma */
    long sigma = 2 * n;
    rng_state = seed;
    int* x = (int*)malloc((size_t)sigma * sizeof(int));
    int* t = (int*)malloc((size_t)m * sizeof(int));
    char* in_x = (char*)calloc((size_t)sigma + 2, 1);
    if (!x || !t || !in_x) { fprintf(stderr, "out of memory\n"); return 1; }
    for (long i = 0; i < sigma; i++) x[i] = (int)(i + 1);
    shuffle(x, sigma);
    for (long i = 0; i < n; i++) in_x[x[i]] = 1;
    for (long j = 0; j < m; j++) t[j] = (int)(1 + rnd() % (uint64_t)sigma);

    /* correct answers */
    long expect_hits = 0;
    uint64_t expect_walk = 0;
    for (long j = 0; j < m; j++) {
        expect_hits += in_x[t[j]];
        int c = 0;
        for (long k = t[j]; k <= sigma && c < WALK; k++)
            if (in_x[k]) { expect_walk += (uint64_t)k; c++; }
    }

    skiplist* sl = sl_create();
    if (!sl) { fprintf(stderr, "out of memory\n"); return 1; }

    double t0 = now_ns();
    for (long i = 0; i < n; i++)
        if (sl_insert(sl, x[i], VAL(x[i])) < 0) { fprintf(stderr, "out of memory\n"); return 1; }
    double t1 = now_ns();

    long hits = 0;
    for (long j = 0; j < m; j++)
        hits += sl_search(sl, t[j]) == VAL(t[j]);
    double t2 = now_ns();

    uint64_t walk = 0;
    sl_iter it;
    for (long j = 0; j < m; j++) {
        if (!sl_seek(sl, t[j], &it)) continue;
        int c = 0;
        do walk += (uint64_t)sl_iter_key(&it);
        while (++c < WALK && sl_next(&it));
    }
    double t3 = now_ns();

    shuffle(x, n);                                  /* delete order */
    double t4 = now_ns();
    long deleted = 0;
    for (long i = 0; i < n; i++)
        deleted += sl_delete(sl, x[i], NULL);
    double t5 = now_ns();

    int ok = hits == expect_hits && walk == expect_walk && deleted == n && sl_size(sl) == 0;
    sl_free(sl);
    free(x); free(t); free(in_x);
    if (!ok) {
        fprintf(stderr, "WRONG RESULT: %s n=%ld seed=%llu\n", SL_VERSION, n,
                (unsigned long long)seed);
        return 1;
    }
    printf("%s,%ld,%llu,%.1f,%.1f,%.1f,%.1f\n", SL_VERSION, n, (unsigned long long)seed,
           (t1 - t0) / n, (t2 - t1) / m, (t3 - t2) / m, (t5 - t4) / n);
    return 0;
}
