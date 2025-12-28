/*-----------------------------------------------------------------------------
 * Adaptive Skip List - Issue #3
 * 
 * Automatically switches between:
 *   - Simple sorted array with binary search (for small N)
 *   - Full block skip list (cskiplist) for large N
 * 
 * The threshold is configurable at compile time and runtime.
 * Default threshold: 10 elements
 * 
 * Benefits:
 *   - Small collections: no allocation overhead, cache-optimal linear array
 *   - Large collections: O(log N) skip list performance
 *   - Automatic migration when crossing threshold
 *----------------------------------------------------------------------------*/
#ifndef ASKIPLIST_H
#define ASKIPLIST_H

#include <stddef.h>
#include <stdint.h>
#include "cskiplist.h"

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * Configuration
 *----------------------------------------------------------------------------*/

/* Default threshold: use array for N < threshold, skip list for N >= threshold */
#ifndef ASL_DEFAULT_THRESHOLD
#define ASL_DEFAULT_THRESHOLD 10
#endif

/* Initial array capacity (will grow as needed up to threshold) */
#ifndef ASL_INITIAL_CAPACITY
#define ASL_INITIAL_CAPACITY 4
#endif

/* Use same key/value types as cskiplist for compatibility */
typedef csl_key_t asl_key_t;
typedef csl_val_t asl_val_t;

/*-----------------------------------------------------------------------------
 * Data Structures
 *----------------------------------------------------------------------------*/

/* Storage mode */
typedef enum {
    ASL_MODE_ARRAY,     /* Using simple sorted array */
    ASL_MODE_SKIPLIST   /* Using full cskiplist */
} asl_mode;

/* Key-value pair (same layout as csl_kv for easy migration) */
typedef struct asl_kv {
    asl_key_t key;
    asl_val_t val;
} asl_kv;

/* Simple sorted array storage */
typedef struct asl_array {
    asl_kv* items;      /* sorted array of items */
    int count;          /* number of valid items */
    int capacity;       /* allocated capacity */
} asl_array;

/* Adaptive skip list - union of array and skiplist */
typedef struct askiplist {
    asl_mode mode;              /* current storage mode */
    int threshold;              /* switch threshold (configurable) */
    
    union {
        asl_array arr;          /* array storage (when mode == ASL_MODE_ARRAY) */
        cskiplist* sl;          /* skiplist storage (when mode == ASL_MODE_SKIPLIST) */
    } store;
    
    /* Statistics */
    size_t size;                /* total number of items */
    size_t stat_migrations;     /* times we switched from array to skiplist */
    size_t stat_inserts;
    size_t stat_updates;
    size_t stat_deletes;
} askiplist;

/*-----------------------------------------------------------------------------
 * API - Creation and Destruction
 *----------------------------------------------------------------------------*/

/* Create adaptive skiplist with default threshold */
askiplist* asl_create(void);

/* Create adaptive skiplist with custom threshold */
askiplist* asl_create_with_threshold(int threshold);

/* Free all memory. If free_val is provided, it's called for each value. */
void asl_free(askiplist* asl, void (*free_val)(asl_val_t));

/*-----------------------------------------------------------------------------
 * API - Configuration
 *----------------------------------------------------------------------------*/

/* Get current threshold */
int asl_get_threshold(askiplist* asl);

/* Set new threshold. If current size > new threshold and in array mode, migrates to skiplist.
 * Returns 1 on success, 0 on failure (OOM during migration). */
int asl_set_threshold(askiplist* asl, int threshold);

/* Get current storage mode */
asl_mode asl_get_mode(askiplist* asl);

/* Force migration to skiplist mode (useful if you know many inserts are coming) */
int asl_force_skiplist_mode(askiplist* asl);

/*-----------------------------------------------------------------------------
 * API - Core Operations
 *----------------------------------------------------------------------------*/

/* Insert/update a key-value pair.
 * Returns: 1 on new insert, 0 on update of existing key, -1 on error (OOM) */
int asl_insert(askiplist* asl, asl_key_t key, asl_val_t val);

/* Delete a key. Returns 1 if deleted, 0 if not found. */
int asl_delete(askiplist* asl, asl_key_t key, void (*free_val)(asl_val_t));

/* Search for a key. Returns value if found, NULL if not found.
 * Note: NULL may be a valid stored value; use asl_contains for existence check. */
asl_val_t asl_search(askiplist* asl, asl_key_t key);

/* Check if key exists. Returns 1 if found, 0 if not. */
int asl_contains(askiplist* asl, asl_key_t key);

/* Get number of items */
size_t asl_size(askiplist* asl);

/*-----------------------------------------------------------------------------
 * API - Iterator
 *----------------------------------------------------------------------------*/

typedef struct asl_iter {
    askiplist* asl;     /* parent list */
    int arr_idx;        /* index for array mode */
    csl_iter sl_iter;   /* iterator for skiplist mode */
} asl_iter;

/* Initialize iterator to first item. Returns 1 if non-empty, 0 if empty. */
int asl_iter_first(askiplist* asl, asl_iter* it);

/* Move to next item. Returns 1 if valid, 0 if at end. */
int asl_iter_next(asl_iter* it);

/* Get current key-value. Returns NULL if iterator invalid. */
asl_kv* asl_iter_get(asl_iter* it);

/*-----------------------------------------------------------------------------
 * API - Debugging/Stats
 *----------------------------------------------------------------------------*/

/* Print statistics to stdout */
void asl_print_stats(askiplist* asl);

#ifdef __cplusplus
}
#endif

#endif /* ASKIPLIST_H */
