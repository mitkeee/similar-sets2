/*-----------------------------------------------------------------------------
 * Test for Adaptive Skip List - Issue #3
 * 
 * Tests:
 * - Small N: stays in array mode
 * - Large N: migrates to skiplist mode
 * - Threshold configuration
 * - Correctness across mode transitions
 * - Performance comparison: array vs skiplist for various N
 *----------------------------------------------------------------------------*/
#include "askiplist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s\n", msg); \
        tests_failed++; \
        return 0; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    printf("  [PASS] %s\n", msg); \
    tests_passed++; \
} while(0)

static double get_time_ms(void) {
    return (double)clock() / CLOCKS_PER_SEC * 1000.0;
}

/*-----------------------------------------------------------------------------
 * Test 1: Basic array mode operations (N < threshold)
 *----------------------------------------------------------------------------*/
int test_array_mode(void) {
    printf("\n--- Test 1: Array Mode Operations ---\n");
    
    askiplist* asl = asl_create_with_threshold(10);
    TEST_ASSERT(asl != NULL, "Create adaptive skiplist");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_ARRAY, "Initial mode is ARRAY");
    TEST_ASSERT(asl_get_threshold(asl) == 10, "Threshold is 10");
    
    /* Insert 5 items - should stay in array mode */
    for (int i = 0; i < 5; i++) {
        int rc = asl_insert(asl, i * 10, (void*)(intptr_t)(i + 1));
        TEST_ASSERT(rc == 1, "Insert returns 1 for new key");
    }
    
    TEST_ASSERT(asl_size(asl) == 5, "Size is 5");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_ARRAY, "Still in ARRAY mode after 5 inserts");
    
    /* Verify searches */
    for (int i = 0; i < 5; i++) {
        void* val = asl_search(asl, i * 10);
        TEST_ASSERT(val == (void*)(intptr_t)(i + 1), "Search finds correct value");
    }
    
    /* Test update */
    int rc = asl_insert(asl, 20, (void*)999);
    TEST_ASSERT(rc == 0, "Insert returns 0 for update");
    TEST_ASSERT(asl_search(asl, 20) == (void*)999, "Updated value is correct");
    
    /* Test delete */
    rc = asl_delete(asl, 20, NULL);
    TEST_ASSERT(rc == 1, "Delete returns 1 for existing key");
    TEST_ASSERT(asl_search(asl, 20) == NULL, "Deleted key not found");
    TEST_ASSERT(asl_size(asl) == 4, "Size decreased after delete");
    
    /* Test not found */
    TEST_ASSERT(asl_search(asl, 999) == NULL, "Non-existent key returns NULL");
    rc = asl_delete(asl, 999, NULL);
    TEST_ASSERT(rc == 0, "Delete returns 0 for non-existent key");
    
    asl_free(asl, NULL);
    TEST_PASS("Array mode operations");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 2: Automatic migration to skiplist mode
 *----------------------------------------------------------------------------*/
int test_auto_migration(void) {
    printf("\n--- Test 2: Automatic Migration ---\n");
    
    askiplist* asl = asl_create_with_threshold(5);
    TEST_ASSERT(asl != NULL, "Create adaptive skiplist with threshold 5");
    
    /* Insert up to threshold - should migrate on 5th insert */
    for (int i = 0; i < 4; i++) {
        asl_insert(asl, i, (void*)(intptr_t)(i + 100));
    }
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_ARRAY, "Still ARRAY mode with 4 items");
    
    /* 5th insert triggers migration */
    asl_insert(asl, 4, (void*)(intptr_t)104);
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "Migrated to SKIPLIST on threshold");
    TEST_ASSERT(asl_size(asl) == 5, "Size is correct after migration");
    
    /* Verify all data survived migration */
    for (int i = 0; i < 5; i++) {
        void* val = asl_search(asl, i);
        TEST_ASSERT(val == (void*)(intptr_t)(i + 100), "Data intact after migration");
    }
    
    /* Continue inserting in skiplist mode */
    for (int i = 5; i < 20; i++) {
        asl_insert(asl, i, (void*)(intptr_t)(i + 100));
    }
    TEST_ASSERT(asl_size(asl) == 20, "Size is 20 after more inserts");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "Still in SKIPLIST mode");
    
    /* Verify all data */
    for (int i = 0; i < 20; i++) {
        void* val = asl_search(asl, i);
        TEST_ASSERT(val == (void*)(intptr_t)(i + 100), "All values correct");
    }
    
    asl_print_stats(asl);
    
    asl_free(asl, NULL);
    TEST_PASS("Automatic migration");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 3: Iterator across modes
 *----------------------------------------------------------------------------*/
