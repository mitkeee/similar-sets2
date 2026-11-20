/* v3-branchless: v2 (block skip list, Eytzinger blocks) with a branchless
 * search inside the blocks; everything else as in v2.
 * P.-V. Khuong, P. Morin, "Array layouts for comparison-based searching",
 * ACM JEA 22, 2017.  License: GPL-3.0. */
#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_VERSION "v3-branchless"

#ifndef SL_BLOCK
#define SL_BLOCK 64         /* max keys per block */
#endif

#ifndef SL_MAX_LEVEL
#define SL_MAX_LEVEL 32
#endif

#ifndef SL_P
#define SL_P 0.25           /* chance of one more level */
#endif

typedef int   sl_key_t;
typedef void* sl_val_t;

typedef struct sl_block {
    sl_key_t min;               /* smallest key */
    int n;                      /* keys used */
    int cap;                    /* key capacity */
    int height;                 /* length of next[] */
    struct sl_block* next[];    /* then keys[cap], vals[cap] */
} sl_block;

typedef struct skiplist {
    sl_block* head;             /* no keys, grows to the top level */
    size_t size;
    int level;                  /* top level in use */
    uint32_t rng;
} skiplist;

typedef struct sl_iter {
    sl_block* b;
    int i;                      /* index in the block */
} sl_iter;

static inline sl_key_t* sl_keys(const sl_block* b)
{
    return (sl_key_t*)(b->next + b->height);
}

static inline sl_val_t* sl_vals(const sl_block* b)
{
    size_t k = ((size_t)b->cap * sizeof(sl_key_t) + sizeof(sl_val_t) - 1)
               / sizeof(sl_val_t) * sizeof(sl_val_t);
    return (sl_val_t*)((char*)sl_keys(b) + k);
}

/* Eytzinger order: node k (1-based) has children 2k and 2k+1.
 * Walking keeps the ifs of v2: their pattern is regular, so they are
 * predicted well. */
static inline int sl_eyt_first(int n)           /* index of the smallest */
{
    int k = 1;
    while (2 * k <= n) k *= 2;
    return k - 1;
}

static inline int sl_eyt_next(int n, int i)     /* next in order, -1 at end */
{
    int k = i + 1;
    if (2 * k + 1 <= n) {
        k = 2 * k + 1;
        while (2 * k <= n) k *= 2;
    } else {
        k >>= __builtin_ffs(~k);
    }
    return k - 1;
}

skiplist* sl_create(void);
void      sl_free(skiplist* sl);

/* 1 inserted, 0 value replaced, -1 out of memory */
int       sl_insert(skiplist* sl, sl_key_t key, sl_val_t val);

/* 1 removed (old value in *old_val), 0 not found */
int       sl_delete(skiplist* sl, sl_key_t key, sl_val_t* old_val);

/* NULL if not found */
sl_val_t  sl_search(const skiplist* sl, sl_key_t key);

/* seek: first key >= key; first: smallest key; next: next key.
 * Return 0 past the end. */
int       sl_seek(const skiplist* sl, sl_key_t key, sl_iter* it);
int       sl_first(const skiplist* sl, sl_iter* it);

static inline int sl_next(sl_iter* it)
{
    it->i = sl_eyt_next(it->b->n, it->i);
    if (it->i >= 0) return 1;
    it->b = it->b->next[0];
    if (!it->b) return 0;
    it->i = sl_eyt_first(it->b->n);
    return 1;
}

static inline sl_key_t sl_iter_key(const sl_iter* it) { return sl_keys(it->b)[it->i]; }
static inline sl_val_t sl_iter_val(const sl_iter* it) { return sl_vals(it->b)[it->i]; }

size_t    sl_size(const skiplist* sl);

/* bytes used, without malloc overhead */
size_t    sl_memory(const skiplist* sl);

#ifdef __cplusplus
}
#endif

#endif /* SKIPLIST_H */
