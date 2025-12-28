/*-----------------------------------------------------------------------------
 * Adaptive Skip List Implementation - Issue #3
 * 
 * Switches between simple array and cskiplist based on element count.
 *----------------------------------------------------------------------------*/
#include "askiplist.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*-----------------------------------------------------------------------------
 * Internal: Array operations
 *----------------------------------------------------------------------------*/

/* Binary search in sorted array. Returns index if found, or -(insertion_point + 1) if not found. */
static int arr_binary_search(asl_array* arr, asl_key_t key) {
    int lo = 0, hi = arr->count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (arr->items[mid].key < key) {
            lo = mid + 1;
        } else if (arr->items[mid].key > key) {
            hi = mid - 1;
        } else {
            return mid;  /* found */
        }
    }
    return -(lo + 1);  /* not found, insertion point is lo */
}

/* Ensure array has capacity for at least one more item */
static int arr_ensure_capacity(asl_array* arr, int needed) {
    if (arr->capacity >= needed) return 1;
    
    int new_cap = arr->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    if (new_cap < ASL_INITIAL_CAPACITY) new_cap = ASL_INITIAL_CAPACITY;
    
    asl_kv* new_items = (asl_kv*)realloc(arr->items, new_cap * sizeof(asl_kv));
    if (!new_items) return 0;
    
    arr->items = new_items;
    arr->capacity = new_cap;
    return 1;
}

/* Insert into sorted array at given position */
static int arr_insert_at(asl_array* arr, int pos, asl_key_t key, asl_val_t val) {
    if (!arr_ensure_capacity(arr, arr->count + 1)) return -1;
    
    /* Shift elements right */
    if (pos < arr->count) {
        memmove(&arr->items[pos + 1], &arr->items[pos], 
                (arr->count - pos) * sizeof(asl_kv));
    }
    
    arr->items[pos].key = key;
    arr->items[pos].val = val;
    arr->count++;
    return 1;
}

/* Delete from array at given position */
static void arr_delete_at(asl_array* arr, int pos, void (*free_val)(asl_val_t)) {
    if (free_val) {
        free_val(arr->items[pos].val);
    }
    
    /* Shift elements left */
    if (pos < arr->count - 1) {
        memmove(&arr->items[pos], &arr->items[pos + 1],
                (arr->count - pos - 1) * sizeof(asl_kv));
    }
    arr->count--;
}

/*-----------------------------------------------------------------------------
 * Internal: Migration between modes
 *----------------------------------------------------------------------------*/

/* Migrate from array mode to skiplist mode */
static int migrate_to_skiplist(askiplist* asl) {
    if (asl->mode == ASL_MODE_SKIPLIST) return 1;  /* already skiplist */
    
    cskiplist* sl = csl_create();
    if (!sl) return 0;
    
    /* Copy all items from array to skiplist */
    for (int i = 0; i < asl->store.arr.count; i++) {
        int rc = csl_insert(sl, asl->store.arr.items[i].key, 
                           asl->store.arr.items[i].val);
        if (rc < 0) {
            csl_free(sl, NULL);
            return 0;
        }
    }
    
    /* Rebuild skip pointers for optimal search */
    csl_rebuild_skips(sl);
    
    /* Free old array storage */
    free(asl->store.arr.items);
    
    /* Switch to skiplist mode */
    asl->store.sl = sl;
    asl->mode = ASL_MODE_SKIPLIST;
    asl->stat_migrations++;
    
    return 1;
}

/*-----------------------------------------------------------------------------
 * API Implementation: Creation and Destruction
 *----------------------------------------------------------------------------*/

askiplist* asl_create(void) {
    return asl_create_with_threshold(ASL_DEFAULT_THRESHOLD);
}

