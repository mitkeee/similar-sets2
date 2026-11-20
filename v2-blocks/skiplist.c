/* v2-blocks: block skip list. */
#include "skiplist.h"
#include <stdlib.h>
#include <string.h>

/* xorshift32 */
static uint32_t sl_rand(skiplist* sl)
{
    uint32_t x = sl->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sl->rng = x ? x : 0xdeadbeefu;
    return sl->rng;
}

/* level k with probability p^k (1-p) */
static int random_level(skiplist* sl)
{
    int lvl = 0;
    while (lvl < SL_MAX_LEVEL - 1 && (double)(sl_rand(sl) & 0xFFFF) / 65536.0 < SL_P)
        lvl++;
    return lvl;
}

static size_t block_bytes(int height, int cap)
{
    size_t k = ((size_t)cap * sizeof(sl_key_t) + sizeof(sl_val_t) - 1)
               / sizeof(sl_val_t) * sizeof(sl_val_t);
    return sizeof(sl_block) + (size_t)height * sizeof(sl_block*)
           + k + (size_t)cap * sizeof(sl_val_t);
}

static sl_block* block_new(int height, int cap)
{
    sl_block* b = (sl_block*)malloc(block_bytes(height, cap));
    if (!b) return NULL;
    b->min = INT32_MIN;
    b->n = 0;
    b->cap = cap;
    b->height = height;
    memset(b->next, 0, (size_t)height * sizeof(sl_block*));
    return b;
}

/* keys of b in sorted order */
static int gather(const sl_block* b, sl_key_t* k, sl_val_t* v)
{
    const sl_key_t* keys = sl_keys(b);
    const sl_val_t* vals = sl_vals(b);
    int j = 0;
    for (int i = sl_eyt_first(b->n); i >= 0; i = sl_eyt_next(b->n, i)) {
        k[j] = keys[i];
        v[j] = vals[i];
        j++;
    }
    return j;
}

/* n >= 1 sorted keys into b, in Eytzinger order */
static void scatter(sl_block* b, const sl_key_t* k, const sl_val_t* v, int n)
{
    sl_key_t* keys = sl_keys(b);
    sl_val_t* vals = sl_vals(b);
    int j = 0;
    for (int i = sl_eyt_first(n); i >= 0; i = sl_eyt_next(n, i)) {
        keys[i] = k[j];
        vals[i] = v[j];
        j++;
    }
    b->n = n;
    b->min = k[0];
}

/* index of the first key >= x in b, -1 if none */
static int block_lower(const sl_block* b, sl_key_t x)
{
    const sl_key_t* a = sl_keys(b);
    int n = b->n, k = 1;
    while (k <= n) {
        if (x < a[k - 1]) k = 2 * k;
        else if (x > a[k - 1]) k = 2 * k + 1;
        else return k - 1;
    }
    k >>= __builtin_ffs(~k);
    return k - 1;
}

/* last block with min <= key, or the head */
static sl_block* find_block(const skiplist* sl, sl_key_t key)
{
    sl_block* x = sl->head;
    for (int i = sl->level; i >= 0; --i)
        while (x->next[i] && x->next[i]->min <= key)
            x = x->next[i];
    return x;
}

/* head at least h levels high; update[] is fixed if given */
static int grow_head(skiplist* sl, int h, sl_block** update)
{
    sl_block* old = sl->head;
    if (h <= old->height) return 0;
    int nh = old->height * 2;
    if (nh < h) nh = h;
    if (nh > SL_MAX_LEVEL) nh = SL_MAX_LEVEL;
    sl_block* nb = block_new(nh, 0);
    if (!nb) return -1;
    memcpy(nb->next, old->next, (size_t)old->height * sizeof(sl_block*));
    if (update)
        for (int i = 0; i <= sl->level; i++)
            if (update[i] == old) update[i] = nb;
    sl->head = nb;
    free(old);
    return 0;
}

/* links to old now point to nb */
static void relink(skiplist* sl, const sl_block* old, sl_block* nb)
{
    sl_block* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->min < old->min)
            x = x->next[i];
        if (x->next[i] == old) x->next[i] = nb;
    }
}

/* same block with double capacity */
static sl_block* block_grow(skiplist* sl, sl_block* b)
{
    int cap = b->cap * 2 < SL_BLOCK ? b->cap * 2 : SL_BLOCK;
    sl_block* nb = block_new(b->height, cap);
    if (!nb) return NULL;
    nb->min = b->min;
    nb->n = b->n;
    memcpy(nb->next, b->next, (size_t)b->height * sizeof(sl_block*));
    memcpy(sl_keys(nb), sl_keys(b), (size_t)b->n * sizeof(sl_key_t));
    memcpy(sl_vals(nb), sl_vals(b), (size_t)b->n * sizeof(sl_val_t));
    relink(sl, b, nb);
    free(b);
    return nb;
}

/* sorted insert into k[0..n) */
static int put(sl_key_t* k, sl_val_t* v, int n, sl_key_t key, sl_val_t val)
{
    int p = n;
    while (p > 0 && k[p - 1] > key) {
        k[p] = k[p - 1];
        v[p] = v[p - 1];
        p--;
    }
    k[p] = key;
    v[p] = val;
    return n + 1;
}

static int insert_first(skiplist* sl, sl_key_t key, sl_val_t val)
{
    int h = random_level(sl) + 1;
    sl_block* c = block_new(h, 1);
    if (!c || grow_head(sl, h, NULL) < 0) { free(c); return -1; }
    scatter(c, &key, &val, 1);
    for (int i = 0; i < h; i++) sl->head->next[i] = c;
    sl->level = h - 1;
    sl->size = 1;
    return 1;
}

