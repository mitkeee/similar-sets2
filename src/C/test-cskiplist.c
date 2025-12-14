#include "cskiplist.h"
#include <stdio.h>
#include <stdint.h>

int main(void) {
    cskiplist* sl = csl_create();
    if (!sl) { fprintf(stderr, "OOM\n"); return 1; }

    /* append ascending keys */
    for (int i = 0; i < 1000; ++i) {
        if (csl_append(sl, i, (void*)(intptr_t)(i+1)) < 0) { fprintf(stderr, "append OOM\n"); return 2; }
    }

    csl_rebuild_skips(sl);

    /* probe a few keys */
    for (int i = 0; i < 1000; i += 111) {
        void* v = csl_search(sl, i);
        if (!v) { fprintf(stderr, "missing %d\n", i); return 3; }
        printf("key=%d val=%ld\n", i, (long)(intptr_t)v);
    }

    /* edge keys */
    printf("key=0 => %ld\n", (long)(intptr_t)csl_search(sl, 0));
    printf("key=999 => %ld\n", (long)(intptr_t)csl_search(sl, 999));
    printf("key=1001 => %p\n", csl_search(sl, 1001));

    csl_free(sl, NULL);
    return 0;
}
