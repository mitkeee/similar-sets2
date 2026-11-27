/* dataset.c: experiment 2, skip lists as the dictionaries of a set-trie
 * (Savnik et al., PLOS ONE 2021).
 *   Sigma = {1, ..., sigma}  the alphabet; a line of a file is a set X,
 *                            elements sorted, lines sorted
 *   W = the sets of dataset.txt, stored in the set-trie
 *   T = the sets of dataset.test, the test sets
 * A node is a prefix of a set.  It has a dictionary (key = next element,
 * value = child node) if it is the root or at least two sets go on past it;
 * otherwise the rest of the set is a tail and needs no dictionary.
 * Measured:
 *   insert  build all dictionaries, keys in the order the set-trie gets
 *           them when the sets of W are inserted one by one; ns per key
 *   search  for every X in T, from the root: look up the next element of X,
 *           go to the child it points to, until a tail; ns per lookup
 *           (every lookup needs the result of the one before), test sets in
 *           the order of the file.  With -s, a second pass times the test
 *           sets in groups by their size |X| (1, 2-3, 4-7, ...).
 * Every answer is checked.  One run prints one CSV line:
 *   version,dataset,insert_ns,search_ns
 * and with -s adds one line per group of test sets to file:
 *   version,dataset,size_from,size_to,test_sets,lookups,ns_per_test_set
 * -i prints only a description of the dataset and the test, no timing:
 *   dataset,sigma,sets,test_sets,avg_set_size,dictionaries,largest_dictionary,
 *   lookups_per_test_set,root_lookup_pct,dict_size_per_lookup
 * Usage: data-<version> [-i] [-s file] dataset.txt   (dataset.test next to it) */
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

#define TAIL (-1)                                       /* child without a dictionary */
#define VAL(c) ((sl_val_t)(intptr_t)((c) + 2))          /* child -> value, never NULL */
#define CHILD(v) ((int)((intptr_t)(v) - 2))

static void* xrealloc(void* p, size_t n)
{
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

/* one set per line, numbers separated by spaces, any line length */
typedef struct reader {
    FILE* f;
    char buf[1 << 20];
    size_t len, pos;
} reader;

static reader* open_reader(const char* path)
{
    reader* r = (reader*)xrealloc(NULL, sizeof(reader));
    r->f = fopen(path, "rb");
    if (!r->f) { perror(path); exit(1); }
    r->len = r->pos = 0;
    return r;
}

static void close_reader(reader* r)
{
    fclose(r->f);
    free(r);
}

/* next set into *el; 0 at end of file */
static int read_set(reader* r, int** el, size_t* n, size_t* cap)
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

/* The dictionaries: dictionary i has the keys dkey[doff[i] .. doff[i+1]),
 * sorted, and the child of each key in dchild[]. */
static int*    dkey;
static int*    dchild;
static size_t  nkeys, capkeys;
static size_t* doff;
static int     ndicts, capdicts;

static int add_dict(const int* k, const int* c, size_t n)
{
    if (nkeys + n > capkeys) {
        while (nkeys + n > capkeys) capkeys = capkeys ? capkeys * 2 : 1 << 20;
        dkey = (int*)xrealloc(dkey, capkeys * sizeof(int));
        dchild = (int*)xrealloc(dchild, capkeys * sizeof(int));
    }
    if (ndicts + 2 > capdicts) {
        capdicts = capdicts ? capdicts * 2 : 1 << 16;
        doff = (size_t*)xrealloc(doff, (size_t)capdicts * sizeof(size_t));
    }
    if (ndicts == 0) doff[0] = 0;
    memcpy(dkey + nkeys, k, n * sizeof(int));
    memcpy(dchild + nkeys, c, n * sizeof(int));
    nkeys += n;
    doff[++ndicts] = nkeys;
    return ndicts - 1;
}

/* child of key in dictionary d; TAIL - 1 if the key is not there */
static int child_of(int d, int key)
{
    size_t lo = doff[d], hi = doff[d + 1];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (dkey[mid] < key) lo = mid + 1; else hi = mid;
    }
    return (lo < doff[d + 1] && dkey[lo] == key) ? dchild[lo] : TAIL - 1;
}

