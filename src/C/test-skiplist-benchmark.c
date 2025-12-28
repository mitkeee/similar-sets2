/*-----------------------------------------------------------------------------
 * Skip List Implementation Comparison Benchmark
 * Issue #2: Compare with reference skip list implementation
 * 
 * Compares our cskiplist (cache-friendly block skip list) with the reference
 * probabilistic skip list from https://github.com/begeekmyfriend/skiplist
 * 
 * Metrics compared:
 * - Sequential insertion performance
 * - Random insertion performance  
 * - Search/lookup performance
 * - Delete performance
 * - Memory efficiency
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#define random() rand()
#define srandom(x) srand(x)
#else
#include <sys/resource.h>
#endif

/*-----------------------------------------------------------------------------
 * Include our cskiplist implementation
 *----------------------------------------------------------------------------*/
#include "cskiplist.h"

/*-----------------------------------------------------------------------------
 * Include reference skiplist (header-only implementation)
 * Path relative to src/C - assumes reference is copied or linked
 *----------------------------------------------------------------------------*/

/* Reference skiplist inline (from begeekmyfriend/skiplist) */
/* We embed a modified version to avoid path issues */

struct sk_link {
    struct sk_link *prev, *next;
};

static inline void ref_list_init(struct sk_link *link) {
    link->prev = link;
    link->next = link;
}

static inline void ref_list_add(struct sk_link *link, struct sk_link *prev, struct sk_link *next) {
    link->next = next;
    link->prev = prev;
    next->prev = link;
    prev->next = link;
}

static inline void ref_list_del(struct sk_link *prev, struct sk_link *next) {
    prev->next = next;
    next->prev = prev;
}

static inline void ref_list_remove(struct sk_link *link) {
    ref_list_del(link->prev, link->next);
    ref_list_init(link);
}

static inline int ref_list_empty(struct sk_link *link) {
    return link->next == link;
}

#define ref_list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))

#define REF_MAX_LEVEL 32

struct ref_skiplist {
    int level;
    int count;
    struct sk_link head[REF_MAX_LEVEL];
};

struct ref_skipnode {
    int key;
    int value;
    int node_level;  /* track node's level for deletion */
    struct sk_link link[0];
};

static struct ref_skipnode *ref_skipnode_new(int level, int key, int value) {
    struct ref_skipnode *node = (struct ref_skipnode *)malloc(
        sizeof(struct ref_skipnode) + level * sizeof(struct sk_link));
    if (node != NULL) {
        node->key = key;
        node->value = value;
        node->node_level = level;
    }
    return node;
}

static void ref_skipnode_delete(struct ref_skipnode *node) {
    free(node);
}

static struct ref_skiplist *ref_skiplist_new(void) {
    int i;
    struct ref_skiplist *list = (struct ref_skiplist *)malloc(sizeof(struct ref_skiplist));
    if (list != NULL) {
        list->level = 1;
        list->count = 0;
        for (i = 0; i < REF_MAX_LEVEL; i++) {
            ref_list_init(&list->head[i]);
        }
    }
    return list;
}

static void ref_skiplist_delete(struct ref_skiplist *list) {
    struct sk_link *n;
    struct sk_link *pos = list->head[0].next;
    while (pos != &list->head[0]) {
        n = pos->next;
        struct ref_skipnode *node = ref_list_entry(pos, struct ref_skipnode, link[0]);
        ref_skipnode_delete(node);
        pos = n;
    }
    free(list);
}

static int ref_random_level(void) {
    int level = 1;
    while ((random() & 0xFFFF) < (0xFFFF >> 2)) {  /* p = 0.25 */
        level++;
    }
    return level > REF_MAX_LEVEL ? REF_MAX_LEVEL : level;
}

static struct ref_skipnode *ref_skiplist_search(struct ref_skiplist *list, int key) {
    int i = list->level - 1;
    struct sk_link *pos = &list->head[i];
    struct sk_link *end = &list->head[i];
    struct ref_skipnode *node = NULL;

