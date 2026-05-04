#include "cskiplist.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*-----------------------------------------------------------------------------
 * SSE2 SIMD support for parallel key comparisons within blocks.
 *
 * When CSL_USE_SIMD is enabled (default on x86 with SSE2), we use 128-bit
 * SIMD registers to compare 4 keys simultaneously during intra-block search.
 *
 * The csl_kv struct is {int key, void* val} = 8 bytes per entry, so keys
 * are at stride-2-int positions.  We gather 4 keys from 4 consecutive
 * entries into one __m128i register using _mm_set_epi32, then compare all
 * 4 against the search key in a single instruction.
 *
 * Compile with: gcc -O3 -msse2 (or just -O3 on any modern x86)
 * Disable with: -DCSL_USE_SIMD=0
 *----------------------------------------------------------------------------*/
#if !defined(CSL_USE_SIMD)
  /* Auto-detect: enable SIMD on x86/x64 targets with SSE2 */
  #if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64) || \
      (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__i386__)
    #define CSL_USE_SIMD 1
  #else
    #define CSL_USE_SIMD 0
  #endif
#endif

#if CSL_USE_SIMD
  #include <emmintrin.h>  /* SSE2: _mm_set1_epi32, _mm_cmpeq_epi32, etc. */
#endif

/*
 * Threshold for choosing SIMD linear scan vs binary search.
 * For small blocks, brute-force SIMD scan (4 keys/cycle) beats binary search
 * because it avoids branch mispredictions.  For large blocks, binary search
 * O(log n) wins.  32 items = 8 SIMD iterations = very fast.
 */
#ifndef CSL_SIMD_SCAN_THRESHOLD
  #define CSL_SIMD_SCAN_THRESHOLD 32
#endif

/* Items per cache line for Eytzinger prefetch distance (64-byte cache line). */
#define EYT_ITEMS_PER_CL (64 / (int)sizeof(csl_kv))

/* Heuristic limits used by the TLB-aware block-cap helpers. */
#define CSL_MIN_BLOCK_CAP 4
#define CSL_TLB_AWARE_MAX_BLOCK_BYTES (16 * 1024)

/* Allocate a block with runtime-sized item and skip arrays. */
static csl_block* blk_alloc_with_cap(int item_cap, int skip_slots) {
    csl_block* b = (csl_block*)calloc(1, sizeof(csl_block));
    if (!b) return NULL;

    b->items = (item_cap > 0)
        ? (csl_kv*)calloc((size_t)item_cap, sizeof(csl_kv))
        : NULL;
    b->next = (csl_block**)calloc((size_t)skip_slots, sizeof(csl_block*));

    if ((item_cap > 0 && !b->items) || !b->next) {
        free(b->items);
        free(b->next);
        free(b);
        return NULL;
    }

    b->min_key = INT_MIN;
    b->count = 0;
    b->item_cap = item_cap;
    b->skip_alloc = skip_slots;
    b->prev = NULL;
    return b;
}

/* Allocate a data block with just 1 skip-pointer slot (level-0 link). */
static csl_block* blk_alloc(const cskiplist* sl) {
    return blk_alloc_with_cap(sl ? sl->block_cap : CSL_BLOCK_CAP, 1);
}

/* Allocate the head/sentinel block with full skip-pointer array. */
static csl_block* blk_alloc_head(void) {
    return blk_alloc_with_cap(0, CSL_MAX_LEVEL);
}

/* Ensure block has at least `needed` skip-pointer slots. */
static csl_block* blk_ensure_skips(csl_block* b, int needed) {
    csl_block** new_next;

    if (b->skip_alloc >= needed) return b;
    new_next = (csl_block**)realloc(b->next, (size_t)needed * sizeof(csl_block*));
    if (!new_next) return NULL;

    memset(&new_next[b->skip_alloc], 0,
           (size_t)(needed - b->skip_alloc) * sizeof(csl_block*));
    b->next = new_next;
    b->skip_alloc = needed;
    return b;
}

int csl_tlb_aware_block_cap_hint(int requested_block_cap) {
    int max_by_bytes = CSL_TLB_AWARE_MAX_BLOCK_BYTES / (int)sizeof(csl_kv);
    int capped = requested_block_cap;

    if (capped < CSL_MIN_BLOCK_CAP) capped = CSL_MIN_BLOCK_CAP;
    if (max_by_bytes >= CSL_MIN_BLOCK_CAP && capped > max_by_bytes)
        capped = max_by_bytes;
    return capped;
}

