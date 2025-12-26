/*-----------------------------------------------------------------------------
 * Large-scale test for cskiplist with 1M+ keys
 * Issue #1: Test cskiplist with 1M+ keys
 * 
 * Tests:
 * - Sequential insert of 1M+ keys
 * - Random insert of 1M+ keys  
 * - Search correctness verification
 * - Delete operations at scale
 * - Memory usage estimation
 * - Performance timing
 *----------------------------------------------------------------------------*/
#include "cskiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

/* Configuration */
#define TEST_SIZE_SMALL   100000    /* 100K for quick validation */
#define TEST_SIZE_MEDIUM  500000    /* 500K medium test */
#define TEST_SIZE_LARGE   1000000   /* 1M full test */
#define TEST_SIZE_XLARGE  2000000   /* 2M stress test */

/* Utility: Get current time in milliseconds */
static double get_time_ms(void) {
    return (double)clock() / CLOCKS_PER_SEC * 1000.0;
}

/* Utility: Get memory usage (platform-specific) */
static size_t get_memory_usage_kb(void) {
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

/* Utility: Simple LCG random number generator (deterministic) */
static uint32_t rng_state = 12345;
static uint32_t fast_rand(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}
static void seed_rand(uint32_t seed) {
    rng_state = seed;
}

/* Utility: Shuffle array (Fisher-Yates) */
static void shuffle_array(int* arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = fast_rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/* Print separator */
static void print_separator(const char* title) {
    printf("\n");
    printf("================================================================\n");
    printf("  %s\n", title);
    printf("================================================================\n");
}

/* Print result line */
static void print_result(const char* name, int passed) {
    printf("  [%s] %s\n", passed ? "PASS" : "FAIL", name);
}

/*-----------------------------------------------------------------------------
 * TEST 1: Sequential Insert Performance
 *----------------------------------------------------------------------------*/
int test_sequential_insert(int n) {
    print_separator("TEST 1: Sequential Insert");
    printf("  Inserting %d keys sequentially...\n", n);
    
    size_t mem_before = get_memory_usage_kb();
    double t_start = get_time_ms();
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert keys 0 to n-1 */
    for (int i = 0; i < n; i++) {
        int result = csl_insert(sl, i, (void*)(intptr_t)(i + 1));
        if (result < 0) {
            printf("  ERROR: Insert failed at key %d\n", i);
            csl_free(sl, NULL);
            return 0;
        }
        
        /* Progress indicator */
        if ((i + 1) % 100000 == 0) {
            printf("    ... inserted %d keys\n", i + 1);
        }
    }
    
    double t_insert = get_time_ms() - t_start;
    
    /* Rebuild skip pointers for optimal search */
    t_start = get_time_ms();
    csl_rebuild_skips(sl);
    double t_rebuild = get_time_ms() - t_start;
    
    size_t mem_after = get_memory_usage_kb();
    
    /* Verify */
    printf("  Verifying all keys...\n");
    t_start = get_time_ms();
    int verified = 0;
    for (int i = 0; i < n; i++) {
        void* val = csl_search(sl, i);
        if (val && (intptr_t)val == i + 1) {
            verified++;
        }
    }
    double t_search = get_time_ms() - t_start;
    
    /* Results */
    printf("\n  Results:\n");
    printf("    Keys inserted:    %zu\n", sl->size);
    printf("    Blocks created:   %zu\n", sl->nblocks);
    printf("    Block splits:     %zu\n", sl->stat_splits);
    printf("    Insert time:      %.2f ms (%.0f inserts/sec)\n", 
           t_insert, n / (t_insert / 1000.0));
    printf("    Rebuild time:     %.2f ms\n", t_rebuild);
    printf("    Search time:      %.2f ms (%.0f searches/sec)\n", 
           t_search, n / (t_search / 1000.0));
    printf("    Memory delta:     %zu KB\n", mem_after - mem_before);
    printf("    Keys verified:    %d/%d\n", verified, n);
    
    int passed = (verified == n && (int)sl->size == n);
    print_result("Sequential Insert", passed);
    
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 2: Random Insert Performance  
 *----------------------------------------------------------------------------*/
int test_random_insert(int n) {
    print_separator("TEST 2: Random Insert");
    printf("  Inserting %d keys in random order...\n", n);
    
    /* Create shuffled array of keys */
    int* keys = (int*)malloc(n * sizeof(int));
    if (!keys) {
        printf("  ERROR: Failed to allocate key array\n");
        return 0;
    }
    
    for (int i = 0; i < n; i++) keys[i] = i;
    seed_rand(42);  /* deterministic shuffle */
    shuffle_array(keys, n);
    
    size_t mem_before = get_memory_usage_kb();
    double t_start = get_time_ms();
    
    cskiplist* sl = csl_create();
    if (!sl) {
        free(keys);
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert in random order */
    for (int i = 0; i < n; i++) {
        int result = csl_insert(sl, keys[i], (void*)(intptr_t)(keys[i] + 1));
        if (result < 0) {
            printf("  ERROR: Insert failed at key %d\n", keys[i]);
            csl_free(sl, NULL);
            free(keys);
            return 0;
        }
        
        if ((i + 1) % 100000 == 0) {
            printf("    ... inserted %d keys\n", i + 1);
        }
    }
    
    double t_insert = get_time_ms() - t_start;
    
    /* Rebuild */
    t_start = get_time_ms();
    csl_rebuild_skips(sl);
    double t_rebuild = get_time_ms() - t_start;
    
    size_t mem_after = get_memory_usage_kb();
    
    /* Verify all keys exist */
    printf("  Verifying all keys...\n");
    t_start = get_time_ms();
    int verified = 0;
    for (int i = 0; i < n; i++) {
        void* val = csl_search(sl, i);
        if (val && (intptr_t)val == i + 1) {
            verified++;
        }
    }
    double t_search = get_time_ms() - t_start;
    
    /* Results */
    printf("\n  Results:\n");
    printf("    Keys inserted:    %zu\n", sl->size);
    printf("    Blocks created:   %zu\n", sl->nblocks);
    printf("    Block splits:     %zu\n", sl->stat_splits);
    printf("    Insert time:      %.2f ms (%.0f inserts/sec)\n", 
           t_insert, n / (t_insert / 1000.0));
    printf("    Rebuild time:     %.2f ms\n", t_rebuild);
    printf("    Search time:      %.2f ms (%.0f searches/sec)\n", 
           t_search, n / (t_search / 1000.0));
    printf("    Memory delta:     %zu KB\n", mem_after - mem_before);
    printf("    Keys verified:    %d/%d\n", verified, n);
    
    int passed = (verified == n && (int)sl->size == n);
    print_result("Random Insert", passed);
    
    csl_free(sl, NULL);
    free(keys);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 3: Delete Operations at Scale
 *----------------------------------------------------------------------------*/
int test_delete_large(int n) {
    print_separator("TEST 3: Delete Operations");
    printf("  Testing delete on %d keys...\n", n);
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert all keys */
    printf("  Inserting %d keys...\n", n);
    for (int i = 0; i < n; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1));
    }
    csl_rebuild_skips(sl);
    printf("    Initial size: %zu\n", sl->size);
    
    /* Delete every 3rd key */
    printf("  Deleting every 3rd key...\n");
    double t_start = get_time_ms();
    int deleted = 0;
    for (int i = 0; i < n; i += 3) {
        if (csl_delete(sl, i, NULL)) deleted++;
    }
    double t_delete = get_time_ms() - t_start;
    
    printf("    Deleted: %d keys in %.2f ms\n", deleted, t_delete);
    printf("    Remaining: %zu keys\n", sl->size);
    
    /* Verify deleted keys are gone */
    printf("  Verifying deleted keys are gone...\n");
    int gone = 0;
    for (int i = 0; i < n; i += 3) {
        if (!csl_search(sl, i)) gone++;
    }
    
    /* Verify remaining keys exist */
    printf("  Verifying remaining keys exist...\n");
    int remaining = 0;
    for (int i = 0; i < n; i++) {
        if (i % 3 != 0) {
            void* val = csl_search(sl, i);
            if (val && (intptr_t)val == i + 1) remaining++;
        }
    }
    
    int expected_remaining = n - (n + 2) / 3;
    
    printf("\n  Results:\n");
    printf("    Deleted confirmed:   %d/%d\n", gone, deleted);
    printf("    Remaining verified:  %d/%d\n", remaining, expected_remaining);
    printf("    Delete stats:        %zu\n", sl->stat_deletes);
    
    int passed = (gone == deleted && remaining == expected_remaining);
    print_result("Delete Operations", passed);
    
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 4: Iteration Correctness
 *----------------------------------------------------------------------------*/
int test_iteration_large(int n) {
    print_separator("TEST 4: Iteration Correctness");
    printf("  Testing iteration on %d keys...\n", n);
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert keys with gaps (only even numbers) */
    printf("  Inserting %d even keys...\n", n / 2);
    for (int i = 0; i < n; i += 2) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1));
    }
    csl_rebuild_skips(sl);
    
    /* Forward iteration */
    printf("  Testing forward iteration...\n");
    double t_start = get_time_ms();
    int count = 0;
    int last_key = -1;
    int order_ok = 1;
    
    csl_iter it;
    if (csl_iter_first(sl, &it)) {
        do {
            csl_kv* kv = csl_iter_get(&it);
            if (kv) {
                if (kv->key <= last_key) order_ok = 0;
                last_key = kv->key;
                count++;
            }
        } while (csl_iter_next(&it));
    }
    double t_iter = get_time_ms() - t_start;
    
    printf("    Forward: %d items in %.2f ms, order_ok=%d\n", count, t_iter, order_ok);
    
    /* Backward iteration (from the end) */
    printf("  Testing backward iteration...\n");
    t_start = get_time_ms();
    int rev_count = 0;
    last_key = INT32_MAX;
    int rev_order_ok = 1;
    
    /* Find the last element by iterating forward and keeping track */
    csl_iter last_it;
    last_it.b = NULL;
    last_it.idx = -1;
    
    if (csl_iter_first(sl, &it)) {
        do {
            last_it = it;  /* Save current position */
        } while (csl_iter_next(&it));
        
        /* Now last_it points to the last valid element */
        it = last_it;
        
        /* Count backwards */
        do {
            csl_kv* kv = csl_iter_get(&it);
            if (kv) {
                if (kv->key >= last_key) rev_order_ok = 0;
                last_key = kv->key;
                rev_count++;
            }
        } while (csl_iter_prev(sl, &it));
    }
    double t_rev = get_time_ms() - t_start;
    
    printf("    Backward: %d items in %.2f ms, order_ok=%d\n", rev_count, t_rev, rev_order_ok);
    
    int passed = (count == (int)sl->size && rev_count == (int)sl->size && 
                  order_ok && rev_order_ok);
    print_result("Iteration Correctness", passed);
    
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 5: Search Performance with Skip Pointers
 *----------------------------------------------------------------------------*/
int test_search_performance(int n) {
    print_separator("TEST 5: Search Performance (Skip Pointers)");
    printf("  Comparing search with/without skip pointers...\n", n);
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert keys */
    printf("  Inserting %d keys...\n", n);
    for (int i = 0; i < n; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1));
    }
    
    /* Search WITHOUT skip pointers (level=0) */
    sl->level = 0;  /* Force level-0 traversal */
    printf("  Searching WITHOUT skip pointers...\n");
    
    double t_start = get_time_ms();
    int found_no_skip = 0;
    for (int i = 0; i < n; i += 100) {  /* Sample every 100th key */
        if (csl_search(sl, i)) found_no_skip++;
    }
    double t_no_skip = get_time_ms() - t_start;
    
    /* Rebuild skip pointers */
    printf("  Rebuilding skip pointers...\n");
    csl_rebuild_skips(sl);
    
    /* Search WITH skip pointers */
    printf("  Searching WITH skip pointers...\n");
    t_start = get_time_ms();
    int found_skip = 0;
    for (int i = 0; i < n; i += 100) {
        if (csl_search(sl, i)) found_skip++;
    }
    double t_skip = get_time_ms() - t_start;
    
    int num_searches = (n + 99) / 100;
    double speedup = t_no_skip / t_skip;
    
    printf("\n  Results:\n");
    printf("    Searches performed: %d\n", num_searches);
    printf("    Without skips:      %.2f ms\n", t_no_skip);
    printf("    With skips:         %.2f ms\n", t_skip);
    printf("    Speedup:            %.2fx\n", speedup);
    printf("    Skip levels:        %d\n", sl->level);
    
    int passed = (found_no_skip == num_searches && found_skip == num_searches);
    print_result("Search Performance", passed);
    
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 6: Update Existing Keys
 *----------------------------------------------------------------------------*/
int test_update_large(int n) {
    print_separator("TEST 6: Update Existing Keys");
    printf("  Testing update on %d keys...\n", n);
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    /* Insert keys */
    printf("  Inserting %d keys...\n", n);
    for (int i = 0; i < n; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1));
    }
    size_t initial_inserts = sl->stat_inserts;
    
    /* Update every 10th key */
    printf("  Updating every 10th key...\n");
    double t_start = get_time_ms();
    for (int i = 0; i < n; i += 10) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 10000));
    }
    double t_update = get_time_ms() - t_start;
    
    /* Verify updates */
    printf("  Verifying updates...\n");
    int updated_ok = 0;
    int not_updated_ok = 0;
    
    for (int i = 0; i < n; i++) {
        void* val = csl_search(sl, i);
        if (i % 10 == 0) {
            if (val && (intptr_t)val == i + 10000) updated_ok++;
        } else {
            if (val && (intptr_t)val == i + 1) not_updated_ok++;
        }
    }
    
    int num_updates = (n + 9) / 10;
    int num_not_updated = n - num_updates;
    
    printf("\n  Results:\n");
    printf("    Size unchanged:      %zu (expected %d)\n", sl->size, n);
    printf("    Update time:         %.2f ms\n", t_update);
    printf("    Updates verified:    %d/%d\n", updated_ok, num_updates);
    printf("    Non-updates OK:      %d/%d\n", not_updated_ok, num_not_updated);
    printf("    stat_inserts delta:  %zu (expected 0)\n", sl->stat_inserts - initial_inserts);
    printf("    stat_updates:        %zu (expected %d)\n", sl->stat_updates, num_updates);
    
    int passed = ((int)sl->size == n && updated_ok == num_updates && 
                  not_updated_ok == num_not_updated);
    print_result("Update Existing Keys", passed);
    
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * TEST 7: Stress Test - Mixed Operations
 *----------------------------------------------------------------------------*/