    for (; i >= 0; i--) {
        pos = pos->next;
        while (pos != end) {
            node = ref_list_entry(pos, struct ref_skipnode, link[i]);
            if (node->key >= key) {
                end = &node->link[i];
                break;
            }
            pos = pos->next;
        }
        if (node && node->key == key) {
            return node;
        }
        pos = end->prev;
        if (i > 0) {
            pos = (struct sk_link *)((char *)pos - sizeof(struct sk_link));
            end = (struct sk_link *)((char *)end - sizeof(struct sk_link));
        }
    }
    return NULL;
}

static struct ref_skipnode *ref_skiplist_insert(struct ref_skiplist *list, int key, int value) {
    int level = ref_random_level();
    if (level > list->level) {
        list->level = level;
    }

    struct ref_skipnode *node = ref_skipnode_new(level, key, value);
    if (node != NULL) {
        int i = list->level - 1;
        struct sk_link *pos = &list->head[i];
        struct sk_link *end = &list->head[i];

        for (; i >= 0; i--) {
            pos = pos->next;
            while (pos != end) {
                struct ref_skipnode *nd = ref_list_entry(pos, struct ref_skipnode, link[i]);
                if (nd->key >= key) {
                    end = &nd->link[i];
                    break;
                }
                pos = pos->next;
            }
            pos = end->prev;
            if (i < level) {
                ref_list_add(&node->link[i], pos, end);
            }
            if (i > 0) {
                pos = (struct sk_link *)((char *)pos - sizeof(struct sk_link));
                end = (struct sk_link *)((char *)end - sizeof(struct sk_link));
            }
        }
        list->count++;
    }
    return node;
}

static void ref_skiplist_remove(struct ref_skiplist *list, int key) {
    int i = list->level - 1;
    struct sk_link *pos = &list->head[i];
    struct sk_link *end = &list->head[i];
    struct sk_link *n;

    for (; i >= 0; i--) {
        pos = pos->next;
        while (pos != end) {
            n = pos->next;
            struct ref_skipnode *node = ref_list_entry(pos, struct ref_skipnode, link[i]);
            if (node->key > key) {
                end = &node->link[i];
                break;
            } else if (node->key == key) {
                /* Remove at all levels this node participates in */
                for (int j = 0; j < node->node_level; j++) {
                    ref_list_remove(&node->link[j]);
                    if (ref_list_empty(&list->head[j])) {
                        list->level--;
                    }
                }
                ref_skipnode_delete(node);
                list->count--;
                return;
            }
            pos = n;
        }
        pos = end->prev;
        if (i > 0) {
            pos = (struct sk_link *)((char *)pos - sizeof(struct sk_link));
            end = (struct sk_link *)((char *)end - sizeof(struct sk_link));
        }
    }
}

/*-----------------------------------------------------------------------------
 * Benchmark utilities
 *----------------------------------------------------------------------------*/

typedef struct {
    double insert_ms;
    double search_ms;
    double delete_ms;
    double rebuild_ms;  /* only for cskiplist */
    int count;
    int verified;
    size_t mem_delta_kb;
    double inserts_per_sec;
    double searches_per_sec;
    double deletes_per_sec;
} benchmark_result;

static double get_time_ms(void) {
    return (double)clock() / CLOCKS_PER_SEC * 1000.0;
}

static size_t get_memory_kb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024;
    }
    return 0;
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
#endif
}

/* Simple deterministic RNG */
static uint32_t bench_rng_state = 12345;
static uint32_t bench_rand(void) {
    bench_rng_state = bench_rng_state * 1103515245 + 12345;
    return (bench_rng_state >> 16) & 0x7FFF;
}
static void bench_seed(uint32_t seed) {
    bench_rng_state = seed;
}