int csl_choose_block_cap_for_level(int trie_level) {
    int suggested;

    if (trie_level <= 0) suggested = 2048;
    else if (trie_level == 1) suggested = 1024;
    else if (trie_level == 2) suggested = 512;
    else if (trie_level <= 4) suggested = 256;
    else if (trie_level <= 8) suggested = 128;
    else if (trie_level <= 12) suggested = 64;
    else suggested = 32;

    return csl_tlb_aware_block_cap_hint(suggested);
}

cskiplist* csl_create_with_block_cap(int block_cap) {
    cskiplist* sl = (cskiplist*)calloc(1, sizeof(cskiplist));

    if (!sl) return NULL;
    sl->block_cap = csl_tlb_aware_block_cap_hint(block_cap);
    sl->head = blk_alloc_head();
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

cskiplist* csl_create_for_level(int trie_level) {
    return csl_create_with_block_cap(csl_choose_block_cap_for_level(trie_level));
}

cskiplist* csl_create(void) {
    return csl_create_with_block_cap(CSL_BLOCK_CAP);
}

int csl_get_block_cap(const cskiplist* sl) {
    return sl ? sl->block_cap : 0;
}

void csl_free(cskiplist* sl, void (*free_val)(csl_val_t)) {
    if (!sl) return;
    csl_block* cur = sl->head;
    while (cur) {
        csl_block* nxt = cur->next[0];
        if (cur != sl->head && free_val) {
            for (int i = 0; i < cur->count; ++i) free_val(cur->items[i].val);
        }
        free(cur->items);
        free(cur->next);
        free(cur);
        cur = nxt;
    }
    free(sl);
}

/* locate block with min_key <= key < next.min_key using top-down skip traversal */
static csl_block* locate_block(cskiplist* sl, csl_key_t key) {
    csl_block* x = sl->head;
    for (int lvl = sl->level; lvl >= 0; --lvl) {
        while (lvl < x->skip_alloc && x->next[lvl] &&
               x->next[lvl]->min_key <= key)
            x = x->next[lvl];
    }
    return x; /* x is the block whose min_key <= key, or head if before first */
}

static int blk_binary_search(csl_block* b, csl_key_t key) {
#if CSL_USE_SIMD
    /*-----------------------------------------------------------------
     * SIMD path: use SSE2 to compare 4 keys at once.
     *
     * For small blocks (count <= CSL_SIMD_SCAN_THRESHOLD), we do a
     * brute-force SIMD linear scan.  This is faster than binary search
     * because:
     *   1) No branch mispredictions (linear, predictable access pattern)
     *   2) 4 comparisons per SIMD instruction
     *   3) Sequential memory access is cache-prefetch friendly
     *
     * For larger blocks, we fall through to the scalar binary search
     * which has O(log n) comparisons but suffers branch mispredicts.
     *-----------------------------------------------------------------*/
    if (b->count <= CSL_SIMD_SCAN_THRESHOLD) {
        /* Broadcast the search key to all 4 lanes of a 128-bit register */
        __m128i vkey = _mm_set1_epi32(key);

        int i = 0;
        int count = b->count;

        /* Process 4 items at a time using SSE2.
         *
         * csl_kv layout (32-bit): [key0|val0|key1|val1|key2|val2|key3|val3]
         * Each csl_kv is 8 bytes; 4 entries = 32 bytes.
         *
         * We gather the 4 keys using _mm_set_epi32, which loads from
         * non-contiguous memory into one SIMD register.  The compiler
         * may optimize this into a shuffle on packed data.
         */
        for (; i + 3 < count; i += 4) {
            /* Gather 4 keys from 4 consecutive csl_kv entries */
            __m128i vkeys = _mm_set_epi32(
                b->items[i + 3].key,
                b->items[i + 2].key,
                b->items[i + 1].key,
                b->items[i + 0].key
            );

            /* Compare all 4 keys against search key simultaneously.
             * _mm_cmpeq_epi32: sets each 32-bit lane to 0xFFFFFFFF if equal */
            __m128i vcmp = _mm_cmpeq_epi32(vkeys, vkey);

            /* Collapse comparison result to a bitmask.
             * _mm_movemask_epi8: extracts the high bit of each byte.
             * For 4 int32 lanes, bits [3:0],[7:4],[11:8],[15:12] indicate
             * which lanes matched (4 bits per lane, all-1s if match). */
            int mask = _mm_movemask_epi8(vcmp);

            if (mask != 0) {
                /* At least one key matched — find which lane.
                 * Each matching lane sets 4 consecutive bits in the mask.
                 * __builtin_ctz gives the position of the lowest set bit.
                 * Divide by 4 to get the lane index (0-3). */
                int lane = __builtin_ctz(mask) >> 2;
                return i + lane;  /* exact match found */
            }

            /* Early exit optimization for sorted arrays: if the smallest
             * key in this group exceeds our search key, the key isn't here
             * (all remaining keys are even larger, since the array is sorted). */
            if (b->items[i].key > key) {
                return -(i + 1);   /* insertion point before items[i] */
            }
        }

        /* Scalar tail: handle remaining 0-3 items that don't fill a SIMD lane */
        for (; i < count; i++) {
            if (b->items[i].key == key) return i;
            if (b->items[i].key > key) return -(i + 1);
        }

        /* Key is larger than all items in the block */
        return -(count + 1);
    }
#endif /* CSL_USE_SIMD */

    /*-----------------------------------------------------------------
     * Branchless binary search (lower-bound) for larger sorted blocks.
     *
     * Uses conditional arithmetic that the compiler converts to CMOV
     * instructions, eliminating branch misprediction penalties entirely.
     * The loop body has NO branches — the pointer advance is computed
     * via a comparison result (0 or 1) multiplied by the half-step.
     *
     * Algorithm: narrow the search range by halving, always advancing
     * the base pointer by `half` when items[base+half] < key.
     * After the loop, base points at the lower-bound candidate.
     *
     * Reference: Khuong & Morin, "Array Layouts for Comparison-Based
     * Searching" (2015), §3.1 — "branchless binary search".
     *-----------------------------------------------------------------*/
    {
        const csl_kv* base = b->items;
        int n = b->count;
        while (n > 1) {
            int half = n >> 1;
            /* Branchless: compiler generates cmov for this ternary.
             * base advances by `half` iff base[half].key < key. */
            base = (base[half].key < key) ? base + half : base;
            n -= half;
        }
        /* base[0] is the lower-bound candidate.
         * If base[0] < key, the true lower bound is one position right. */
        int lo = (int)(base - b->items);
        if (lo < b->count && base->key < key) lo++;
        if (lo < b->count && b->items[lo].key == key)
            return lo;            /* exact match */
        return -(lo + 1);        /* insertion point */
    }
}

/*-----------------------------------------------------------------------------
 * Eytzinger (BFS / heap) layout for within-block search.
 *
 * Instead of storing items in sorted order, we store them in a BFS traversal
 * of an implicit binary search tree (0-indexed):
 *   root at 0, left child of i at 2i+1, right child at 2i+2.
 *
 * Benefits:
 *   - Branchless search: k = 2*k + 1 + (items[k].key < key)
 *   - Forward-only memory access pattern → prefetch-friendly
 *   - Optimal number of comparisons (same as binary search)
 *
 * Reference: Khuong & Morin, "Array Layouts for Comparison-Based Searching"
 *            (2015), https://arxiv.org/abs/1509.05053
 *----------------------------------------------------------------------------*/

/* --- Conversion: sorted ↔ Eytzinger --- */

/* Recursive in-order traversal to build Eytzinger layout from sorted array. */
static void eyt_build(const csl_kv* sorted, csl_kv* eyt, int n, int* si, int k) {
    if (k >= n) return;
    eyt_build(sorted, eyt, n, si, 2*k + 1);  /* left subtree */
    eyt[k] = sorted[(*si)++];                 /* root */
    eyt_build(sorted, eyt, n, si, 2*k + 2);  /* right subtree */
}

/* Convert a block's items[] from sorted order to Eytzinger BFS order. */
static void blk_sorted_to_eytzinger(csl_block* b) {
    csl_kv* tmp;
    int si = 0;

    if (b->count <= 1) return;
    tmp = (csl_kv*)malloc((size_t)b->count * sizeof(csl_kv));
    if (!tmp) return;
    memcpy(tmp, b->items, (size_t)b->count * sizeof(csl_kv));
    eyt_build(tmp, b->items, b->count, &si, 0);
    free(tmp);
}

/* Recursive in-order traversal to extract sorted array from Eytzinger. */
static void eyt_extract(const csl_kv* eyt, csl_kv* sorted, int n, int* si, int k) {
    if (k >= n) return;
    eyt_extract(eyt, sorted, n, si, 2*k + 1);
    sorted[(*si)++] = eyt[k];
    eyt_extract(eyt, sorted, n, si, 2*k + 2);
}

/* Convert a block's items[] from Eytzinger BFS order back to sorted order. */
static void blk_eytzinger_to_sorted(csl_block* b) {
    csl_kv* tmp;
    int si = 0;

    if (b->count <= 1) return;
    tmp = (csl_kv*)malloc((size_t)b->count * sizeof(csl_kv));
    if (!tmp) return;
    eyt_extract(b->items, tmp, b->count, &si, 0);
    memcpy(b->items, tmp, (size_t)b->count * sizeof(csl_kv));
    free(tmp);
}

/* --- Eytzinger branchless search --- */

/* EYT_ITEMS_PER_CL is defined in the top-of-file constants section. */

/*
 * Branchless search in Eytzinger-laid-out items[].
 * Returns: index >= 0 on exact match, -(lower_bound_index + 1) if not found.
 * The returned index is in Eytzinger space (direct items[] index).
 */
static int blk_eytzinger_search(csl_block* b, csl_key_t key) {
    int n = b->count;
    if (n == 0) return -(0 + 1);

    /* Branchless descent: k = 2k+1 if items[k] >= key, else 2k+2 */
    unsigned k = 0;
    while (k < (unsigned)n) {
        /* Prefetch grandchild area (a few levels ahead) */
        __builtin_prefetch(&b->items[k * EYT_ITEMS_PER_CL + EYT_ITEMS_PER_CL], 0, 1);
        k = 2*k + 1 + (unsigned)(b->items[k].key < key);
    }

    /* Recover the lower-bound position (0-indexed Eytzinger).
     * k is now past the leaves.  The answer is found by canceling
     * trailing right-turns in the binary representation of (k+1). */
    unsigned u = k + 1;
    int shift = __builtin_ffs((int)(~u));
    if (shift == 0) return -(n + 1);  /* all elements < key */
    int j = (int)(u >> shift) - 1;
    if (j < 0) return -(n + 1);       /* all elements < key */

    if (b->items[j].key == key) return j;  /* exact match */
    return -(j + 1);                       /* lower bound, not exact */
}

/* --- Eytzinger in-order traversal helpers for iterators --- */

/* Find the Eytzinger index of the minimum (leftmost) element. */
static int eyt_inorder_first(int n) {
    if (n <= 0) return -1;
    int k = 0;
    while (2*k + 1 < n) k = 2*k + 1;
    return k;
}

/* Find the Eytzinger index of the maximum (rightmost) element. */
static int eyt_inorder_last(int n) {
    if (n <= 0) return -1;
    int k = 0;
    while (2*k + 2 < n) k = 2*k + 2;
    return k;
}

/* In-order successor: returns -1 if k is the maximum element. */
static int eyt_inorder_succ(int k, int n) {
    /* If right child exists, go to leftmost of right subtree */
    int r = 2*k + 2;
    if (r < n) {
        k = r;
        while (2*k + 1 < n) k = 2*k + 1;
        return k;
    }
    /* Go up until we come from a left child */
    while (k > 0) {
        int parent = (k - 1) / 2;
        if (k == 2*parent + 1) return parent;  /* was left child */
        k = parent;
    }
    return -1;  /* no successor */
}

/* In-order predecessor: returns -1 if k is the minimum element. */
static int eyt_inorder_pred(int k, int n) {
    /* If left child exists, go to rightmost of left subtree */
    int l = 2*k + 1;
    if (l < n) {
        k = l;
        while (2*k + 2 < n) k = 2*k + 2;
        return k;
    }
    /* Go up until we come from a right child */
    while (k > 0) {
        int parent = (k - 1) / 2;
        if (k == 2*parent + 2) return parent;  /* was right child */
        k = parent;
    }
    return -1;  /* no predecessor */
}

int csl_append(cskiplist* sl, csl_key_t key, csl_val_t val) {
    if (!sl) return -1;
    /* Find tail block (last data block) */
    csl_block* tail = sl->head;
    while (tail->next[0]) tail = tail->next[0];

    if (tail == sl->head || tail->count >= sl->block_cap) {
        /* allocate new data block */
        csl_block* nb = blk_alloc(sl);
        if (!nb) return -1;
        nb->min_key = key;
        /* link at level 0 temporarily; others rebuilt later */
    tail->next[0] = nb;
        nb->prev = (tail == sl->head ? NULL : tail);
        sl->nblocks++;
        tail = nb;
    sl->level = 0; /* higher-level skips stale; force level-0 traversal until rebuild */
    }

    /* If Eytzinger, convert tail block to sorted for manipulation */
    int was_eyt = sl->eytzinger && tail != sl->head && tail->count > 1;
    if (was_eyt) blk_eytzinger_to_sorted(tail);

    /* enforce non-decreasing keys and sorted within block */
    if (tail->count > 0 && key < tail->items[tail->count - 1].key) {
        /* out-of-order append not supported in fast path */
        /* fallback: insert in-order via binary search within block if space */
        if (tail->count >= tail->item_cap) {
            if (was_eyt) blk_sorted_to_eytzinger(tail);
            return -1;
        }
        int pos = blk_binary_search(tail, key);
        if (pos >= 0) {
            tail->items[pos].val = val;
            if (sl->eytzinger) blk_sorted_to_eytzinger(tail);
            return 0;
        }
        pos = -pos - 1;
        memmove(&tail->items[pos+1], &tail->items[pos], (tail->count - pos) * sizeof(csl_kv));
        tail->items[pos].key = key;
        tail->items[pos].val = val;
        tail->count++;
        if (key < tail->min_key) tail->min_key = key;
        sl->size++;
        if (sl->eytzinger) blk_sorted_to_eytzinger(tail);
        return 1;
    }

    /* fast append */
    if (tail->count > 0 && tail->items[tail->count - 1].key == key) {
        tail->items[tail->count - 1].val = val;
        sl->stat_updates++;
        if (was_eyt) blk_sorted_to_eytzinger(tail);
        return 0;
    }
    tail->items[tail->count].key = key;
    tail->items[tail->count].val = val;
    tail->count++;
    if (tail->count == 1) tail->min_key = key;
    sl->size++;
    sl->stat_inserts++;
    if (sl->eytzinger) blk_sorted_to_eytzinger(tail);
    return 1;
}

csl_val_t csl_search(cskiplist* sl, csl_key_t key) {
    if (!sl) return NULL;
    csl_block* b = locate_block(sl, key);
    if (b == sl->head) b = b->next[0]; /* first data block */
    if (!b) return NULL;
    /* key could be in this block only if key >= min_key and < next.min_key */
    int idx = sl->eytzinger ? blk_eytzinger_search(b, key)
                            : blk_binary_search(b, key);
    if (idx >= 0) return b->items[idx].val;
    /* if not found and key >= next.min_key, move to next and check */
    if (b->next[0] && key >= b->next[0]->min_key) {
        b = b->next[0];
        idx = sl->eytzinger ? blk_eytzinger_search(b, key)
                            : blk_binary_search(b, key);
        if (idx >= 0) return b->items[idx].val;
    }
    return NULL;
}

/* helper: split a full block into two roughly equal halves. */
static csl_block* blk_split(cskiplist* sl, csl_block* b) {
    int right_cnt = b->count / 2;
    int left_cnt = b->count - right_cnt;
    csl_block* nb = blk_alloc(sl);
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
        csl_block* nb = blk_alloc(sl);
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

    /* If Eytzinger layout is active, convert block to sorted for manipulation */
    int was_eyt = sl->eytzinger && b->count > 1;
    if (was_eyt) blk_eytzinger_to_sorted(b);

    /* insert/update within block, splitting if needed */
    int pos = blk_binary_search(b, key);
    if (pos >= 0) {
        b->items[pos].val = val; sl->stat_updates++;
        if (was_eyt) blk_sorted_to_eytzinger(b);
        return 0;
    }
    pos = -pos - 1;
    if (b->count < b->item_cap) {
        memmove(&b->items[pos+1], &b->items[pos], (b->count - pos) * sizeof(csl_kv));
        b->items[pos].key = key;
        b->items[pos].val = val;
        b->count++;
        if (pos == 0) b->min_key = key;
        sl->size++;
        sl->stat_inserts++;
        if (sl->eytzinger) blk_sorted_to_eytzinger(b);
        return 1;
    }

    /* split and decide which block to insert into */
    csl_block* right = blk_split(sl, b);
    if (!right) { if (sl->eytzinger) blk_sorted_to_eytzinger(b); return -1; }
    csl_block* target;
    if (key >= right->min_key) {
        target = right;
        pos = blk_binary_search(target, key);
        if (pos >= 0) {
            target->items[pos].val = val; sl->stat_updates++;
            if (sl->eytzinger) { blk_sorted_to_eytzinger(b); blk_sorted_to_eytzinger(right); }
            return 0;
        }
        pos = -pos - 1;
        memmove(&target->items[pos+1], &target->items[pos], (target->count - pos) * sizeof(csl_kv));
        target->items[pos].key = key;
        target->items[pos].val = val;
        target->count++;
        if (pos == 0) target->min_key = key;
    } else {
        target = b;
        pos = blk_binary_search(target, key);
        if (pos >= 0) {
            target->items[pos].val = val; sl->stat_updates++;
            if (sl->eytzinger) { blk_sorted_to_eytzinger(b); blk_sorted_to_eytzinger(right); }
            return 0;
        }
        pos = -pos - 1;
        memmove(&target->items[pos+1], &target->items[pos], (target->count - pos) * sizeof(csl_kv));
        target->items[pos].key = key;
        target->items[pos].val = val;
        target->count++;
        if (pos == 0) target->min_key = key;
    }
    sl->size++;
    sl->stat_inserts++;
    if (sl->eytzinger) { blk_sorted_to_eytzinger(b); blk_sorted_to_eytzinger(right); }
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

    /* If Eytzinger, convert to sorted for manipulation */
    int was_eyt = sl->eytzinger && b->count > 1;
    if (was_eyt) blk_eytzinger_to_sorted(b);

    int idx = blk_binary_search(b, key);
    if (idx < 0) {
        if (was_eyt) blk_sorted_to_eytzinger(b);
        return 0;
    }
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
    } else {
        if (idx == 0) b->min_key = b->items[0].key;
        if (sl->eytzinger) blk_sorted_to_eytzinger(b);
    }
    return 1;
}

