/* dataset.c: benchmark on the node dictionaries (child keys) of the set-trie
 * of a dataset, one skip list per dictionary.  Results per dictionary size
 * (1, 2-3, 4-7, ...): insert ns/key, search ns, walk ns, bytes/key.
 * Answers checked.
 * Usage: data-<version> -d dataset.txt [-q queries per size] [-w walk]
 *                       [-H hit%] [-r reps] [-s seed] [-o out.csv] */
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

static uint64_t rng_state;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* never NULL, also for key 0 */
#define VAL(k) ((sl_val_t)((intptr_t)(k) + 1))

static void* xrealloc(void* p, size_t n)
{
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

/* dictionary i = dkeys[doff[i] .. doff[i+1]), sorted */
static int*    dkeys;
static size_t  nkeys, capkeys;
static size_t* doff;
static size_t  ndicts, capdicts;

static void emit_dict(const int* keys, size_t n)
{
    if (nkeys + n > capkeys) {
        while (nkeys + n > capkeys) capkeys = capkeys ? capkeys * 2 : 1 << 20;
        dkeys = (int*)xrealloc(dkeys, capkeys * sizeof(int));
    }
    if (ndicts + 2 > capdicts) {
        capdicts = capdicts ? capdicts * 2 : 1 << 16;
        doff = (size_t*)xrealloc(doff, capdicts * sizeof(size_t));
    }
    if (ndicts == 0) doff[0] = 0;
    memcpy(dkeys + nkeys, keys, n * sizeof(int));
    nkeys += n;
    doff[++ndicts] = nkeys;
}

typedef struct reader {
    FILE* f;
    char buf[1 << 20];
    size_t len, pos;
} reader;

/* next line (one set) into *el; 0 at end of file */
static int read_line(reader* r, int** el, size_t* n, size_t* cap)
{
    int innum = 0, any = 0;
    long v = 0;
    *n = 0;
    for (;;) {
        if (r->pos == r->len) {
            r->len = fread(r->buf, 1, sizeof(r->buf), r->f);
            r->pos = 0;
            if (r->len == 0) break;
        }
        char c = r->buf[r->pos++];
        any = 1;
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            innum = 1;
            continue;
        }
        if (innum) {
            if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *el = (int*)xrealloc(*el, *cap * sizeof(int)); }
            (*el)[(*n)++] = (int)v;
            v = 0;
            innum = 0;
        }
        if (c == '\n') return 1;
    }
    if (innum) {
        if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *el = (int*)xrealloc(*el, *cap * sizeof(int)); }
        (*el)[(*n)++] = (int)v;
    }
    return any;
}

/* Dictionaries in one pass over the sorted sets.  Node at depth d = prefix of
 * length d; it has a dictionary if it is the root or >= 2 sets go on past it
 * (else the rest is a tail).  A node is complete when the next set leaves it. */
static int**   ch;      /* keys of the open node at depth d */
static size_t* chn;
static size_t* chcap;
static long*   cnt;     /* sets going on past depth d */
static size_t  depth_cap;

static void ensure_depth(size_t need)
{
    if (need <= depth_cap) return;
    size_t nc = depth_cap ? depth_cap : 64;
    while (nc < need) nc *= 2;
    ch = (int**)xrealloc(ch, nc * sizeof(int*));
    chn = (size_t*)xrealloc(chn, nc * sizeof(size_t));
    chcap = (size_t*)xrealloc(chcap, nc * sizeof(size_t));
    cnt = (long*)xrealloc(cnt, nc * sizeof(long));
    for (size_t d = depth_cap; d < nc; d++) { ch[d] = NULL; chn[d] = chcap[d] = 0; cnt[d] = 0; }
    depth_cap = nc;
}

static void add_key(size_t d, int key)
{
    if (chn[d] == chcap[d]) {
        chcap[d] = chcap[d] ? chcap[d] * 2 : 8;
        ch[d] = (int*)xrealloc(ch[d], chcap[d] * sizeof(int));
    }
    ch[d][chn[d]++] = key;
}

static void close_node(size_t d)
{
    if ((cnt[d] >= 2 || d == 0) && chn[d] > 0)
        emit_dict(ch[d], chn[d]);
    chn[d] = 0;
    cnt[d] = 0;
}