static void shuffle(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = bench_rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/*-----------------------------------------------------------------------------
 * Benchmark: Our cskiplist (cache-friendly block skip list)
 *----------------------------------------------------------------------------*/

static benchmark_result bench_cskiplist_sequential(int n) {
    benchmark_result r = {0};
    size_t mem_before = get_memory_kb();
    
    cskiplist *sl = csl_create();
    if (!sl) return r;
    
    /* Insert sequential keys */
    double t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1));
    }
    r.insert_ms = get_time_ms() - t0;
    
    /* Rebuild skip pointers */
    t0 = get_time_ms();
    csl_rebuild_skips(sl);
    r.rebuild_ms = get_time_ms() - t0;
    
    r.count = (int)sl->size;
    r.mem_delta_kb = get_memory_kb() - mem_before;
    
    /* Search all keys */
    t0 = get_time_ms();
    int found = 0;
    for (int i = 0; i < n; i++) {
        void *val = csl_search(sl, i);
        if (val && (intptr_t)val == i + 1) found++;
    }
    r.search_ms = get_time_ms() - t0;
    r.verified = found;
    
    /* Delete all keys */
    t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        csl_delete(sl, i, NULL);
    }
    r.delete_ms = get_time_ms() - t0;
    
    csl_free(sl, NULL);
    
    r.inserts_per_sec = n / (r.insert_ms / 1000.0);
    r.searches_per_sec = n / (r.search_ms / 1000.0);
    r.deletes_per_sec = n / (r.delete_ms / 1000.0);
    
    return r;
}

static benchmark_result bench_cskiplist_random(int n, int *keys) {
    benchmark_result r = {0};
    size_t mem_before = get_memory_kb();
    
    cskiplist *sl = csl_create();
    if (!sl) return r;
    
    /* Insert random keys */
    double t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        csl_insert(sl, keys[i], (void*)(intptr_t)(keys[i] + 1));
    }
    r.insert_ms = get_time_ms() - t0;
    
    /* Rebuild skip pointers */
    t0 = get_time_ms();
    csl_rebuild_skips(sl);
    r.rebuild_ms = get_time_ms() - t0;
    
    r.count = (int)sl->size;
    r.mem_delta_kb = get_memory_kb() - mem_before;
    
    /* Search all keys */
    t0 = get_time_ms();
    int found = 0;
    for (int i = 0; i < n; i++) {
        void *val = csl_search(sl, keys[i]);
        if (val && (intptr_t)val == keys[i] + 1) found++;
    }
    r.search_ms = get_time_ms() - t0;
    r.verified = found;
    
    /* Delete all keys */
    t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        csl_delete(sl, keys[i], NULL);
    }
    r.delete_ms = get_time_ms() - t0;
    
    csl_free(sl, NULL);
    
    r.inserts_per_sec = n / (r.insert_ms / 1000.0);
    r.searches_per_sec = n / (r.search_ms / 1000.0);
    r.deletes_per_sec = n / (r.delete_ms / 1000.0);
    
    return r;
}

/*-----------------------------------------------------------------------------
 * Benchmark: Reference probabilistic skip list
 *----------------------------------------------------------------------------*/

static benchmark_result bench_ref_skiplist_sequential(int n) {
    benchmark_result r = {0};
    size_t mem_before = get_memory_kb();
    
    srandom(42);  /* deterministic random levels */
    struct ref_skiplist *sl = ref_skiplist_new();
    if (!sl) return r;
    
    /* Insert sequential keys */
    double t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        ref_skiplist_insert(sl, i, i + 1);
    }
    r.insert_ms = get_time_ms() - t0;
    r.rebuild_ms = 0;  /* no rebuild needed for probabilistic skiplist */
    
    r.count = sl->count;
    r.mem_delta_kb = get_memory_kb() - mem_before;
    
    /* Search all keys */
    t0 = get_time_ms();
    int found = 0;
    for (int i = 0; i < n; i++) {
        struct ref_skipnode *node = ref_skiplist_search(sl, i);
        if (node && node->value == i + 1) found++;
    }
    r.search_ms = get_time_ms() - t0;
    r.verified = found;
    
    /* Delete all keys */
    t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        ref_skiplist_remove(sl, i);
    }
    r.delete_ms = get_time_ms() - t0;
    
    ref_skiplist_delete(sl);
    
    r.inserts_per_sec = n / (r.insert_ms / 1000.0);
    r.searches_per_sec = n / (r.search_ms / 1000.0);
    r.deletes_per_sec = n / (r.delete_ms / 1000.0);
    
    return r;
}