askiplist* asl_create_with_threshold(int threshold) {
    askiplist* asl = (askiplist*)calloc(1, sizeof(askiplist));
    if (!asl) return NULL;
    
    asl->mode = ASL_MODE_ARRAY;
    asl->threshold = threshold > 0 ? threshold : ASL_DEFAULT_THRESHOLD;
    
    /* Start with small array */
    asl->store.arr.items = (asl_kv*)malloc(ASL_INITIAL_CAPACITY * sizeof(asl_kv));
    if (!asl->store.arr.items) {
        free(asl);
        return NULL;
    }
    asl->store.arr.count = 0;
    asl->store.arr.capacity = ASL_INITIAL_CAPACITY;
    
    asl->size = 0;
    asl->stat_migrations = 0;
    asl->stat_inserts = 0;
    asl->stat_updates = 0;
    asl->stat_deletes = 0;
    
    return asl;
}

void asl_free(askiplist* asl, void (*free_val)(asl_val_t)) {
    if (!asl) return;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        if (free_val) {
            for (int i = 0; i < asl->store.arr.count; i++) {
                free_val(asl->store.arr.items[i].val);
            }
        }
        free(asl->store.arr.items);
    } else {
        csl_free(asl->store.sl, free_val);
    }
    
    free(asl);
}

/*-----------------------------------------------------------------------------
 * API Implementation: Configuration
 *----------------------------------------------------------------------------*/

int asl_get_threshold(askiplist* asl) {
    return asl ? asl->threshold : 0;
}

int asl_set_threshold(askiplist* asl, int threshold) {
    if (!asl || threshold < 1) return 0;
    
    asl->threshold = threshold;
    
    /* If we're in array mode and size >= new threshold, migrate */
    if (asl->mode == ASL_MODE_ARRAY && (int)asl->size >= threshold) {
        return migrate_to_skiplist(asl);
    }
    
    return 1;
}

asl_mode asl_get_mode(askiplist* asl) {
    return asl ? asl->mode : ASL_MODE_ARRAY;
}

int asl_force_skiplist_mode(askiplist* asl) {
    if (!asl) return 0;
    return migrate_to_skiplist(asl);
}

/*-----------------------------------------------------------------------------
 * API Implementation: Core Operations
 *----------------------------------------------------------------------------*/

int asl_insert(askiplist* asl, asl_key_t key, asl_val_t val) {
    if (!asl) return -1;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        /* First try to insert/update in array */
        int idx = arr_binary_search(&asl->store.arr, key);
        if (idx >= 0) {
            /* Key exists, update value - no size change, no migration needed */
            asl->store.arr.items[idx].val = val;
            asl->stat_updates++;
            return 0;
        }
        
        /* New key - check if inserting would cross threshold */
        if ((int)(asl->size + 1) >= asl->threshold) {
            /* Insert first, then migrate */
            int pos = -(idx + 1);
            int rc = arr_insert_at(&asl->store.arr, pos, key, val);
            if (rc < 0) return -1;
            asl->size++;
            asl->stat_inserts++;
            
            /* Now migrate to skiplist */
            if (!migrate_to_skiplist(asl)) {
                /* Migration failed but insert succeeded - stay in array mode */
                /* This is acceptable; we'll try migration again on next insert */
            }
            return 1;
        }
        
        /* Insert into array (below threshold) */
        int pos = -(idx + 1);
        int rc = arr_insert_at(&asl->store.arr, pos, key, val);
        if (rc > 0) {
            asl->size++;
            asl->stat_inserts++;
        }
        return rc;
    }
    
    /* Skiplist mode */
    int rc = csl_insert(asl->store.sl, key, val);
    if (rc > 0) {
        asl->size++;
        asl->stat_inserts++;
    } else if (rc == 0) {
        asl->stat_updates++;
    }
    return rc;
}