int test_stress(int n) {
    print_separator("TEST 7: Stress Test - Mixed Operations");
    printf("  Running %d mixed operations...\n", n);
    
    cskiplist* sl = csl_create();
    if (!sl) {
        printf("  ERROR: Failed to create skiplist\n");
        return 0;
    }
    
    seed_rand(999);
    int inserts = 0, deletes = 0, searches = 0, updates = 0;
    int errors = 0;
    
    /* Track which keys are inserted */
    char* present = (char*)calloc(n, sizeof(char));
    if (!present) {
        csl_free(sl, NULL);
        return 0;
    }
    
    double t_start = get_time_ms();
    
    for (int i = 0; i < n; i++) {
        int op = fast_rand() % 10;  /* 0-4=insert, 5-7=search, 8-9=delete */
        int key = fast_rand() % (n / 2);
        
        if (op < 5) {
            /* Insert */
            int result = csl_insert(sl, key, (void*)(intptr_t)(key + 1));
            if (result == 1) {
                present[key] = 1;
                inserts++;
            } else if (result == 0) {
                updates++;
            } else {
                errors++;
            }
        } else if (op < 8) {
            /* Search */
            void* val = csl_search(sl, key);
            if (present[key] && !val) errors++;
            searches++;
        } else {
            /* Delete */
            if (csl_delete(sl, key, NULL)) {
                present[key] = 0;
                deletes++;
            }
        }
        
        if ((i + 1) % 200000 == 0) {
            printf("    ... %d operations, size=%zu\n", i + 1, sl->size);
        }
    }
    
    double t_total = get_time_ms() - t_start;
    
    /* Verify consistency */
    printf("  Verifying consistency...\n");
    csl_rebuild_skips(sl);
    
    int verify_present = 0;
    int verify_absent = 0;
    int verify_errors = 0;
    
    for (int i = 0; i < n / 2; i++) {
        void* val = csl_search(sl, i);
        if (present[i]) {
            if (val) verify_present++;
            else verify_errors++;
        } else {
            if (!val) verify_absent++;
            else verify_errors++;
        }
    }
    
    /* Count via iteration */
    int iter_count = 0;
    csl_iter it;
    if (csl_iter_first(sl, &it)) {
        do { iter_count++; } while (csl_iter_next(&it));
    }
    
    printf("\n  Results:\n");
    printf("    Total time:        %.2f ms (%.0f ops/sec)\n", 
           t_total, n / (t_total / 1000.0));
    printf("    Inserts:           %d\n", inserts);
    printf("    Updates:           %d\n", updates);
    printf("    Deletes:           %d\n", deletes);
    printf("    Searches:          %d\n", searches);
    printf("    Runtime errors:    %d\n", errors);
    printf("    Final size:        %zu\n", sl->size);
    printf("    Iteration count:   %d\n", iter_count);
    printf("    Verify present:    %d\n", verify_present);
    printf("    Verify absent:     %d\n", verify_absent);
    printf("    Verify errors:     %d\n", verify_errors);
    
    int passed = (errors == 0 && verify_errors == 0 && 
                  iter_count == (int)sl->size);
    print_result("Stress Test", passed);
    
    free(present);
    csl_free(sl, NULL);
    return passed;
}