static benchmark_result bench_ref_skiplist_random(int n, int *keys) {
    benchmark_result r = {0};
    size_t mem_before = get_memory_kb();
    
    srandom(42);
    struct ref_skiplist *sl = ref_skiplist_new();
    if (!sl) return r;
    
    /* Insert random keys */
    double t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        ref_skiplist_insert(sl, keys[i], keys[i] + 1);
    }
    r.insert_ms = get_time_ms() - t0;
    r.rebuild_ms = 0;
    
    r.count = sl->count;
    r.mem_delta_kb = get_memory_kb() - mem_before;
    
    /* Search all keys */
    t0 = get_time_ms();
    int found = 0;
    for (int i = 0; i < n; i++) {
        struct ref_skipnode *node = ref_skiplist_search(sl, keys[i]);
        if (node && node->value == keys[i] + 1) found++;
    }
    r.search_ms = get_time_ms() - t0;
    r.verified = found;
    
    /* Delete all keys */
    t0 = get_time_ms();
    for (int i = 0; i < n; i++) {
        ref_skiplist_remove(sl, keys[i]);
    }
    r.delete_ms = get_time_ms() - t0;
    
    ref_skiplist_delete(sl);
    
    r.inserts_per_sec = n / (r.insert_ms / 1000.0);
    r.searches_per_sec = n / (r.search_ms / 1000.0);
    r.deletes_per_sec = n / (r.delete_ms / 1000.0);
    
    return r;
}

/*-----------------------------------------------------------------------------
 * Report generation
 *----------------------------------------------------------------------------*/

static void print_header(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("  SKIP LIST IMPLEMENTATION COMPARISON BENCHMARK\n");
    printf("  Issue #2: Compare with reference skip list implementation\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Implementations compared:\n");
    printf("  [CSL] cskiplist   - Cache-friendly block skip list (deterministic, power-of-2)\n");
    printf("  [REF] ref_skiplist - Classic probabilistic skip list (begeekmyfriend/skiplist)\n");
    printf("\n");
    printf("Key differences:\n");
    printf("  - CSL uses blocks of %d items for cache efficiency\n", CSL_BLOCK_CAP);
    printf("  - CSL uses deterministic skip pointers (rebuilt after batch inserts)\n");
    printf("  - REF uses per-node probabilistic levels (no rebuild needed)\n");
    printf("  - CSL optimized for sequential/batch inserts, REF for random inserts\n");
    printf("\n");
}