int csl_iter_first(cskiplist* sl, csl_iter* it) {
    if (!sl || !it) return 0;
    it->eytzinger = sl->eytzinger;
    csl_block* b = sl->head->next[0];
    if (!b || b->count == 0) { it->b = NULL; it->idx = -1; return 0; }
    it->b = b;
    it->idx = sl->eytzinger ? eyt_inorder_first(b->count) : 0;
    return 1;
}

int csl_iter_seek(cskiplist* sl, csl_key_t key, csl_iter* it, int* exact) {
    if (exact) *exact = 0;
    if (!sl || !it) return 0;
    it->eytzinger = sl->eytzinger;
    csl_block* b = sl->head->next[0];
    csl_block* last = NULL;
    while (b && b->min_key <= key) { last = b; b = b->next[0]; }
    csl_block* cand = last ? last : sl->head->next[0];
    if (!cand) { it->b = NULL; it->idx = -1; return 0; }
    int idx = sl->eytzinger ? blk_eytzinger_search(cand, key)
                            : blk_binary_search(cand, key);
    if (idx >= 0) { if (exact) *exact = 1; it->b = cand; it->idx = idx; return 1; }
    idx = -idx - 1; /* lower_bound in cand (in Eytzinger or sorted space) */
    if (idx < cand->count) { it->b = cand; it->idx = idx; return 1; }
    /* move to first of next block */
    if (cand->next[0]) {
        it->b = cand->next[0];
        it->idx = sl->eytzinger ? eyt_inorder_first(cand->next[0]->count) : 0;
        return 1;
    }
    it->b = NULL; it->idx = -1; return 0;
}