/*-----------------------------------------------------------------------------
 * MAIN
 *----------------------------------------------------------------------------*/
int main(int argc, char* argv[]) {
    int test_size = TEST_SIZE_LARGE;  /* Default 1M */
    
    /* Parse command line for test size */
    if (argc > 1) {
        if (strcmp(argv[1], "small") == 0) test_size = TEST_SIZE_SMALL;
        else if (strcmp(argv[1], "medium") == 0) test_size = TEST_SIZE_MEDIUM;
        else if (strcmp(argv[1], "large") == 0) test_size = TEST_SIZE_LARGE;
        else if (strcmp(argv[1], "xlarge") == 0) test_size = TEST_SIZE_XLARGE;
        else test_size = atoi(argv[1]);
    }
    
    printf("\n");
    printf("****************************************************************\n");
    printf("*                                                              *\n");
    printf("*   CSKIPLIST MILLION-KEY TEST SUITE                          *\n");
    printf("*   Issue #1: Test cskiplist with 1M+ keys                    *\n");
    printf("*                                                              *\n");
    printf("****************************************************************\n");
    printf("\n  Test size: %d keys\n", test_size);
    printf("  Block capacity: %d (CSL_BLOCK_CAP)\n", CSL_BLOCK_CAP);
    printf("  Max levels: %d (CSL_MAX_LEVEL)\n", CSL_MAX_LEVEL);
    
    int passed = 0;
    int total = 7;
    
    passed += test_sequential_insert(test_size);
    passed += test_random_insert(test_size);
    passed += test_delete_large(test_size);
    passed += test_iteration_large(test_size);
    passed += test_search_performance(test_size);
    passed += test_update_large(test_size);
    passed += test_stress(test_size);
    
    printf("\n");
    printf("****************************************************************\n");
    printf("*                                                              *\n");
    printf("*   FINAL RESULTS: %d/%d tests passed                          *\n", passed, total);
    printf("*                                                              *\n");
    if (passed == total) {
        printf("*   STATUS: ALL TESTS PASSED!                                 *\n");
    } else {
        printf("*   STATUS: SOME TESTS FAILED                                 *\n");
    }
    printf("*                                                              *\n");
    printf("****************************************************************\n");
    printf("\n");
    
    return (passed == total) ? 0 : 1;
}