static void print_comparison(const char *test_name, int n, 
                             benchmark_result *csl, benchmark_result *ref) {
    printf("--------------------------------------------------------------------------------\n");
    printf("  %s (n = %d)\n", test_name, n);
    printf("--------------------------------------------------------------------------------\n");
    printf("\n");
    printf("                          cskiplist        ref_skiplist     Winner    Ratio\n");
    printf("  -------------------------------------------------------------------------\n");
    
    /* Insert comparison */
    const char *insert_winner = csl->insert_ms < ref->insert_ms ? "CSL" : "REF";
    double insert_ratio = csl->insert_ms < ref->insert_ms ? 
                          ref->insert_ms / csl->insert_ms : 
                          csl->insert_ms / ref->insert_ms;
    printf("  Insert time:        %8.2f ms      %8.2f ms       %s      %.2fx\n",
           csl->insert_ms, ref->insert_ms, insert_winner, insert_ratio);
    printf("  Insert rate:        %8.0f /sec    %8.0f /sec\n",
           csl->inserts_per_sec, ref->inserts_per_sec);
    
    if (csl->rebuild_ms > 0) {
        printf("  Rebuild time:       %8.2f ms       (N/A)\n", csl->rebuild_ms);
        printf("  Insert+Rebuild:     %8.2f ms      %8.2f ms\n",
               csl->insert_ms + csl->rebuild_ms, ref->insert_ms);
    }
    
    /* Search comparison */
    const char *search_winner = csl->search_ms < ref->search_ms ? "CSL" : "REF";
    double search_ratio = csl->search_ms < ref->search_ms ? 
                          ref->search_ms / csl->search_ms : 
                          csl->search_ms / ref->search_ms;
    printf("  Search time:        %8.2f ms      %8.2f ms       %s      %.2fx\n",
           csl->search_ms, ref->search_ms, search_winner, search_ratio);
    printf("  Search rate:        %8.0f /sec    %8.0f /sec\n",
           csl->searches_per_sec, ref->searches_per_sec);
    
    /* Delete comparison */
    const char *delete_winner = csl->delete_ms < ref->delete_ms ? "CSL" : "REF";
    double delete_ratio = csl->delete_ms < ref->delete_ms ? 
                          ref->delete_ms / csl->delete_ms : 
                          csl->delete_ms / ref->delete_ms;
    printf("  Delete time:        %8.2f ms      %8.2f ms       %s      %.2fx\n",
           csl->delete_ms, ref->delete_ms, delete_winner, delete_ratio);
    printf("  Delete rate:        %8.0f /sec    %8.0f /sec\n",
           csl->deletes_per_sec, ref->deletes_per_sec);
    
    /* Memory comparison */
    printf("  Memory delta:       %8zu KB      %8zu KB\n", 
           csl->mem_delta_kb, ref->mem_delta_kb);
    
    /* Verification */
    printf("  Verified:           %8d/%d      %8d/%d\n",
           csl->verified, n, ref->verified, n);
    printf("\n");
}

static void print_summary(benchmark_result *csl_seq, benchmark_result *ref_seq,
                          benchmark_result *csl_rnd, benchmark_result *ref_rnd,
                          int n) {
    printf("================================================================================\n");
    printf("  SUMMARY & ANALYSIS\n");
    printf("================================================================================\n");
    printf("\n");
    
    /* Count wins */
    int csl_wins = 0, ref_wins = 0;
    
    /* Sequential */
    if (csl_seq->insert_ms < ref_seq->insert_ms) csl_wins++; else ref_wins++;
    if (csl_seq->search_ms < ref_seq->search_ms) csl_wins++; else ref_wins++;
    if (csl_seq->delete_ms < ref_seq->delete_ms) csl_wins++; else ref_wins++;
    
    /* Random */
    if (csl_rnd->insert_ms < ref_rnd->insert_ms) csl_wins++; else ref_wins++;
    if (csl_rnd->search_ms < ref_rnd->search_ms) csl_wins++; else ref_wins++;
    if (csl_rnd->delete_ms < ref_rnd->delete_ms) csl_wins++; else ref_wins++;
    
    printf("Performance wins: CSL = %d, REF = %d\n", csl_wins, ref_wins);
    printf("\n");
    printf("Analysis:\n");
    printf("\n");
    
    /* Sequential insert analysis */
    double seq_insert_speedup = ref_seq->insert_ms / csl_seq->insert_ms;
    if (seq_insert_speedup > 1.0) {
        printf("  * Sequential insert: cskiplist is %.1fx FASTER\n", seq_insert_speedup);
        printf("    -> Block-based storage reduces allocation overhead\n");
    } else {
        printf("  * Sequential insert: ref_skiplist is %.1fx faster\n", 1.0/seq_insert_speedup);
    }
    
    /* Search analysis */
    double seq_search_speedup = ref_seq->search_ms / csl_seq->search_ms;
    if (seq_search_speedup > 1.0) {
        printf("  * Sequential search: cskiplist is %.1fx FASTER\n", seq_search_speedup);
        printf("    -> Cache-friendly blocks improve locality\n");
    } else {
        printf("  * Sequential search: ref_skiplist is %.1fx faster\n", 1.0/seq_search_speedup);
    }
    
    /* Random insert analysis */
    double rnd_insert_speedup = ref_rnd->insert_ms / csl_rnd->insert_ms;
    if (rnd_insert_speedup > 1.0) {
        printf("  * Random insert: cskiplist is %.1fx FASTER\n", rnd_insert_speedup);
    } else {
        printf("  * Random insert: ref_skiplist is %.1fx faster\n", 1.0/rnd_insert_speedup);
        printf("    -> Probabilistic structure adapts better to random order\n");
    }
    
    printf("\n");
    printf("Recommendations:\n");
    printf("  - Use cskiplist for sequential/batch workloads (e.g., sorted data loading)\n");
    printf("  - Consider ref_skiplist style for highly random insert patterns\n");
    printf("  - cskiplist's deterministic skips provide consistent search performance\n");
    printf("  - Block size (%d) can be tuned for specific cache line sizes\n", CSL_BLOCK_CAP);
    printf("\n");
}