static long load_dataset(const char* path)
{
    reader* r = (reader*)malloc(sizeof(reader));
    if (!r) { fprintf(stderr, "out of memory\n"); exit(1); }
    r->f = fopen(path, "rb");
    if (!r->f) { perror(path); exit(1); }
    r->len = r->pos = 0;

    int *cur = NULL, *prev = NULL;
    size_t k = 0, curcap = 0, kp = 0, prevcap = 0;
    long nsets = 0;
    while (read_line(r, &cur, &k, &curcap)) {
        if (k == 0) continue;
        nsets++;
        ensure_depth(k + 1);
        size_t c = 0;
        if (nsets > 1) {
            size_t m = k < kp ? k : kp;
            while (c < m && cur[c] == prev[c]) c++;
            if (c == k || (c < kp && cur[c] < prev[c])) {
                fprintf(stderr, "%s: set %ld is not in sorted order (or a duplicate)\n", path, nsets);
                exit(1);
            }
            for (size_t d = kp; d > c; d--) close_node(d);
        }
        /* a set ending at depth k does not count there */
        for (size_t d = 0; d <= c; d++) cnt[d]++;
        for (size_t d = c + 1; d <= k; d++) { cnt[d] = d < k; chn[d] = 0; }
        for (size_t d = c; d < k; d++) add_key(d, cur[d]);

        int* t = prev; prev = cur; cur = t;
        size_t tc = prevcap; prevcap = curcap; curcap = tc;
        kp = k;
    }
    for (size_t d = kp; d > 0; d--) close_node(d);
    if (nsets > 0) close_node(0);

    fclose(r->f);
    free(r); free(cur); free(prev);
    return nsets;
}

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

static int size_class(size_t n)     /* 1 -> 0, 2-3 -> 1, 4-7 -> 2, ... */
{
    int b = 0;
    while (n > 1) { n >>= 1; b++; }
    return b;
}

static void usage(const char* prog)
{
    fprintf(stderr, "usage: %s -d dataset.txt [-q queries per size] [-w walk] [-H hit%%] "
                    "[-r repetitions] [-s seed] [-o results.csv]\n", prog);
    exit(2);
}

#define NCLASS 40

int main(int argc, char** argv)
{
    long q = 500000, w = 16, hit = 50, reps = 3;
    uint64_t seed = 42;
    const char *path = NULL, *out = NULL;

    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc || argv[i][0] != '-') usage(argv[0]);
        const char* v = argv[++i];
        switch (argv[i - 1][1]) {
        case 'd': path = v; break;
        case 'q': q = atol(v); break;
        case 'w': w = atol(v); break;
        case 'H': hit = atol(v); break;
        case 'r': reps = atol(v); break;
        case 's': seed = strtoull(v, NULL, 10); break;
        case 'o': out = v; break;
        default: usage(argv[0]);
        }
    }
    if (!path || q < 1 || w < 1 || hit < 0 || hit > 100 || reps < 1) usage(argv[0]);