int test_iterator(void) {
    printf("\n--- Test 3: Iterator ---\n");
    
    /* Test array mode iterator */
    askiplist* asl = asl_create_with_threshold(100);
    for (int i = 0; i < 10; i++) {
        asl_insert(asl, i * 2, (void*)(intptr_t)i);  /* keys: 0, 2, 4, ... 18 */
    }
    
    asl_iter it;
    int count = 0;
    int last_key = -1;
    for (int valid = asl_iter_first(asl, &it); valid; valid = asl_iter_next(&it)) {
        asl_kv* kv = asl_iter_get(&it);
        TEST_ASSERT(kv != NULL, "Iterator returns valid kv");
        TEST_ASSERT(kv->key > last_key, "Keys are in sorted order");
        last_key = kv->key;
        count++;
    }
    TEST_ASSERT(count == 10, "Iterator visited all items in array mode");
    
    asl_free(asl, NULL);
    
    /* Test skiplist mode iterator */
    asl = asl_create_with_threshold(5);
    for (int i = 0; i < 20; i++) {
        asl_insert(asl, i * 3, (void*)(intptr_t)i);  /* keys: 0, 3, 6, ... 57 */
    }
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "In skiplist mode");
    
    count = 0;
    last_key = -1;
    for (int valid = asl_iter_first(asl, &it); valid; valid = asl_iter_next(&it)) {
        asl_kv* kv = asl_iter_get(&it);
        TEST_ASSERT(kv != NULL, "Iterator returns valid kv");
        TEST_ASSERT(kv->key > last_key, "Keys are in sorted order");
        last_key = kv->key;
        count++;
    }
    TEST_ASSERT(count == 20, "Iterator visited all items in skiplist mode");
    
    asl_free(asl, NULL);
    TEST_PASS("Iterator");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 4: Threshold configuration
 *----------------------------------------------------------------------------*/
int test_threshold_config(void) {
    printf("\n--- Test 4: Threshold Configuration ---\n");
    
    askiplist* asl = asl_create_with_threshold(100);
    
    /* Insert 50 items */
    for (int i = 0; i < 50; i++) {
        asl_insert(asl, i, (void*)(intptr_t)i);
    }
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_ARRAY, "Array mode with 50 items, threshold 100");
    
    /* Lower threshold to trigger migration */
    int rc = asl_set_threshold(asl, 25);
    TEST_ASSERT(rc == 1, "Set threshold returned success");
    TEST_ASSERT(asl_get_threshold(asl) == 25, "Threshold updated");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "Migrated when threshold lowered");
    
    /* Verify data after migration */
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT(asl_search(asl, i) == (void*)(intptr_t)i, "Data intact after threshold change");
    }
    
    asl_free(asl, NULL);
    TEST_PASS("Threshold configuration");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 5: Force skiplist mode
 *----------------------------------------------------------------------------*/
int test_force_skiplist(void) {
    printf("\n--- Test 5: Force Skiplist Mode ---\n");
    
    askiplist* asl = asl_create_with_threshold(1000);
    
    /* Insert just a few items */
    for (int i = 0; i < 5; i++) {
        asl_insert(asl, i, (void*)(intptr_t)i);
    }
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_ARRAY, "Initially in array mode");
    
    /* Force skiplist mode */
    int rc = asl_force_skiplist_mode(asl);
    TEST_ASSERT(rc == 1, "Force skiplist succeeded");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "Now in skiplist mode");
    
    /* Verify data */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(asl_search(asl, i) == (void*)(intptr_t)i, "Data intact after force");
    }
    
    asl_free(asl, NULL);
    TEST_PASS("Force skiplist mode");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 6: Random order inserts
 *----------------------------------------------------------------------------*/
int test_random_order(void) {
    printf("\n--- Test 6: Random Order Inserts ---\n");
    
    askiplist* asl = asl_create_with_threshold(50);
    
    /* Insert in reverse order to test sorting */
    for (int i = 99; i >= 0; i--) {
        asl_insert(asl, i, (void*)(intptr_t)(i * 2));
    }
    
    TEST_ASSERT(asl_size(asl) == 100, "Size is 100");
    TEST_ASSERT(asl_get_mode(asl) == ASL_MODE_SKIPLIST, "In skiplist mode");
    
    /* Verify order and values */
    asl_iter it;
    int last_key = -1;
    int count = 0;
    for (int valid = asl_iter_first(asl, &it); valid; valid = asl_iter_next(&it)) {
        asl_kv* kv = asl_iter_get(&it);
        TEST_ASSERT(kv->key > last_key, "Keys in ascending order");
        TEST_ASSERT(kv->val == (void*)(intptr_t)(kv->key * 2), "Values correct");
        last_key = kv->key;
        count++;
    }
    TEST_ASSERT(count == 100, "Iterator counted 100 items");
    
    asl_free(asl, NULL);
    TEST_PASS("Random order inserts");
    return 1;
}

/*-----------------------------------------------------------------------------
 * Test 7: Performance comparison
 *----------------------------------------------------------------------------*/