int csl_iter_next(csl_iter* it) {
    if (!it || !it->b) return 0;
    if (it->eytzinger) {
        int next = eyt_inorder_succ(it->idx, it->b->count);
        if (next >= 0) { it->idx = next; return 1; }
        /* move to next block */
        if (it->b->next[0]) {
            it->b = it->b->next[0];
            it->idx = eyt_inorder_first(it->b->count);
            return (it->idx >= 0);
        }
    } else {
        if (it->idx + 1 < it->b->count) { it->idx++; return 1; }
        if (it->b->next[0]) { it->b = it->b->next[0]; it->idx = 0; return 1; }
    }
    it->b = NULL; it->idx = -1; return 0;
}

int csl_iter_prev(cskiplist* sl, csl_iter* it) {
    (void)sl; /* sl not needed now, kept for API symmetry */
    if (!it || !it->b) return 0;
    if (it->eytzinger) {
        int prev = eyt_inorder_pred(it->idx, it->b->count);
        if (prev >= 0) { it->idx = prev; return 1; }
        /* move to previous block */
        if (it->b->prev) {
            it->b = it->b->prev;
            it->idx = eyt_inorder_last(it->b->count);
            return (it->idx >= 0);
        }
    } else {
        if (it->idx > 0) { it->idx--; return 1; }
        if (it->b->prev) {
            it->b = it->b->prev;
            it->idx = it->b->count - 1;
            return (it->idx >= 0);
        }
    }
    it->b = NULL; it->idx = -1; return 0;
}