#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    double t0 = now_ns();
    long nsets = load_dataset(path);
    double tload = (now_ns() - t0) / 1e9;
    if (ndicts == 0) { fprintf(stderr, "%s: no sets\n", path); return 1; }

    const char* name = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;

    /* group by size class */
    size_t ccount[NCLASS] = {0}, ckeys[NCLASS] = {0}, largest = 0;
    for (size_t i = 0; i < ndicts; i++) {
        size_t n = doff[i + 1] - doff[i];
        int b = size_class(n);
        ccount[b]++;
        ckeys[b] += n;
        if (n > largest) largest = n;
    }
    size_t* cfirst[NCLASS];
    for (int b = 0; b < NCLASS; b++) {
        cfirst[b] = ccount[b] ? (size_t*)malloc(ccount[b] * sizeof(size_t)) : NULL;
        if (ccount[b] && !cfirst[b]) { fprintf(stderr, "out of memory\n"); return 1; }
        ccount[b] = 0;
    }
    for (size_t i = 0; i < ndicts; i++) {
        int b = size_class(doff[i + 1] - doff[i]);
        cfirst[b][ccount[b]++] = i;
    }

    printf("# %s  dataset=%s  sets=%ld  dictionaries=%zu  keys=%zu  largest=%zu  "
           "(read in %.1f s)\n", SL_VERSION, name, nsets, ndicts, nkeys, largest, tload);
    printf("# queries=%ld per size  walk=%ld  hit=%ld%%  seed=%llu  pointer=%d bytes  "
           "compiler %s\n", q, w, hit, (unsigned long long)seed, (int)sizeof(void*), __VERSION__);

    FILE* csv = NULL;
    if (out) {
        csv = fopen(out, "a");
        if (!csv) { perror(out); return 1; }
        fseek(csv, 0, SEEK_END);
        if (ftell(csv) == 0)
            fprintf(csv, "version,dataset,rep,size_from,size_to,dicts,keys,"
                         "insert_ns,search_ns,walk_ns,bytes_per_key\n");
    }

    uint32_t* qd = (uint32_t*)malloc((size_t)q * sizeof(uint32_t));
    int* qx = (int*)malloc((size_t)q * sizeof(int));
    skiplist** lists = (skiplist**)malloc(ndicts * sizeof(skiplist*));
    if (!qd || !qx || !lists) { fprintf(stderr, "out of memory\n"); return 1; }

    for (long r = 1; r <= reps; r++) {
        printf("%-12s %4s %12s %10s %12s %10s %10s %10s %10s\n", "version", "rep", "size",
               "dicts", "keys", "insert ns", "search ns", "walk ns", "bytes/key");

        /* build, per size class */
        double ins_ns[NCLASS] = {0};
        for (int b = 0; b < NCLASS; b++) {
            if (!ccount[b]) continue;
            double t1 = now_ns();
            for (size_t j = 0; j < ccount[b]; j++) {
                size_t i = cfirst[b][j];
                skiplist* sl = sl_create();
                if (!sl) { fprintf(stderr, "out of memory\n"); return 1; }
                for (size_t p = doff[i]; p < doff[i + 1]; p++)
                    if (sl_insert(sl, dkeys[p], VAL(dkeys[p])) < 0) {
                        fprintf(stderr, "out of memory\n");
                        return 1;
                    }
                lists[i] = sl;
            }
            ins_ns[b] = (now_ns() - t1) / (double)ckeys[b];
        }

        for (int b = 0; b < NCLASS; b++) {
            if (!ccount[b]) continue;

            /* queries, same for every version */
            rng_state = seed + (uint64_t)b * 1000003u;
            long expect_hits = 0;
            uint64_t expect_sum = 0;
            for (long j = 0; j < q; j++) {
                size_t i = cfirst[b][rnd() % ccount[b]];
                const int* a = dkeys + doff[i];
                size_t n = doff[i + 1] - doff[i];
                int x = 0;
                if ((long)(rnd() % 100) < hit) {
                    x = a[rnd() % n];
                    expect_hits++;
                } else {
                    /* miss in range, else a[n-1] + 1 */
                    int lo = a[0], range = a[n - 1] - a[0] + 2, t;
                    for (t = 0; t < 16; t++) {
                        x = lo + (int)(rnd() % (uint64_t)range);
                        size_t p = lower_index(a, n, x);
                        if (p == n || a[p] != x) break;
                    }
                    if (t == 16) x = a[n - 1] + 1;
                }
                qd[j] = (uint32_t)i;
                qx[j] = x;
                size_t p = lower_index(a, n, x);
                for (long c = 0; c < w && p < n; c++, p++)
                    expect_sum += (uint64_t)a[p];
            }

            double t1 = now_ns();
            long hits = 0;
            for (long j = 0; j < q; j++)
                hits += sl_search(lists[qd[j]], qx[j]) == VAL(qx[j]);
            double t2 = now_ns();
            uint64_t sum = 0;
            sl_iter it;
            for (long j = 0; j < q; j++) {
                if (!sl_seek(lists[qd[j]], qx[j], &it)) continue;
                long c = 0;
                do sum += (uint64_t)sl_iter_key(&it);
                while (++c < w && sl_next(&it));
            }
            double t3 = now_ns();

            if (hits != expect_hits || sum != expect_sum) {
                fprintf(stderr, "WRONG RESULT in %s on %s, dictionaries of size %zu-%zu\n",
                        SL_VERSION, name, (size_t)1 << b, ((size_t)2 << b) - 1);
                return 1;
            }

            size_t bytes = 0;
            for (size_t j = 0; j < ccount[b]; j++)
                bytes += sl_memory(lists[cfirst[b][j]]);

            size_t from = (size_t)1 << b, to = ((size_t)2 << b) - 1;
            char sz[32];
            if (from == to) snprintf(sz, sizeof(sz), "%lu", (unsigned long)from);
            else snprintf(sz, sizeof(sz), "%lu-%lu", (unsigned long)from, (unsigned long)to);
            double sea = (t2 - t1) / q, wal = (t3 - t2) / q;
            double bpk = (double)bytes / (double)ckeys[b];
            printf("%-12s %4ld %12s %10zu %12zu %10.1f %10.1f %10.1f %10.1f\n", SL_VERSION, r, sz,
                   ccount[b], ckeys[b], ins_ns[b], sea, wal, bpk);
            if (csv)
                fprintf(csv, "%s,%s,%ld,%zu,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f\n", SL_VERSION, name, r,
                        from, to, ccount[b], ckeys[b], ins_ns[b], sea, wal, bpk);
        }

        for (size_t i = 0; i < ndicts; i++) sl_free(lists[i]);
    }

    if (csv) fclose(csv);
    for (int b = 0; b < NCLASS; b++) free(cfirst[b]);
    free(qd); free(qx); free(lists);
    return 0;
}
