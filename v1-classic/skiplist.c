/* v1-classic: classic skip list. */
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

static sl_node* node_alloc(sl_key_t key, sl_val_t val, int height)
{
    sl_node* n = (sl_node*)calloc(1, sizeof(sl_node) + (size_t)height * sizeof(sl_node*));
    if (!n) return NULL;
    n->key = key;
    n->val = val;
    n->height = height;
    return n;
}

/* every list gets its own random levels */
static uint32_t next_seed = 0x12345678u;

skiplist* sl_create(void)
{
    skiplist* sl = (skiplist*)calloc(1, sizeof(skiplist));
    if (!sl) return NULL;
    sl->level = 0;
    sl->size = 0;
    sl->rng = next_seed;
    next_seed = next_seed * 1664525u + 1013904223u;
    sl->head = node_alloc(INT32_MIN, NULL, SL_MAX_LEVEL);
    if (!sl->head) { free(sl); return NULL; }
    return sl;
}

void sl_free(skiplist* sl)
{
    if (!sl) return;
    sl_node* cur = sl->head;
    while (cur) {
        sl_node* nxt = cur->next[0];
        free(cur);
        cur = nxt;
    }
    free(sl);
}

/* first node with key >= key, or NULL */
static sl_node* lower_bound(const skiplist* sl, sl_key_t key)
{
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key)
            x = x->next[i];
    }
    return x->next[0];
}

int sl_insert(skiplist* sl, sl_key_t key, sl_val_t val)
{
    if (!sl) return -1;
    /* update[i] = last node before key on level i */
    sl_node* update[SL_MAX_LEVEL];
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key)
            x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (x && x->key == key) {
        x->val = val;
        return 0;
    }

    int lvl = random_level(sl);
    if (lvl > sl->level) {
        for (int i = sl->level + 1; i <= lvl; ++i)
            update[i] = sl->head;
        sl->level = lvl;
    }
    sl_node* n = node_alloc(key, val, lvl + 1);
    if (!n) return -1;
    for (int i = 0; i <= lvl; ++i) {
        n->next[i] = update[i]->next[i];
        update[i]->next[i] = n;
    }
    sl->size++;
    return 1;
}

int sl_delete(skiplist* sl, sl_key_t key, sl_val_t* old_val)
{
    if (!sl) return 0;
    sl_node* update[SL_MAX_LEVEL];
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key)
            x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (!x || x->key != key) return 0;

    if (old_val) *old_val = x->val;
    for (int i = 0; i < x->height; ++i)
        update[i]->next[i] = x->next[i];
    free(x);
    while (sl->level > 0 && !sl->head->next[sl->level])
        sl->level--;
    sl->size--;
    return 1;
}

sl_val_t sl_search(const skiplist* sl, sl_key_t key)
{
    if (!sl) return NULL;
    sl_node* x = lower_bound(sl, key);
    return (x && x->key == key) ? x->val : NULL;
}

int sl_seek(const skiplist* sl, sl_key_t key, sl_iter* it)
{
    it->node = sl ? lower_bound(sl, key) : NULL;
    return it->node != NULL;
}

int sl_first(const skiplist* sl, sl_iter* it)
{
    it->node = sl ? sl->head->next[0] : NULL;
    return it->node != NULL;
}

size_t sl_size(const skiplist* sl)
{
    return sl ? sl->size : 0;
}