void csl_rebuild_skips(cskiplist* sl) {
    if (!sl) return;
    /* collect blocks into array */
    size_t cap = sl->nblocks + 1;
    csl_block** arr = (csl_block**)malloc(sizeof(csl_block*) * cap);
    if (!arr) return;
    size_t m = 0;
    csl_block* cur = sl->head->next[0];
    while (cur) { arr[m++] = cur; cur = cur->next[0]; }

    /* determine levels: highest k such that 2^k <= m */
    int top = 0;
    while ((size_t)(1ull << (top+1)) <= m) ++top;
    sl->level = top;

    /*
     * Grow blocks that need higher skip levels.  A block at index i
     * participates in level k iff (i+1) is divisible by 2^k.  Its
     * required height = number-of-trailing-zeros(i+1) + 1, capped at
     * top+1.  After realloc we rebuild the entire level-0 chain and
     * prev pointers from arr[] so moved-block addresses are handled.
     */
    for (size_t i = 0; i < m; ++i) {
        int height = 1;
        { size_t v = i + 1; while ((v & 1) == 0 && height <= top) { v >>= 1; ++height; } }
        if (height > top + 1) height = top + 1;
        if (arr[i]->skip_alloc < height) {
            csl_block* nb = blk_ensure_skips(arr[i], height);
            if (nb) arr[i] = nb; /* may have moved */
        }
    }

    /* Rebuild level-0 chain and prev pointers from the (possibly moved) arr[] */
    sl->head->next[0] = (m > 0) ? arr[0] : NULL;
    for (size_t i = 0; i < m; ++i) {
        arr[i]->next[0] = (i + 1 < m) ? arr[i + 1] : NULL;
        arr[i]->prev = (i > 0) ? arr[i - 1] : NULL;
    }

    /* clear skip pointers above level 0 */
    for (size_t i = 0; i < m; ++i) {
        for (int lvl = 1; lvl < arr[i]->skip_alloc; ++lvl)
            arr[i]->next[lvl] = NULL;
    }

    /* rebuild higher levels deterministically */
    for (int lvl = 1; lvl <= top; ++lvl) {
        size_t stride = 1ull << lvl;
        csl_block* prev_blk = sl->head;
        for (size_t i = stride - 1; i < m; i += stride) {
            prev_blk->next[lvl] = arr[i];
            prev_blk = arr[i];
        }
        prev_blk->next[lvl] = NULL;
    }

    free(arr);
}

void csl_set_eytzinger(cskiplist* sl, int enable) {
    if (!sl) return;
    if (enable && !sl->eytzinger) {
        /* Convert all data blocks from sorted to Eytzinger layout */
        csl_block* b = sl->head->next[0];
        while (b) {
            blk_sorted_to_eytzinger(b);
            b = b->next[0];
        }
        sl->eytzinger = 1;
    } else if (!enable && sl->eytzinger) {
        /* Convert all data blocks from Eytzinger back to sorted */
        csl_block* b = sl->head->next[0];
        while (b) {
            blk_eytzinger_to_sorted(b);
            b = b->next[0];
        }
        sl->eytzinger = 0;
    }
}
