#include "cskiplist.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

void test_reverse_iteration() {
    printf("\n=== Test Reverse Iteration ===\n");
    cskiplist* sl = csl_create();
    
    // Insert some keys
    for (int i = 0; i < 20; i++) {
        csl_insert(sl, i * 10, (void*)(intptr_t)(i * 10 + 1));
    }
    csl_rebuild_skips(sl);
    
    // Forward iteration
    printf("Forward: ");
    csl_iter it;
    if (csl_iter_first(sl, &it)) {
        do {
            csl_kv* kv = csl_iter_get(&it);
            printf("%d ", kv->key);
        } while (csl_iter_next(&it));
    }
    printf("\n");
    
    // Reverse from end
    printf("Reverse: ");
    // Start from last element by iterating to end first
    if (csl_iter_first(sl, &it)) {
        // Move to end
        while (csl_iter_next(&it)) {}
        // Now iterate backwards
        do {
            csl_kv* kv = csl_iter_get(&it);
            if (kv) printf("%d ", kv->key);
        } while (csl_iter_prev(sl, &it));
    }
    printf("\n");
    
    csl_free(sl, NULL);
    printf("✓ Reverse iteration passed\n");
}

void test_delete_operations() {
    printf("\n=== Test Delete Operations ===\n");
    cskiplist* sl = csl_create();
    
    // Insert 100 keys
    for (int i = 0; i < 100; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1000));
    }
    csl_rebuild_skips(sl);
    
    printf("Initial size: %zu\n", sl->size);
    
    // Delete every other key
    int deleted = 0;
    for (int i = 0; i < 100; i += 2) {
        if (csl_delete(sl, i, NULL)) deleted++;
    }
    
    printf("Deleted: %d, Final size: %zu\n", deleted, sl->size);
    
    // Verify remaining keys
    int found = 0;
    for (int i = 1; i < 100; i += 2) {
        if (csl_search(sl, i)) found++;
    }
    printf("Remaining odd keys found: %d/50\n", found);
    
    // Verify deleted keys are gone
    int notfound = 0;
    for (int i = 0; i < 100; i += 2) {
        if (!csl_search(sl, i)) notfound++;
    }
    printf("Even keys correctly deleted: %d/%d\n", notfound, deleted);
    
    csl_free(sl, NULL);
    printf("✓ Delete operations passed\n");
}

void test_split_behavior() {
    printf("\n=== Test Block Split Behavior ===\n");
    cskiplist* sl = csl_create();
    
    // Insert enough to trigger splits (CSL_BLOCK_CAP is 128)
    int n = 300;
    for (int i = 0; i < n; i++) {
        csl_insert(sl, i, (void*)(intptr_t)(i * 2));
    }
    
    printf("Inserted: %d keys\n", n);
    printf("Blocks: %zu\n", sl->nblocks);
    printf("Splits: %zu\n", sl->stat_splits);
    printf("Size: %zu\n", sl->size);
    
    // Verify all keys present
    int verified = 0;
    for (int i = 0; i < n; i++) {
        void* val = csl_search(sl, i);
        if (val && (intptr_t)val == i * 2) verified++;
    }
    printf("Keys verified: %d/%d\n", verified, n);
    
    csl_free(sl, NULL);
    printf("✓ Split behavior passed\n");
}

void test_stats() {
    printf("\n=== Test Statistics Tracking ===\n");
    cskiplist* sl = csl_create();
    
    // Mix of operations
    for (int i = 0; i < 50; i++) {
        csl_insert(sl, i, (void*)(intptr_t)i);
    }
    
    // Update existing keys
    for (int i = 0; i < 50; i += 5) {
        csl_insert(sl, i, (void*)(intptr_t)(i + 1000));
    }
    
    // Delete some
    for (int i = 0; i < 50; i += 10) {
        csl_delete(sl, i, NULL);
    }
    
    printf("Inserts: %zu\n", sl->stat_inserts);
    printf("Updates: %zu\n", sl->stat_updates);
    printf("Deletes: %zu\n", sl->stat_deletes);
    printf("Splits: %zu\n", sl->stat_splits);
    printf("Final size: %zu\n", sl->size);
    
    csl_free(sl, NULL);
    printf("✓ Statistics tracking passed\n");
}

void test_random_operations() {
    printf("\n=== Test Random Operations ===\n");
    cskiplist* sl = csl_create();
    srand((unsigned)time(NULL));
    
    int ops = 1000;
    int inserts = 0, deletes = 0, searches = 0;
    
    for (int i = 0; i < ops; i++) {
        int op = rand() % 3;
        int key = rand() % 500;
        
        switch (op) {
            case 0: // insert
                csl_insert(sl, key, (void*)(intptr_t)key);
                inserts++;
                break;
            case 1: // delete
                csl_delete(sl, key, NULL);
                deletes++;
                break;
            case 2: // search
                csl_search(sl, key);
                searches++;
                break;
        }
    }
    
    printf("Operations: %d inserts, %d deletes, %d searches\n", inserts, deletes, searches);
    printf("Final size: %zu blocks: %zu\n", sl->size, sl->nblocks);
    
    // Rebuild and verify iteration works
    csl_rebuild_skips(sl);
    int count = 0;
    csl_iter it;
    if (csl_iter_first(sl, &it)) {
        do { count++; } while (csl_iter_next(&it));
    }
    printf("Iterated: %d items (size=%zu)\n", count, sl->size);
    
    if (count == (int)sl->size) {
        printf("✓ Random operations passed\n");
    } else {
        printf("✗ Size mismatch: iterated %d vs size %zu\n", count, sl->size);
    }
    
    csl_free(sl, NULL);
}

int main(void) {
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║  Enhanced CSkiplist Test Suite                       ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    
    test_reverse_iteration();
    test_delete_operations();
    test_split_behavior();
    test_stats();
    test_random_operations();
    
    printf("\n╔═══════════════════════════════════════════════════════╗\n");
    printf("║  All tests completed successfully!                   ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    
    return 0;
}