/* Pass 1: find the dictionaries.  The nodes on the path of the current set
 * are open; node d (prefix of length d) keeps its keys so far and how many
 * sets go on past it.  When the next set leaves a node, the node is done. */
static int**   open_key;      /* keys of the open node at depth d */
static int**   open_child;    /* their children */
static size_t* nk;
static size_t* capk;
static long*   cnt;         /* sets that go on past depth d */
static size_t  depth_cap;

static void ensure_depth(size_t need)
{
    if (need <= depth_cap) return;
    size_t nc = depth_cap ? depth_cap : 64;
    while (nc < need) nc *= 2;
    open_key = (int**)xrealloc(open_key, nc * sizeof(int*));
    open_child = (int**)xrealloc(open_child, nc * sizeof(int*));
    nk = (size_t*)xrealloc(nk, nc * sizeof(size_t));
    capk = (size_t*)xrealloc(capk, nc * sizeof(size_t));
    cnt = (long*)xrealloc(cnt, nc * sizeof(long));
    for (size_t d = depth_cap; d < nc; d++) {
        open_key[d] = open_child[d] = NULL;
        nk[d] = capk[d] = 0;
        cnt[d] = 0;
    }
    depth_cap = nc;
}

static void add_key(size_t d, int key)
{
    if (nk[d] == capk[d]) {
        capk[d] = capk[d] ? capk[d] * 2 : 8;
        open_key[d] = (int*)xrealloc(open_key[d], capk[d] * sizeof(int));
        open_child[d] = (int*)xrealloc(open_child[d], capk[d] * sizeof(int));
    }
    open_key[d][nk[d]] = key;
    open_child[d][nk[d]] = TAIL;
    nk[d]++;
}

static int close_node(size_t d)
{
    int id = TAIL;
    if ((cnt[d] >= 2 || d == 0) && nk[d] > 0)
        id = add_dict(open_key[d], open_child[d], nk[d]);
    nk[d] = 0;
    cnt[d] = 0;
    if (d > 0) open_child[d - 1][nk[d - 1] - 1] = id;     /* the parent's last key leads here */
    return id;
}

static long n_sets, n_elems, sigma;     /* filled by find_dicts */

static int find_dicts(const char* path)
{
    reader* r = open_reader(path);
    int *cur = NULL, *prev = NULL, root = TAIL;
    size_t k = 0, kp = 0, curcap = 0, prevcap = 0;
    long nsets = 0;
    while (read_set(r, &cur, &k, &curcap)) {
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
        for (size_t d = 0; d <= c; d++) cnt[d]++;
        for (size_t d = c + 1; d <= k; d++) { cnt[d] = d < k; nk[d] = 0; }  /* a set ending here does not count */
        for (size_t d = c; d < k; d++) add_key(d, cur[d]);
        n_elems += (long)k;
        if (cur[k - 1] > sigma) sigma = cur[k - 1];
        int* t = prev; prev = cur; cur = t;
        size_t tc = prevcap; prevcap = curcap; curcap = tc;
        kp = k;
    }
    n_sets = nsets;
    for (size_t d = kp; d > 0; d--) close_node(d);
    if (nsets > 0) root = close_node(0);
    close_reader(r);
    free(cur); free(prev);
    return root;
}

/* Pass 2: the order in which the set-trie gets the keys: set by set, the
 * elements that are new on the path of the set, down to the first tail. */
typedef struct event { int dict, key, child; } event;

