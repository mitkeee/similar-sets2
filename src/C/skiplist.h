/*-----------------------------------------------------------------------------
 * Classic skip list (cache-neutral baseline)
 *
 * Unique integer keys; value is a void* payload. Duplicate inserts update value.
 *
 * Copyright (c) 2025
 *----------------------------------------------------------------------------*/
#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SL_MAX_LEVEL
#define SL_MAX_LEVEL 32
#endif

#ifndef SL_P
#define SL_P 0.25 /* probability for level promotion */
#endif

/* Key and value types */
typedef int sl_key_t;     /* monotonic order: ascending */
typedef void* sl_val_t;   /* user payload */

/* Node */
typedef struct sl_node {
    sl_key_t key;
    sl_val_t val;
    /* next pointers per level [0..height-1]; array sized to SL_MAX_LEVEL */
    struct sl_node* next[SL_MAX_LEVEL];
} sl_node;

/* List */
typedef struct skiplist {
    int level;         /* highest level in use (0-based) */
    size_t size;       /* number of elements */
    sl_node* head;     /* header with -inf key */
    /* simple xorshift rng state for deterministic tests */
    uint32_t rng;
} skiplist;

/* API */
skiplist* sl_create(void);
void sl_free(skiplist* sl, void (*free_val)(sl_val_t));

/* Insert or update. Returns 1 if inserted new, 0 if updated existing, -1 on OOM */
int sl_insert(skiplist* sl, sl_key_t key, sl_val_t val);
/* Delete key. Returns 1 if removed, 0 if not found */
int sl_delete(skiplist* sl, sl_key_t key, void (**out_val)(void));
/* Search. Returns payload or NULL if not found */
sl_val_t sl_search(skiplist* sl, sl_key_t key);

/* Iteration helpers */
sl_node* sl_first(skiplist* sl);            /* smallest node or NULL */
sl_node* sl_next_of(skiplist* sl, sl_key_t key); /* first node with key > given */

#ifdef __cplusplus
}
#endif

#endif /* SKIPLIST_H */