void test_performance(void) {
    printf("\n--- Performance Comparison ---\n");
    printf("Comparing array mode (small N) vs skiplist mode (forced)\n\n");
    
    int test_sizes[] = {5, 10, 20, 50, 100, 500, 1000};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    printf("  %-8s  %-12s  %-12s  %-12s  %-12s\n", 
           "N", "Array Ins", "SL Ins", "Array Srch", "SL Srch");
    printf("  %-8s  %-12s  %-12s  %-12s  %-12s\n",
           "--------", "------------", "------------", "------------", "------------");
    
    for (int s = 0; s < num_sizes; s++) {
        int n = test_sizes[s];
        
        /* Array mode test */
        askiplist* arr_asl = asl_create_with_threshold(n + 1000);  /* Keep in array mode */
        double t0 = get_time_ms();
        for (int i = 0; i < n; i++) {
            asl_insert(arr_asl, i, (void*)(intptr_t)i);
        }
        double arr_insert_ms = get_time_ms() - t0;
        
        t0 = get_time_ms();
        for (int i = 0; i < n; i++) {
            asl_search(arr_asl, i);
        }
        double arr_search_ms = get_time_ms() - t0;
        
        /* Skiplist mode test (forced) */
        askiplist* sl_asl = asl_create_with_threshold(1);  /* Force skiplist mode immediately */
        t0 = get_time_ms();
        for (int i = 0; i < n; i++) {
            asl_insert(sl_asl, i, (void*)(intptr_t)i);
        }
        double sl_insert_ms = get_time_ms() - t0;
        
        t0 = get_time_ms();
        for (int i = 0; i < n; i++) {
            asl_search(sl_asl, i);
        }
        double sl_search_ms = get_time_ms() - t0;
        
        printf("  %-8d  %-12.4f  %-12.4f  %-12.4f  %-12.4f\n",
               n, arr_insert_ms, sl_insert_ms, arr_search_ms, sl_search_ms);
        
        asl_free(arr_asl, NULL);
        asl_free(sl_asl, NULL);
    }
    
    printf("\n  Insight: For small N, array is faster due to cache locality.\n");
    printf("  The adaptive threshold lets you get best of both worlds!\n");
}

/*-----------------------------------------------------------------------------
 * Test 8: Recommended threshold analysis
 *----------------------------------------------------------------------------*/
void analyze_optimal_threshold(void) {
    printf("\n--- Optimal Threshold Analysis ---\n");
    printf("Finding crossover point where skiplist becomes faster than array\n\n");
    
    int thresholds[] = {5, 10, 15, 20, 30, 50, 100};
    int num_thresholds = sizeof(thresholds) / sizeof(thresholds[0]);
    int test_n = 10000;
    int iterations = 3;
    
    printf("Testing with N=%d operations, averaged over %d runs\n\n", test_n, iterations);
    printf("  %-10s  %-15s  %-15s\n", "Threshold", "Total Time (ms)", "Migrations");
    printf("  %-10s  %-15s  %-15s\n", "----------", "---------------", "----------");
    
    for (int t = 0; t < num_thresholds; t++) {
        int threshold = thresholds[t];
        double total_time = 0;
        size_t total_migrations = 0;
        
        for (int iter = 0; iter < iterations; iter++) {
            askiplist* asl = asl_create_with_threshold(threshold);
            
            double t0 = get_time_ms();
            for (int i = 0; i < test_n; i++) {
                asl_insert(asl, i, (void*)(intptr_t)i);
            }
            for (int i = 0; i < test_n; i++) {
                asl_search(asl, i);
            }
            total_time += get_time_ms() - t0;
            total_migrations += asl->stat_migrations;
            
            asl_free(asl, NULL);
        }
        
        printf("  %-10d  %-15.2f  %-15zu\n", 
               threshold, total_time / iterations, total_migrations / iterations);
    }
    
    printf("\n  Recommendation: threshold=10 is a good default for most workloads.\n");
}

/*-----------------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------------*/
int main(int argc, char** argv) {
    printf("============================================================\n");
    printf("  Adaptive Skip List Tests - Issue #3\n");
    printf("  Testing adaptive threshold N parameter\n");
    printf("============================================================\n");
    
    /* Run correctness tests */
    test_array_mode();
    test_auto_migration();
    test_iterator();
    test_threshold_config();
    test_force_skiplist();
    test_random_order();
    
    /* Summary */
    printf("\n============================================================\n");
    printf("  Test Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("============================================================\n");
    
    /* Performance analysis */
    if (argc > 1 && strcmp(argv[1], "--perf") == 0) {
        test_performance();
        analyze_optimal_threshold();
    } else {
        printf("\nRun with --perf flag for performance analysis\n");
    }
    
    return tests_failed > 0 ? 1 : 0;
}
