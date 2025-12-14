/*-----------------------------------------------------------------------------
 * Classic skip list implementation (baseline for cache-friendly variant)
 *----------------------------------------------------------------------------*/
#include "skiplist.h"
#include <stdlib.h>
#include <string.h>

/* Internal utilities */
static uint32_t sl_rand(skiplist* sl) {
    /* xorshift32 */
    uint32_t x = sl->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sl->rng = x ? x : 0xdeadbeef; /* avoid zero */
    return sl->rng;
}

static int random_level(skiplist* sl) {
    int lvl = 0;
    while (lvl < SL_MAX_LEVEL-1 && (double)(sl_rand(sl) & 0xFFFF) / 65536.0 < SL_P) {
        lvl++;
    }
    return lvl;
}

static sl_node* sl_node_alloc(sl_key_t key, sl_val_t val) {
    sl_node* n = (sl_node*)calloc(1, sizeof(sl_node));
    if (!n) return NULL;
    n->key = key; n->val = val;
    return n;
}

skiplist* sl_create(void) {
    skiplist* sl = (skiplist*)calloc(1, sizeof(skiplist));
    if (!sl) return NULL;
    sl->level = 0;
    sl->size = 0;
    sl->rng = 0x12345678; /* seed */
    sl->head = sl_node_alloc(INT32_MIN, NULL);
    return sl;
}

void sl_free(skiplist* sl, void (*free_val)(sl_val_t)) {
    if (!sl) return;
    sl_node* cur = sl->head;
    while (cur) {
        sl_node* nxt = cur->next[0];
        if (cur != sl->head && free_val) free_val(cur->val);
        free(cur);
        cur = nxt;
    }
    free(sl);
}

int sl_insert(skiplist* sl, sl_key_t key, sl_val_t val) {
    if (!sl) return -1;
    sl_node* update[SL_MAX_LEVEL];
    memset(update, 0, sizeof(update));
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (x && x->key == key) {
        x->val = val; /* update */
        return 0;
    }
    int lvl = random_level(sl);
    if (lvl > sl->level) {
        for (int i = sl->level + 1; i <= lvl; ++i) update[i] = sl->head;
        sl->level = lvl;
    }
    sl_node* n = sl_node_alloc(key, val);
    if (!n) return -1;
    for (int i = 0; i <= lvl; ++i) {
        n->next[i] = update[i]->next[i];
        update[i]->next[i] = n;
    }
    sl->size++;
    return 1;
}

int sl_delete(skiplist* sl, sl_key_t key, void (**out_val)(void)) {
    if (!sl) return 0;
    sl_node* update[SL_MAX_LEVEL];
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (!x || x->key != key) return 0;
    if (out_val) *(out_val) = (void(*)(void))x->val; /* raw cast */
    for (int i = 0; i <= sl->level; ++i) {
        if (update[i]->next[i] == x) update[i]->next[i] = x->next[i];
    }
    free(x);
    while (sl->level > 0 && !sl->head->next[sl->level]) sl->level--;
    sl->size--;
    return 1;
}

sl_val_t sl_search(skiplist* sl, sl_key_t key) {
    if (!sl) return NULL;
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
    }
    x = x->next[0];
    if (x && x->key == key) return x->val;
    return NULL;
}

sl_node* sl_first(skiplist* sl) {
    if (!sl) return NULL;
    return sl->head->next[0];
}

sl_node* sl_next_of(skiplist* sl, sl_key_t key) {
    if (!sl) return NULL;
    sl_node* x = sl->head;
    for (int i = sl->level; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key <= key) x = x->next[i];
    }
    return x->next[0];
}
