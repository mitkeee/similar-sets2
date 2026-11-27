/* test.c: random insert/delete/search checked against a reference array,
 * full check every BATCH operations.  Prints "OK <version>" or "FAIL ...". */
#define __USE_MINGW_ANSI_STDIO 1   /* MinGW: C99 printf */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "skiplist.h"

#define LO    (-3000)           /* keys are in [LO, HI) */
#define HI    3000
#define RANGE (HI - LO)
#define OPS   400000
#define BATCH 5000

static uint64_t rng_state = 7;
static uint64_t rnd(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int present[RANGE];      /* reference contents */
static intptr_t value[RANGE];

#define FAIL(...) do { printf("FAIL %s: ", SL_VERSION); printf(__VA_ARGS__); \
                       printf("\n"); exit(1); } while (0)

static void check_all(const skiplist* sl, size_t expect_size)
{
    if (sl_size(sl) != expect_size)
        FAIL("size %zu, expected %zu", sl_size(sl), expect_size);

    /* full walk */
    sl_iter it;
    int on = sl_first(sl, &it);
    for (int i = 0; i < RANGE; i++) {
        if (!present[i]) continue;
        if (!on) FAIL("walk ended early, missing key %d", LO + i);
        if (sl_iter_key(&it) != LO + i)
            FAIL("walk gave key %d, expected %d", sl_iter_key(&it), LO + i);
        if ((intptr_t)sl_iter_val(&it) != value[i])
            FAIL("walk gave wrong value for key %d", LO + i);
        on = sl_next(&it);
    }
    if (on) FAIL("walk gave extra key %d", sl_iter_key(&it));

    /* random seeks, also outside the range */
    for (int t = 0; t < 200; t++) {
        int x = LO - 5 + (int)(rnd() % (RANGE + 10));
        int i = x - LO;
        if (i < 0) i = 0;
        while (i < RANGE && !present[i]) i++;
        int found = sl_seek(sl, x, &it);
        if (i == RANGE && found) FAIL("seek(%d) found %d, expected end", x, sl_iter_key(&it));
        if (i < RANGE && !found) FAIL("seek(%d) found nothing, expected %d", x, LO + i);
        if (i < RANGE && sl_iter_key(&it) != LO + i)
            FAIL("seek(%d) gave %d, expected %d", x, sl_iter_key(&it), LO + i);
    }
}

int main(void)
{
    skiplist* sl = sl_create();
    if (!sl) FAIL("sl_create returned NULL");
    size_t size = 0;
    check_all(sl, 0);

    for (int op = 1; op <= OPS; op++) {
        int i = (int)(rnd() % RANGE);
        int key = LO + i;
        int what = (int)(rnd() % 10);
        if (what < 5) {                                 /* insert or replace */
            intptr_t v = (intptr_t)(rnd() % 1000000) + 1;
            int r = sl_insert(sl, key, (sl_val_t)v);
            if (r != (present[i] ? 0 : 1)) FAIL("insert(%d) returned %d", key, r);
            if (!present[i]) size++;
            present[i] = 1;
            value[i] = v;
        } else if (what < 8) {                          /* delete */
            sl_val_t old = NULL;
            int r = sl_delete(sl, key, &old);
            if (r != present[i]) FAIL("delete(%d) returned %d", key, r);
            if (r && (intptr_t)old != value[i]) FAIL("delete(%d) returned wrong value", key);
            if (r) size--;
            present[i] = 0;
        } else {                                        /* search */
            sl_val_t v = sl_search(sl, key);
            intptr_t expect = present[i] ? value[i] : 0;
            if ((intptr_t)v != expect) FAIL("search(%d) wrong", key);
        }
        if (op % BATCH == 0) check_all(sl, size);
    }

    /* empty and reuse */
    for (int i = 0; i < RANGE; i++)
        if (present[i]) { sl_delete(sl, LO + i, NULL); present[i] = 0; }
    check_all(sl, 0);
    if (sl_insert(sl, 42, (sl_val_t)(intptr_t)1) != 1 || sl_search(sl, 42) == NULL)
        FAIL("insert after emptying");
    sl_free(sl);

    printf("OK %s (%d random operations)\n", SL_VERSION, OPS);
    return 0;
}
