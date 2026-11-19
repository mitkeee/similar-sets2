/* v1-classic: classic skip list, one node per key.
 * W. Pugh, "Skip lists: a probabilistic alternative to balanced trees",
 * CACM 33(6), 1990.  License: GPL-3.0. */
#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_VERSION "v1-classic"

#ifndef SL_MAX_LEVEL
#define SL_MAX_LEVEL 32
#endif

#ifndef SL_P
#define SL_P 0.25           /* chance of one more level */
#endif

typedef int   sl_key_t;
typedef void* sl_val_t;

typedef struct sl_node {
    sl_key_t key;
    sl_val_t val;
    int height;                 /* length of next[] */
    struct sl_node* next[];
} sl_node;

typedef struct skiplist {
    int level;                  /* top level in use */
    size_t size;
    sl_node* head;              /* SL_MAX_LEVEL levels, no key */
    uint32_t rng;
} skiplist;

typedef struct sl_iter {
    sl_node* node;
} sl_iter;

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
    it->node = it->node->next[0];
    return it->node != NULL;
}

static inline sl_key_t sl_iter_key(const sl_iter* it) { return it->node->key; }
static inline sl_val_t sl_iter_val(const sl_iter* it) { return it->node->val; }

size_t    sl_size(const skiplist* sl);

/* bytes used, without malloc overhead */
size_t    sl_memory(const skiplist* sl);

#ifdef __cplusplus
}
#endif

#endif /* SKIPLIST_H */
