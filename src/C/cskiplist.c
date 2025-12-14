#include "cskiplist.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static csl_block* blk_alloc(void) {
    csl_block* b = (csl_block*)calloc(1, sizeof(csl_block));
    if (!b) return NULL;
    b->min_key = INT_MIN;
    b->count = 0;
    b->prev = NULL;
    memset(b->next, 0, sizeof(b->next));
    return b;
}

cskiplist* csl_create(void) {
    cskiplist* sl = (cskiplist*)calloc(1, sizeof(cskiplist));
    if (!sl) return NULL;
    sl->head = blk_alloc();
    if (!sl->head) { free(sl); return NULL; }
    sl->level = 0;
    sl->nblocks = 0;
    sl->size = 0;
    sl->stat_inserts = 0;
    sl->stat_updates = 0;
    sl->stat_deletes = 0;
    sl->stat_splits = 0;
    return sl;
}

void csl_free(cskiplist* sl, void (*free_val)(csl_val_t)) {
    if (!sl) return;
    csl_block* cur = sl->head;
    while (cur) {
        csl_block* nxt = cur->next[0];
        if (cur != sl->head && free_val) {
            for (int i = 0; i < cur->count; ++i) free_val(cur->items[i].val);
        }
        free(cur);
        cur = nxt;
    }
    free(sl);
}

/* locate block with min_key <= key < next.min_key using top-down skip traversal */
static csl_block* locate_block(cskiplist* sl, csl_key_t key) {
    csl_block* x = sl->head;
    for (int lvl = sl->level; lvl >= 0; --lvl) {
        while (x->next[lvl] && x->next[lvl]->min_key <= key) x = x->next[lvl];
    }
    return x; /* x is the block whose min_key <= key, or head if before first */
}

static int blk_binary_search(csl_block* b, csl_key_t key) {
    int lo = 0, hi = b->count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (b->items[mid].key < key) lo = mid + 1;
        else if (b->items[mid].key > key) hi = mid - 1;
        else return mid;
    }
    return -(lo + 1); /* insertion point */
}

int csl_append(cskiplist* sl, csl_key_t key, csl_val_t val) {
    if (!sl) return -1;
    /* Find tail block (last data block) */
    csl_block* tail = sl->head;
    while (tail->next[0]) tail = tail->next[0];

    if (tail == sl->head || tail->count >= CSL_BLOCK_CAP) {
        /* allocate new data block */
        csl_block* nb = blk_alloc();
        if (!nb) return -1;
        nb->min_key = key;
        /* link at level 0 temporarily; others rebuilt later */
    tail->next[0] = nb;
        nb->prev = (tail == sl->head ? NULL : tail);
        sl->nblocks++;
        tail = nb;
    sl->level = 0; /* higher-level skips stale; force level-0 traversal until rebuild */
    }

    /* enforce non-decreasing keys and sorted within block */
    if (tail->count > 0 && key < tail->items[tail->count - 1].key) {
        /* out-of-order append not supported in fast path */
        /* fallback: insert in-order via binary search within block if space */
        if (tail->count >= CSL_BLOCK_CAP) return -1;
        int pos = blk_binary_search(tail, key);
        if (pos >= 0) { tail->items[pos].val = val; return 0; }
        pos = -pos - 1;
        memmove(&tail->items[pos+1], &tail->items[pos], (tail->count - pos) * sizeof(csl_kv));
        tail->items[pos].key = key;
        tail->items[pos].val = val;
        tail->count++;
        if (key < tail->min_key) tail->min_key = key;
        sl->size++;
        return 1;
    }

    /* fast append */
    if (tail->count > 0 && tail->items[tail->count - 1].key == key) {
        tail->items[tail->count - 1].val = val;
        sl->stat_updates++;
        return 0;
    }
    tail->items[tail->count].key = key;
    tail->items[tail->count].val = val;
    tail->count++;
    if (tail->count == 1) tail->min_key = key;
    sl->size++;
    sl->stat_inserts++;
    return 1;
}