/* b is full: split it in two halves, c goes right after b */
static int split_insert(skiplist* sl, sl_block* b, sl_block** update,
                        sl_key_t key, sl_val_t val)
{
    sl_key_t k[SL_BLOCK + 1];
    sl_val_t v[SL_BLOCK + 1];
    int n = put(k, v, gather(b, k, v), key, val);
    int h = random_level(sl) + 1;
    sl_block* c = block_new(h, SL_BLOCK);
    if (!c || grow_head(sl, h, update) < 0) { free(c); return -1; }
    scatter(b, k, v, n / 2);
    scatter(c, k + n / 2, v + n / 2, n - n / 2);
    for (int i = sl->level + 1; i < h; i++) update[i] = sl->head;
    if (h - 1 > sl->level) sl->level = h - 1;
    for (int i = 0; i < h; i++) {
        sl_block* pred = i < b->height ? b : update[i];
        c->next[i] = pred->next[i];
        pred->next[i] = c;
    }
    sl->size++;
    return 1;
}

skiplist* sl_create(void)
{
    skiplist* sl = (skiplist*)calloc(1, sizeof(skiplist));
    if (!sl) return NULL;
    sl->rng = 0x12345678u;
    sl->head = block_new(1, 0);
    if (!sl->head) { free(sl); return NULL; }
    return sl;
}

void sl_free(skiplist* sl)
{
    if (!sl) return;
    sl_block* b = sl->head;
    while (b) {
        sl_block* nxt = b->next[0];
        free(b);
        b = nxt;
    }
    free(sl);
}

int sl_insert(skiplist* sl, sl_key_t key, sl_val_t val)
{
    if (!sl) return -1;
    sl_block* update[SL_MAX_LEVEL];
    sl_block* b = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (b->next[i] && b->next[i]->min <= key)
            b = b->next[i];
        update[i] = b;
    }
    if (b == sl->head) {
        b = b->next[0];                     /* key is before every block */
        if (!b) return insert_first(sl, key, val);
    } else {
        int i = block_lower(b, key);
        if (i >= 0 && sl_keys(b)[i] == key) {
            sl_vals(b)[i] = val;
            return 0;
        }
    }
    if (b->n == b->cap) {
        if (b->cap == SL_BLOCK) return split_insert(sl, b, update, key, val);
        b = block_grow(sl, b);
        if (!b) return -1;
    }
    sl_key_t k[SL_BLOCK];
    sl_val_t v[SL_BLOCK];
    scatter(b, k, v, put(k, v, gather(b, k, v), key, val));
    sl->size++;
    return 1;
}

int sl_delete(skiplist* sl, sl_key_t key, sl_val_t* old_val)
{
    if (!sl) return 0;
    sl_block* update[SL_MAX_LEVEL];
    sl_block* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->min < key)
            x = x->next[i];
        update[i] = x;
    }
    sl_block* b = x->next[0];
    if (!b || b->min != key) b = x;
    if (b == sl->head) return 0;
    int i = block_lower(b, key);
    if (i < 0 || sl_keys(b)[i] != key) return 0;
    if (old_val) *old_val = sl_vals(b)[i];
    sl->size--;

    sl_key_t k[SL_BLOCK];
    sl_val_t v[SL_BLOCK];
    int n = gather(b, k, v), p = 0;
    while (k[p] != key) p++;
    memmove(k + p, k + p + 1, (size_t)(n - 1 - p) * sizeof(sl_key_t));
    memmove(v + p, v + p + 1, (size_t)(n - 1 - p) * sizeof(sl_val_t));
    if (n > 1) {
        scatter(b, k, v, n - 1);
        return 1;
    }
    /* empty: min was key, so update[] are its predecessors */
    for (int l = 0; l < b->height; l++)
        update[l]->next[l] = b->next[l];
    free(b);
    while (sl->level > 0 && !sl->head->next[sl->level])
        sl->level--;
    return 1;
}

sl_val_t sl_search(const skiplist* sl, sl_key_t key)
{
    if (!sl) return NULL;
    const sl_block* b = find_block(sl, key);
    if (b == sl->head) return NULL;
    int i = block_lower(b, key);
    return (i >= 0 && sl_keys(b)[i] == key) ? sl_vals(b)[i] : NULL;
}

int sl_seek(const skiplist* sl, sl_key_t key, sl_iter* it)
{
    it->b = NULL;
    if (!sl) return 0;
    sl_block* b = find_block(sl, key);
    if (b != sl->head) {
        int i = block_lower(b, key);
        if (i >= 0) {
            it->b = b;
            it->i = i;
            return 1;
        }
    }
    b = b->next[0];
    if (!b) return 0;
    it->b = b;
    it->i = sl_eyt_first(b->n);
    return 1;
}

int sl_first(const skiplist* sl, sl_iter* it)
{
    it->b = sl ? sl->head->next[0] : NULL;
    if (!it->b) return 0;
    it->i = sl_eyt_first(it->b->n);
    return 1;
}

size_t sl_size(const skiplist* sl)
{
    return sl ? sl->size : 0;
}

size_t sl_memory(const skiplist* sl)
{
    if (!sl) return 0;
    size_t bytes = sizeof(skiplist);
    for (const sl_block* b = sl->head; b; b = b->next[0])
        bytes += block_bytes(b->height, b->cap);
    return bytes;
}