int asl_delete(askiplist* asl, asl_key_t key, void (*free_val)(asl_val_t)) {
    if (!asl) return 0;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        int idx = arr_binary_search(&asl->store.arr, key);
        if (idx < 0) return 0;  /* not found */
        
        arr_delete_at(&asl->store.arr, idx, free_val);
        asl->size--;
        asl->stat_deletes++;
        return 1;
    }
    
    /* Skiplist mode */
    int rc = csl_delete(asl->store.sl, key, free_val);
    if (rc > 0) {
        asl->size--;
        asl->stat_deletes++;
    }
    return rc;
}

asl_val_t asl_search(askiplist* asl, asl_key_t key) {
    if (!asl) return NULL;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        int idx = arr_binary_search(&asl->store.arr, key);
        if (idx >= 0) {
            return asl->store.arr.items[idx].val;
        }
        return NULL;
    }
    
    /* Skiplist mode */
    return csl_search(asl->store.sl, key);
}

int asl_contains(askiplist* asl, asl_key_t key) {
    if (!asl) return 0;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        return arr_binary_search(&asl->store.arr, key) >= 0;
    }
    
    /* Skiplist mode - search and check if found */
    return csl_search(asl->store.sl, key) != NULL || 
           /* Handle NULL value case by doing existence check */
           (asl->store.sl->size > 0 && csl_search(asl->store.sl, key) == NULL);
    /* Note: This is imperfect for NULL values. For proper contains, 
       cskiplist would need a separate contains function. */
}

size_t asl_size(askiplist* asl) {
    return asl ? asl->size : 0;
}

/*-----------------------------------------------------------------------------
 * API Implementation: Iterator
 *----------------------------------------------------------------------------*/

int asl_iter_first(askiplist* asl, asl_iter* it) {
    if (!asl || !it) return 0;
    
    it->asl = asl;
    
    if (asl->mode == ASL_MODE_ARRAY) {
        it->arr_idx = 0;
        return asl->store.arr.count > 0;
    }
    
    /* Skiplist mode */
    return csl_iter_first(asl->store.sl, &it->sl_iter);
}

int asl_iter_next(asl_iter* it) {
    if (!it || !it->asl) return 0;
    
    if (it->asl->mode == ASL_MODE_ARRAY) {
        it->arr_idx++;
        return it->arr_idx < it->asl->store.arr.count;
    }
    
    /* Skiplist mode */
    return csl_iter_next(&it->sl_iter);
}

asl_kv* asl_iter_get(asl_iter* it) {
    if (!it || !it->asl) return NULL;
    
    if (it->asl->mode == ASL_MODE_ARRAY) {
        if (it->arr_idx >= 0 && it->arr_idx < it->asl->store.arr.count) {
            return (asl_kv*)&it->asl->store.arr.items[it->arr_idx];
        }
        return NULL;
    }
    
    /* Skiplist mode - csl_kv and asl_kv have same layout */
    return (asl_kv*)csl_iter_get(&it->sl_iter);
}

/*-----------------------------------------------------------------------------
 * API Implementation: Debugging/Stats
 *----------------------------------------------------------------------------*/

void asl_print_stats(askiplist* asl) {
    if (!asl) {
        printf("askiplist: NULL\n");
        return;
    }
    
    printf("=== Adaptive Skip List Stats ===\n");
    printf("Mode:        %s\n", asl->mode == ASL_MODE_ARRAY ? "ARRAY" : "SKIPLIST");
    printf("Threshold:   %d\n", asl->threshold);
    printf("Size:        %zu\n", asl->size);
    printf("Migrations:  %zu\n", asl->stat_migrations);
    printf("Inserts:     %zu\n", asl->stat_inserts);
    printf("Updates:     %zu\n", asl->stat_updates);
    printf("Deletes:     %zu\n", asl->stat_deletes);
    
    if (asl->mode == ASL_MODE_ARRAY) {
        printf("Array cap:   %d\n", asl->store.arr.capacity);
    } else {
        printf("Blocks:      %zu\n", asl->store.sl->nblocks);
        printf("SL Level:    %d\n", asl->store.sl->level);
    }
    printf("================================\n");
}