static event* insert_order(const char* path, int root, size_t* nev)
{
    event* ev = (event*)xrealloc(NULL, (nkeys + 1) * sizeof(event));
    int* node = (int*)xrealloc(NULL, depth_cap * sizeof(int));   /* node[j]: node at depth j */
    reader* r = open_reader(path);
    int *cur = NULL, *prev = NULL;
    size_t k = 0, kp = 0, curcap = 0, prevcap = 0, n = 0;
    node[0] = root;
    while (read_set(r, &cur, &k, &curcap)) {
        if (k == 0) continue;
        size_t c = 0, m = k < kp ? k : kp;
        while (c < m && cur[c] == prev[c]) c++;
        for (size_t j = c; j < k && node[j] != TAIL; j++) {
            int ch = child_of(node[j], cur[j]);
            if (n == nkeys || ch < TAIL) { fprintf(stderr, "%s: set-trie mismatch\n", path); exit(1); }
            ev[n].dict = node[j];
            ev[n].key = cur[j];
            ev[n].child = ch;
            n++;
            node[j + 1] = ch;
        }
        int* t = prev; prev = cur; cur = t;
        size_t tc = prevcap; prevcap = curcap; curcap = tc;
        kp = k;
    }
    close_reader(r);
    free(cur); free(prev); free(node);
    if (n != nkeys) { fprintf(stderr, "%s: set-trie mismatch\n", path); exit(1); }
    *nev = n;
    return ev;
}

#define NCLASS 40                       /* test sets by size: 1, 2-3, 4-7, ... */

/* one test set X from the root, down to a tail; returns the last node */
static int walk(skiplist** list, int root, const int* x, size_t k, long* lookups)
{
    int d = root;
    for (size_t p = 0; p < k; p++) {
        (*lookups)++;
        sl_val_t v = sl_search(list[d], x[p]);
        if (!v) return -2;
        if (CHILD(v) == TAIL) break;
        d = CHILD(v);
    }
    return d;
}

static int size_class(size_t n)
{
    int b = 0;
    while (n > 1) { n >>= 1; b++; }
    return b;
}

int main(int argc, char** argv)
{
    int info = 0;
    const char *path = NULL, *size_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) info = 1;
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) size_file = argv[++i];
        else if (!path && argv[i][0] != '-') path = argv[i];
        else path = NULL, i = argc;
    }
    size_t len = path ? strlen(path) : 0;
    if (len < 4 || strcmp(path + len - 4, ".txt") != 0) {
        fprintf(stderr, "usage: %s [-i] [-s file] dataset.txt\n", argv[0]);
        return 2;
    }
    char* tpath = (char*)xrealloc(NULL, len + 2);
    memcpy(tpath, path, len - 4);
    strcpy(tpath + len - 4, ".test");
    const char* name = path;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;

