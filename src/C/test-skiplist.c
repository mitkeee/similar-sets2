/* Basic test harness for classic skip list */
#include "skiplist.h"
#include <stdio.h>
#include <stdlib.h>

static void free_val(void* v) { /* nothing for now */ }

int main(void) {
    skiplist* sl = sl_create();
    if (!sl) { fprintf(stderr, "Failed to create skiplist\n"); return 1; }

    /* Insert range */
    for (int i = 0; i < 20; ++i) {
        if (sl_insert(sl, i, (void*)(intptr_t)(i*10 + 1)) < 0) {
            fprintf(stderr, "OOM at insert %d\n", i); return 2; }
    }

    /* Search */
    for (int i = 0; i < 20; ++i) {
        void* v = sl_search(sl, i);
            if (v == NULL) { fprintf(stderr, "Missing key %d\n", i); return 3; }
        printf("key=%d val=%ld\n", i, (long)(intptr_t)v);
    }

    /* Update */
    sl_insert(sl, 5, (void*)(intptr_t)555);
    printf("updated key 5 => %ld\n", (long)(intptr_t)sl_search(sl,5));

    /* Delete */
    sl_delete(sl, 0, NULL);
        if (sl_search(sl,0) != NULL) { fprintf(stderr, "Delete failed for key 0\n"); return 4; }

    /* Iterate */
    printf("Iterate:\n");
    for (sl_node* n = sl_first(sl); n; n = n->next[0]) {
        printf("%d ", n->key);
    }
    printf("\n");

    sl_free(sl, free_val);
    return 0;
}
