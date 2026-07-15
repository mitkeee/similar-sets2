/*-----------------------------------------------------------------------------
 * test-connector.c — connector API conformance test (issue #12).
 *
 * Prints a canonical trace of every connector operation that set2 relies on.
 * Build the SAME test twice:
 *
 *   conntest-base  = test-connector.o + connector.o        (original array)
 *   conntest-csl   = test-connector.o + connector_csl.o    (block skip list)
 *
 * and diff the outputs — they must be identical:
 *
 *   ./conntest-base > base.out; ./conntest-csl > csl.out; diff base.out csl.out
 *
 * Values are stored as REAL pointers (into vals[]) and read back through
 * link->val, which also exercises the key/value contract needed for the
 * set-trie integration (issue #21).
 *
 * Deliberately NOT tested: raw cursor indices (con_get_cursor) — set2.c never
 * reads them after con_lookup, and the adapter documents that divergence.
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "connector.h"

static int vals[64];

static void show(const char* op, link* l) {
    if (l) printf("%s -> key=%d val=%d\n", op, l->key, *(int*)l->val);
    else   printf("%s -> NULL\n", op);
}

int main(void) {
    char buf[64];
    connector* c = con_alloc();
    if (!c) { printf("con_alloc failed\n"); return 1; }

    /* --- empty store --- */
    printf("size(empty)=%d\n", con_size(c));
    con_open(c);
    printf("eos(empty)=%d\n", con_eos(c) ? 1 : 0);

    /* --- sorted bulk load via con_write: keys 0,2,4,...,18 --- */
    for (int i = 0; i < 10; i++) {
        vals[i] = i * 100;
        con_write(c, 2 * i, &vals[i]);
    }
    con_sort(c);
    printf("size(after write)=%d\n", con_size(c));

    /* --- out-of-order con_insert: odd keys 9,1,13,5,17 --- */
    int odd[] = { 9, 1, 13, 5, 17 };
    for (int i = 0; i < 5; i++) {
        vals[32 + i] = 1000 + odd[i];
        con_insert(c, odd[i], &vals[32 + i]);
    }
    printf("size(after insert)=%d\n", con_size(c));

    /* --- membership: hits and misses --- */
    int probe[] = { 0, 1, 3, 4, 5, 9, 10, 11, 18, 19, -1, 100 };
    for (int i = 0; i < 12; i++)
        printf("member(%d)=%d\n", probe[i], con_member(c, probe[i]) ? 1 : 0);

    /* --- lookup: value round-trip through the pointer --- */
    show("lookup(6)", con_lookup(c, 6));
    show("lookup(13)", con_lookup(c, 13));
    show("lookup(7)", con_lookup(c, 7));   /* miss */

    /* --- full forward iteration (order must be sorted) --- */
    printf("iterate:");
    con_open(c);
    while (!con_eos(c)) {
        link* li = con_read(c);
        if (!li) break;
        printf(" %d", li->key);
    }
    printf("\n");

    /* --- open_at an existing key: next reads return key, then successors --- */
    printf("open_at(8)=%d\n", con_open_at(c, 8) ? 1 : 0);
    for (int i = 0; i < 3; i++) { sprintf(buf, "read[%d]", i); show(buf, con_read(c)); }

    /* --- open_at a missing key returns false --- */
    printf("open_at(7)=%d\n", con_open_at(c, 7) ? 1 : 0);

    /* --- peek / read / peek_prev / read_prev sequence --- */
    con_open_at(c, 4);
    show("read", con_read(c));        /* 4 */
    show("read", con_read(c));        /* 5 */
    show("peek", con_peek(c));        /* 6, cursor unchanged */
    show("current", con_current(c));  /* 5 */
    show("peek_prev", con_peek_prev(c)); /* 4, cursor unchanged */
    show("read_prev", con_read_prev(c)); /* 4, cursor moves back */
    show("read", con_read(c));        /* 5 again */

    /* --- read to the very end returns NULL afterwards --- */
    con_open_at(c, 18);
    show("read", con_read(c));        /* 18 (largest key) */
    show("read", con_read(c));        /* NULL */
    printf("eos(end)=%d\n", con_eos(c) ? 1 : 0);

    /* --- print_keys --- */
    printf("print_keys:\n");
    con_print_keys(c, stdout);

    con_free(c);
    printf("OK\n");
    return 0;
}