csl_val_t csl_search(cskiplist* sl, csl_key_t key) {
    if (!sl) return NULL;
    csl_block* b = locate_block(sl, key);
    if (b == sl->head) b = b->next[0]; /* first data block */
    if (!b) return NULL;
    /* key could be in this block only if key >= min_key and < next.min_key */
    int idx = blk_binary_search(b, key);
    if (idx >= 0) return b->items[idx].val;
    /* if not found and key >= next.min_key, move to next and check */
    if (b->next[0] && key >= b->next[0]->min_key) {
        b = b->next[0];
        idx = blk_binary_search(b, key);
        if (idx >= 0) return b->items[idx].val;
    }
    return NULL;
}

/* helper: split a full block into two roughly equal halves; assumes b->count == CSL_BLOCK_CAP */
static csl_block* blk_split(cskiplist* sl, csl_block* b) {
    int right_cnt = b->count / 2;
    int left_cnt = b->count - right_cnt;
    csl_block* nb = blk_alloc();
    if (!nb) return NULL;
    /* move right half into nb */
    memcpy(nb->items, &b->items[left_cnt], right_cnt * sizeof(csl_kv));
    nb->count = right_cnt;
    nb->min_key = nb->items[0].key;
    /* fix left block count */
    b->count = left_cnt;
    b->min_key = (left_cnt > 0) ? b->items[0].key : INT_MIN;
    /* splice nb after b at level 0 */
    nb->next[0] = b->next[0];
    b->next[0] = nb;
    nb->prev = b;
    if (nb->next[0]) nb->next[0]->prev = nb;
    sl->nblocks++;
    sl->stat_splits++;
    sl->level = 0;
    return nb;
}

int csl_insert(cskiplist* sl, csl_key_t key, csl_val_t val) {
    if (!sl) return -1;
    /* find candidate block by linear scan on level 0 to avoid stale skips */
    csl_block* prev = sl->head;
    csl_block* cur = sl->head->next[0];
    while (cur && cur->min_key <= key) { prev = cur; cur = cur->next[0]; }
    /* candidate is prev (may be head) */
    csl_block* b = (prev == sl->head) ? sl->head->next[0] : prev;
    if (!b || key < b->min_key) {
        /* need to insert into new block before b, or start a new first block */
        csl_block* nb = blk_alloc();
        if (!nb) return -1;
        nb->min_key = key;
        nb->next[0] = (prev == sl->head) ? sl->head->next[0] : prev->next[0];
        if (prev == sl->head) sl->head->next[0] = nb; else prev->next[0] = nb;
        nb->prev = (prev == sl->head ? NULL : prev);
        if (nb->next[0]) nb->next[0]->prev = nb;
        sl->nblocks++;
        b = nb;
        sl->level = 0;
    }

    /* insert/update within block, splitting if needed */
    int pos = blk_binary_search(b, key);
    if (pos >= 0) { b->items[pos].val = val; sl->stat_updates++; return 0; }
    pos = -pos - 1;
    if (b->count < CSL_BLOCK_CAP) {
        memmove(&b->items[pos+1], &b->items[pos], (b->count - pos) * sizeof(csl_kv));
        b->items[pos].key = key;
        b->items[pos].val = val;
        b->count++;
        if (pos == 0) b->min_key = key;
        sl->size++;
        sl->stat_inserts++;
        return 1;
    }

    /* split and decide which block to insert into */
    csl_block* right = blk_split(sl, b);
    if (!right) return -1;
    csl_block* target;
    if (key >= right->min_key) {
        target = right;
        pos = blk_binary_search(target, key);
        if (pos >= 0) { target->items[pos].val = val; sl->stat_updates++; return 0; }
        pos = -pos - 1;
        memmove(&target->items[pos+1], &target->items[pos], (target->count - pos) * sizeof(csl_kv));
        target->items[pos].key = key;
        target->items[pos].val = val;
        target->count++;
        if (pos == 0) target->min_key = key;
    } else {
        target = b;
        pos = blk_binary_search(target, key);
        if (pos >= 0) { target->items[pos].val = val; sl->stat_updates++; return 0; }
        pos = -pos - 1;
        memmove(&target->items[pos+1], &target->items[pos], (target->count - pos) * sizeof(csl_kv));
        target->items[pos].key = key;
        target->items[pos].val = val;
        target->count++;
        if (pos == 0) target->min_key = key;
    }
    sl->size++;
    sl->stat_inserts++;
    return 1;
}

