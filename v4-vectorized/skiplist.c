/* v4-vectorized: block skip list, SIMD search in sorted blocks. */
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

/* number of keys < x = index of the first key >= x (n if none).
 * No if and no early exit, so the compiler vectorizes the loop. */
static int block_lower(const sl_block* b, sl_key_t x)
{
    const sl_key_t* a = sl_keys(b);
    int n = b->n, c = 0;
    for (int i = 0; i < n; i++)
        c += a[i] < x;
    return c;
}

/* n sorted keys into b */
static void block_set(sl_block* b, const sl_key_t* k, const sl_val_t* v, int n)
{
    memcpy(sl_keys(b), k, (size_t)n * sizeof(sl_key_t));
    memcpy(sl_vals(b), v, (size_t)n * sizeof(sl_val_t));
    b->n = n;
    b->min = k[0];
}

/* insert at position p, b has room */
static void block_put(sl_block* b, int p, sl_key_t key, sl_val_t val)
{
    sl_key_t* keys = sl_keys(b);
    sl_val_t* vals = sl_vals(b);
    memmove(keys + p + 1, keys + p, (size_t)(b->n - p) * sizeof(sl_key_t));
    memmove(vals + p + 1, vals + p, (size_t)(b->n - p) * sizeof(sl_val_t));
    keys[p] = key;
    vals[p] = val;
    b->n++;
    b->min = keys[0];
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
    memcpy(nb->next, b->next, (size_t)b->height * sizeof(sl_block*));
    block_set(nb, sl_keys(b), sl_vals(b), b->n);
    relink(sl, b, nb);
    free(b);
    return nb;
}

static int insert_first(skiplist* sl, sl_key_t key, sl_val_t val)
{
    int h = random_level(sl) + 1;
    sl_block* c = block_new(h, 1);
    if (!c || grow_head(sl, h, NULL) < 0) { free(c); return -1; }
    block_set(c, &key, &val, 1);
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
    int n = b->n, p = block_lower(b, key);
    memcpy(k, sl_keys(b), (size_t)n * sizeof(sl_key_t));
    memcpy(v, sl_vals(b), (size_t)n * sizeof(sl_val_t));
    memmove(k + p + 1, k + p, (size_t)(n - p) * sizeof(sl_key_t));
    memmove(v + p + 1, v + p, (size_t)(n - p) * sizeof(sl_val_t));
    k[p] = key;
    v[p] = val;
    n++;
    int h = random_level(sl) + 1;
    sl_block* c = block_new(h, SL_BLOCK);
    if (!c || grow_head(sl, h, update) < 0) { free(c); return -1; }
    block_set(b, k, v, n / 2);
    block_set(c, k + n / 2, v + n / 2, n - n / 2);
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

/* every list gets its own random levels */
static uint32_t next_seed = 0x12345678u;

skiplist* sl_create(void)
{
    skiplist* sl = (skiplist*)calloc(1, sizeof(skiplist));
    if (!sl) return NULL;
    sl->rng = next_seed;
    next_seed = next_seed * 1664525u + 1013904223u;
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
        if (i < b->n && sl_keys(b)[i] == key) {
            sl_vals(b)[i] = val;
            return 0;
        }
    }
    if (b->n == b->cap) {
        if (b->cap == SL_BLOCK) return split_insert(sl, b, update, key, val);
        b = block_grow(sl, b);
        if (!b) return -1;
    }
    block_put(b, block_lower(b, key), key, val);
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
    sl_key_t* keys = sl_keys(b);
    sl_val_t* vals = sl_vals(b);
    if (i >= b->n || keys[i] != key) return 0;
    if (old_val) *old_val = vals[i];
    sl->size--;
    b->n--;
    memmove(keys + i, keys + i + 1, (size_t)(b->n - i) * sizeof(sl_key_t));
    memmove(vals + i, vals + i + 1, (size_t)(b->n - i) * sizeof(sl_val_t));
    if (b->n > 0) {
        b->min = keys[0];
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
    int n = b->n, i = block_lower(b, key);
    int j = i < n ? i : n - 1;
    uintptr_t hit = (uintptr_t)((i < n) & (sl_keys(b)[j] == key));
    return (sl_val_t)((uintptr_t)sl_vals(b)[j] & (0 - hit));   /* value or NULL, no if */
}

int sl_seek(const skiplist* sl, sl_key_t key, sl_iter* it)
{
    it->b = NULL;
    if (!sl) return 0;
    sl_block* b = find_block(sl, key);
    if (b != sl->head) {
        int i = block_lower(b, key);
        if (i < b->n) {
            it->b = b;
            it->i = i;
            return 1;
        }
    }
    it->b = b->next[0];
    it->i = 0;
    return it->b != NULL;
}

int sl_first(const skiplist* sl, sl_iter* it)
{
    it->b = sl ? sl->head->next[0] : NULL;
    it->i = 0;
    return it->b != NULL;
}

size_t sl_size(const skiplist* sl)
{
    return sl ? sl->size : 0;
}