/*-----------------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------------*/

int main(int argc, char **argv) {
    int n = 100000;  /* Default: 100K */
    
    if (argc > 1) {
        if (strcmp(argv[1], "small") == 0) n = 10000;
        else if (strcmp(argv[1], "medium") == 0) n = 100000;
        else if (strcmp(argv[1], "large") == 0) n = 500000;
        else if (strcmp(argv[1], "xlarge") == 0) n = 1000000;
        else n = atoi(argv[1]);
    }
    
    print_header();
    printf("Test size: %d keys\n", n);
    printf("Usage: %s [small|medium|large|xlarge|<number>]\n", argv[0]);
    printf("\n");
    
    /* Prepare random keys array */
    int *keys = (int *)malloc(n * sizeof(int));
    if (!keys) {
        printf("ERROR: Failed to allocate key array\n");
        return 1;
    }
    for (int i = 0; i < n; i++) keys[i] = i;
    bench_seed(42);
    shuffle(keys, n);
    
    /* Run benchmarks */
    printf("Running benchmarks...\n\n");
    
    /* Sequential benchmarks */
    printf("  [1/4] cskiplist sequential...\n");
    benchmark_result csl_seq = bench_cskiplist_sequential(n);
    
    printf("  [2/4] ref_skiplist sequential...\n");
    benchmark_result ref_seq = bench_ref_skiplist_sequential(n);
    
    /* Random benchmarks */
    printf("  [3/4] cskiplist random...\n");
    benchmark_result csl_rnd = bench_cskiplist_random(n, keys);
    
    printf("  [4/4] ref_skiplist random...\n");
    benchmark_result ref_rnd = bench_ref_skiplist_random(n, keys);
    
    printf("\n");
    
    /* Print results */
    print_comparison("SEQUENTIAL INSERT TEST", n, &csl_seq, &ref_seq);
    print_comparison("RANDOM INSERT TEST", n, &csl_rnd, &ref_rnd);
    print_summary(&csl_seq, &ref_seq, &csl_rnd, &ref_rnd, n);
    
    /* Verification check */
    int all_verified = (csl_seq.verified == n && ref_seq.verified == n &&
                        csl_rnd.verified == n && ref_rnd.verified == n);
    
    printf("================================================================================\n");
    printf("  RESULT: %s\n", all_verified ? "ALL TESTS PASSED" : "VERIFICATION FAILED");
    printf("================================================================================\n");
    
    free(keys);
    return all_verified ? 0 : 1;
}
