/*-----------------------------------------------------------------------------
 * Cache-friendly block skip list (deterministic, power-of-two skips)
 * Inspired by skep.h notes: mem_blocks with sorted kv arrays + skip pointers.
 *----------------------------------------------------------------------------*/
#ifndef CSKIPLIST_H
#define CSKIPLIST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSL_BLOCK_CAP
#define CSL_BLOCK_CAP 128  /* items per block; tune for cache line / page */
#endif

#ifndef CSL_MAX_LEVEL
#define CSL_MAX_LEVEL 20   /* enough for millions of blocks */
#endif

typedef int csl_key_t;
typedef void* csl_val_t;

typedef struct csl_kv {
    csl_key_t key;
    csl_val_t val;
} csl_kv;

/* Memory block of sorted key/value pairs + skip pointers */
typedef struct csl_block {
    int min_key;                     /* minimum key in items[] */
    int count;                       /* number of valid items */
    csl_kv items[CSL_BLOCK_CAP];     /* sorted by key */
    struct csl_block* prev;          /* backward pointer on level 0 chain */
    struct csl_block* next[CSL_MAX_LEVEL]; /* skip pointers across blocks */
} csl_block;

/* Skip list of blocks */
typedef struct cskiplist {
    csl_block* head;   /* sentinel block; min_key = INT32_MIN, count=0 */
    int level;         /* top level currently valid (0-based) */
    size_t nblocks;    /* number of data blocks (excludes head) */
    size_t size;       /* number of items across all blocks */
    /* simple stats */
    size_t stat_inserts;
    size_t stat_updates;
    size_t stat_deletes;
    size_t stat_splits;
} cskiplist;

/* API */
cskiplist* csl_create(void);
void csl_free(cskiplist* sl, void (*free_val)(csl_val_t));

/* Append item assuming non-decreasing keys. Returns 1 on append, 0 on update (same key at tail), -1 on OOM */
int csl_append(cskiplist* sl, csl_key_t key, csl_val_t val);

/* General insert that maintains order. Splits blocks when full. Returns 1 on insert, 0 on update, -1 on OOM */
int csl_insert(cskiplist* sl, csl_key_t key, csl_val_t val);

/* Delete a key. Returns 1 when deleted, 0 if key not found. If provided, free_val is called on deleted value. */
int csl_delete(cskiplist* sl, csl_key_t key, void (*free_val)(csl_val_t));

/* Find value for key; returns NULL if not found (note: NULL may be a stored value) */
csl_val_t csl_search(cskiplist* sl, csl_key_t key);

/* Rebuild skip pointers deterministically using power-of-two strides */
void csl_rebuild_skips(cskiplist* sl);

/* Lightweight iterator over key/value pairs (in-order) */
typedef struct csl_iter {
    csl_block* b; /* current block, NULL if invalid */
    int idx;      /* index within block */
} csl_iter;

/* Initialize iterator to first item; returns 1 if non-empty, else 0 */
int csl_iter_first(cskiplist* sl, csl_iter* it);

/* Seek to first item with key >= given key. Sets *exact=1 if exact key found. Returns 1 if positioned, 0 if past end. */
int csl_iter_seek(cskiplist* sl, csl_key_t key, csl_iter* it, int* exact);

/* Move to next/prev item; return 1 if valid after move, 0 if hit end/begin */
int csl_iter_next(csl_iter* it);
int csl_iter_prev(cskiplist* sl, csl_iter* it);

/* Accessor for current item; returns NULL if iterator invalid */
static inline csl_kv* csl_iter_get(csl_iter* it) { return (it && it->b && it->idx >= 0 && it->idx < it->b->count) ? &it->b->items[it->idx] : NULL; }

#ifdef __cplusplus
}
#endif

#endif /* CSKIPLIST_H */