int csl_delete(cskiplist* sl, csl_key_t key, void (*free_val)(csl_val_t)) {
    if (!sl) return 0;
    /* linear search across level 0 */
    csl_block* prev = sl->head;
    csl_block* b = sl->head->next[0];
    while (b && b->min_key <= key) {
        if (b->next[0] && key >= b->next[0]->min_key) { prev = b; b = b->next[0]; continue; }
        break;
    }
    if (!b) return 0;
    int idx = blk_binary_search(b, key);
    if (idx < 0) return 0;
    if (free_val) free_val(b->items[idx].val);
    memmove(&b->items[idx], &b->items[idx+1], (b->count - idx - 1) * sizeof(csl_kv));
    b->count--;
    sl->size--;
    sl->stat_deletes++;
    if (b->count == 0) {
        /* remove empty block */
        if (prev) prev->next[0] = b->next[0];
        if (b->next[0]) b->next[0]->prev = (prev == sl->head ? NULL : prev);
        free(b);
        sl->nblocks--;
        sl->level = 0;
    } else if (idx == 0) {
        b->min_key = b->items[0].key;
    }
    return 1;
}

int csl_iter_first(cskiplist* sl, csl_iter* it) {
    if (!sl || !it) return 0;
    csl_block* b = sl->head->next[0];
    if (!b || b->count == 0) { it->b = NULL; it->idx = -1; return 0; }
    it->b = b; it->idx = 0; return 1;
}

int csl_iter_seek(cskiplist* sl, csl_key_t key, csl_iter* it, int* exact) {
    if (exact) *exact = 0;
    if (!sl || !it) return 0;
    csl_block* b = sl->head->next[0];
    csl_block* last = NULL;
    while (b && b->min_key <= key) { last = b; b = b->next[0]; }
    csl_block* cand = last ? last : sl->head->next[0];
    if (!cand) { it->b = NULL; it->idx = -1; return 0; }
    int idx = blk_binary_search(cand, key);
    if (idx >= 0) { if (exact) *exact = 1; it->b = cand; it->idx = idx; return 1; }
    idx = -idx - 1; /* lower_bound in cand */
    if (idx < cand->count) { it->b = cand; it->idx = idx; return 1; }
    /* move to first of next block */
    if (cand->next[0]) { it->b = cand->next[0]; it->idx = 0; return 1; }
    it->b = NULL; it->idx = -1; return 0;
}

int csl_iter_next(csl_iter* it) {
    if (!it || !it->b) return 0;
    if (it->idx + 1 < it->b->count) { it->idx++; return 1; }
    if (it->b->next[0]) { it->b = it->b->next[0]; it->idx = 0; return 1; }
    it->b = NULL; it->idx = -1; return 0;
}

int csl_iter_prev(cskiplist* sl, csl_iter* it) {
    (void)sl; /* sl not needed now, kept for API symmetry */
    if (!it || !it->b) return 0;
    if (it->idx > 0) { it->idx--; return 1; }
    if (!it->b->prev) { it->b = NULL; it->idx = -1; return 0; }
    it->b = it->b->prev;
    it->idx = it->b->count - 1;
    return (it->idx >= 0);
}

void csl_rebuild_skips(cskiplist* sl) {
    if (!sl) return;
    /* collect blocks into array */
    size_t cap = sl->nblocks + 1;
    csl_block** arr = (csl_block**)malloc(sizeof(csl_block*) * (cap));
    if (!arr) return;
    size_t m = 0;
    csl_block* cur = sl->head->next[0];
    while (cur) { arr[m++] = cur; cur = cur->next[0]; }

    /* determine levels: highest k such that 2^k <= m */
    int top = 0;
    while ((size_t)(1ull << (top+1)) <= m) ++top;
    sl->level = top;

    /* clear all pointers above level 0 */
    for (size_t i = 0; i < m; ++i) memset(arr[i]->next + 1, 0, (CSL_MAX_LEVEL-1) * sizeof(csl_block*));

    /* link level 0 chain already exists; rebuild higher levels deterministically */
    for (int lvl = 1; lvl <= top; ++lvl) {
        size_t stride = 1ull << lvl;
        csl_block* prev = sl->head;
        for (size_t i = stride - 1; i < m; i += stride) {
            prev->next[lvl] = arr[i];
            prev = arr[i];
        }
        prev->next[lvl] = NULL;
    }

    free(arr);
}