#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1);   /* one core */
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    int root = find_dicts(path);
    if (root == TAIL) { fprintf(stderr, "%s: no sets\n", path); return 1; }

    /* T, all sets in one array: set s is tel[toff[s] .. toff[s+1]) */
    int *tel = NULL, *x = NULL;
    size_t* toff = (size_t*)xrealloc(NULL, sizeof(size_t));
    size_t ntel = 0, captel = 0, nt = 0, k = 0, xcap = 0;
    toff[0] = 0;
    reader* r = open_reader(tpath);
    while (read_set(r, &x, &k, &xcap)) {
        if (k == 0) continue;
        if (ntel + k > captel) {
            while (ntel + k > captel) captel = captel ? captel * 2 : 1 << 16;
            tel = (int*)xrealloc(tel, captel * sizeof(int));
        }
        memcpy(tel + ntel, x, k * sizeof(int));
        ntel += k;
        toff = (size_t*)xrealloc(toff, (nt + 2) * sizeof(size_t));
        toff[++nt] = ntel;
    }
    close_reader(r);
    free(x);
    if (nt == 0) { fprintf(stderr, "%s: no test sets\n", tpath); return 1; }

    /* test sets by size, in file order within a group */
    size_t* by_size = (size_t*)xrealloc(NULL, nt * sizeof(size_t));
    size_t start[NCLASS + 1] = {0};
    for (size_t s = 0; s < nt; s++) start[size_class(toff[s + 1] - toff[s]) + 1]++;
    for (int b = 0; b < NCLASS; b++) start[b + 1] += start[b];
    size_t fill[NCLASS];
    memcpy(fill, start, sizeof(fill));
    for (size_t s = 0; s < nt; s++) by_size[fill[size_class(toff[s + 1] - toff[s])]++] = s;

    /* correct answers, with binary search in the sorted dictionaries */
    long expect_lookups[NCLASS] = {0}, in_root = 0, all_lookups = 0;
    uint64_t expect_end = 0;
    double dict_size = 0;
    for (size_t s = 0; s < nt; s++) {
        int d = root, b = size_class(toff[s + 1] - toff[s]);
        for (size_t p = toff[s]; p < toff[s + 1]; p++) {
            expect_lookups[b]++;
            all_lookups++;
            in_root += d == root;
            dict_size += (double)(doff[d + 1] - doff[d]);
            int ch = child_of(d, tel[p]);
            if (ch < TAIL) { d = -2; break; }          /* not found */
            if (ch == TAIL) break;
            d = ch;
        }
        expect_end += (uint64_t)(d + 2);
    }

    if (info) {
        size_t largest = 0;
        for (int d = 0; d < ndicts; d++)
            if (doff[d + 1] - doff[d] > largest) largest = doff[d + 1] - doff[d];
        printf("%s,%ld,%ld,%lu,%.1f,%d,%lu,%.1f,%.1f,%.0f\n", name, sigma, n_sets,
               (unsigned long)nt, (double)n_elems / (double)n_sets, ndicts,
               (unsigned long)largest, (double)all_lookups / (double)nt,
               100.0 * (double)in_root / (double)all_lookups, dict_size / (double)all_lookups);
        free(tel); free(toff); free(by_size); free(tpath);
        return 0;
    }

    size_t nev;
    event* ev = insert_order(path, root, &nev);
    skiplist** list = (skiplist**)calloc((size_t)ndicts, sizeof(skiplist*));
    if (!list) { fprintf(stderr, "out of memory\n"); return 1; }

    double t0 = now_ns();
    for (size_t i = 0; i < nev; i++) {
        skiplist* sl = list[ev[i].dict];
        if (!sl) sl = list[ev[i].dict] = sl_create();
        if (!sl || sl_insert(sl, ev[i].key, VAL(ev[i].child)) < 0) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
    }
    double t1 = now_ns();

    /* search: all test sets, in the order of the file */
    long lookups = 0;
    uint64_t end = 0;
    for (size_t s = 0; s < nt; s++)
        end += (uint64_t)(walk(list, root, tel + toff[s], toff[s + 1] - toff[s], &lookups) + 2);
    double t2 = now_ns();

    /* with -s: once more, every group of test sets of one size timed alone */
    long group_lookups[NCLASS] = {0};
    double ns[NCLASS] = {0};
    uint64_t group_end = expect_end;
    if (size_file) {
        group_end = 0;
        for (int g = 0; g < NCLASS; g++) {
            if (start[g] == start[g + 1]) continue;
            double a = now_ns();
            for (size_t j = start[g]; j < start[g + 1]; j++) {
                size_t s = by_size[j];
                group_end += (uint64_t)(walk(list, root, tel + toff[s], toff[s + 1] - toff[s],
                                             &group_lookups[g]) + 2);
            }
            ns[g] = now_ns() - a;
        }
    }

    for (int i = 0; i < ndicts; i++) sl_free(list[i]);
    free(list); free(ev);
    int ok = lookups == all_lookups && end == expect_end && group_end == expect_end;
    for (int g = 0; size_file && g < NCLASS; g++) ok &= group_lookups[g] == expect_lookups[g];
    if (!ok) {
        fprintf(stderr, "WRONG RESULT: %s on %s\n", SL_VERSION, name);
        return 1;
    }
    printf("%s,%s,%.1f,%.1f\n", SL_VERSION, name, (t1 - t0) / (double)nev,
           (t2 - t1) / (double)lookups);
    if (size_file) {
        FILE* f = fopen(size_file, "a");
        if (!f) { perror(size_file); return 1; }
        for (int g = 0; g < NCLASS; g++) {
            size_t sets = start[g + 1] - start[g];
            if (!sets) continue;
            fprintf(f, "%s,%s,%lu,%lu,%lu,%ld,%.1f\n", SL_VERSION, name, 1UL << g,
                    (2UL << g) - 1, (unsigned long)sets, group_lookups[g], ns[g] / (double)sets);
        }
        fclose(f);
    }
    free(tel); free(toff); free(by_size); free(tpath);
    return 0;
}
