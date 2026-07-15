#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "connector.h"
#include "cskiplist.h"

/* Adapter: implement connector API atop cskiplist. Maintains semantics used by set2 code.
   The conn_impl pointer is stored in the seq field (cast to link*) since the sorted-array
   storage is not used in this adapter. */

/*
 * Returned link* lifetime: the original connector returns pointers into its
 * internal array, so callers (set2.c) never free them and hold only a few at
 * a time.  We mirror that contract with a small ring of scratch links per
 * connector — a returned pointer stays valid for the next CON_SCRATCH-1
 * link-returning calls.  No allocation happens per read (the old adapter
 * malloc'd a link per call, leaking one per traversal step).
 */
#define CON_SCRATCH 8

typedef struct conn_impl {
    cskiplist* sl;
    csl_iter it;                /* iterator state for forward reads */
    link scratch[CON_SCRATCH];  /* ring of returned links */
    unsigned scratch_ix;
} conn_impl;

/* Access the impl pointer stored in the seq field */
#define IMPL(sp) ((conn_impl*)(sp)->seq)

static link* make_link(conn_impl* im, int key, void* val) {
    link* l = &im->scratch[im->scratch_ix++ % CON_SCRATCH];
    l->key = key; l->val = val; return l;
}

connector* con_alloc() {
    connector* c = (connector*)calloc(1, sizeof(connector));
    if (!c) return NULL;
    conn_impl* im = (conn_impl*)calloc(1, sizeof(conn_impl));
    if (!im) { free(c); return NULL; }
    im->sl = csl_create();
    if (!im->sl) { free(im); free(c); return NULL; }
    c->length = 0; c->last = -1; c->cursor = -1;
    c->seq = (link*)im; /* store impl in seq field */
    return c;
}

boolean con_free(connector* sp) {
    if (!sp) return false;
    csl_free(IMPL(sp)->sl, NULL);
    free(IMPL(sp));
    free(sp);
    return true;
}

boolean con_sort(connector* sp) { (void)sp; return true; }

int con_size(connector* sp) { return sp ? (int)IMPL(sp)->sl->size : 0; }

void con_print_keys(connector* sp, FILE* f) {
    if (!sp) return; csl_iter it; if (!csl_iter_first(IMPL(sp)->sl, &it)) return; do { csl_kv* kv = csl_iter_get(&it); fprintf(f, "%d\n", kv->key); } while (csl_iter_next(&it)); }

/* Lookup by key in O(log n). On an exact match the internal iterator is
 * positioned at the found pair, so con_read continues right after it
 * (mirrors the original's cursor semantics used by set2). */
link* con_lookup(connector* sp, int key) {
    if (!sp) return NULL;
    conn_impl* im = IMPL(sp);
    csl_iter it; int exact = 0;
    sp->last = (int)im->sl->size - 1;
    if (!csl_iter_seek(im->sl, key, &it, &exact) || !exact) return NULL;
    im->it = it;
    csl_kv* kv = csl_iter_get(&it);
    return kv ? make_link(im, kv->key, kv->val) : NULL;
}

boolean con_member(connector* sp, int key) { return con_lookup(sp, key) != NULL; }

boolean con_open(connector* sp) { if (!sp) return false; IMPL(sp)->it.b = IMPL(sp)->sl->head; IMPL(sp)->it.idx = 0; IMPL(sp)->it.eytzinger = IMPL(sp)->sl->eytzinger; sp->cursor = -1; sp->last = IMPL(sp)->sl->size - 1; return true; }

boolean con_open_at(connector* sp, int key) { if (!sp) return false; int exact=0; int found = csl_iter_seek(IMPL(sp)->sl, key, &IMPL(sp)->it, &exact); if (!found) { IMPL(sp)->it.b = NULL; IMPL(sp)->it.idx = -1; sp->cursor = -1; return 0; } if (!csl_iter_prev(IMPL(sp)->sl, &IMPL(sp)->it)) { IMPL(sp)->it.b = IMPL(sp)->sl->head; IMPL(sp)->it.idx = 0; } sp->cursor = -1; return exact; }

link* con_peek(connector* sp) { if (!sp) return NULL; csl_iter it = IMPL(sp)->it; /* copy */ csl_iter_next(&it); csl_kv* kv = csl_iter_get(&it); return kv ? make_link(IMPL(sp), kv->key, kv->val) : NULL; }

link* con_read(connector* sp) { if (!sp) return NULL; if (!csl_iter_next(&IMPL(sp)->it)) return NULL; csl_kv* kv = csl_iter_get(&IMPL(sp)->it); if (!kv) return NULL; sp->cursor++; return make_link(IMPL(sp), kv->key, kv->val); }

link* con_current(connector* sp) { if (!sp) return NULL; csl_kv* kv = csl_iter_get(&IMPL(sp)->it); return kv ? make_link(IMPL(sp), kv->key, kv->val) : NULL; }

link* con_peek_prev(connector* sp) { if (!sp) return NULL; csl_iter it = IMPL(sp)->it; if (!csl_iter_prev(IMPL(sp)->sl, &it)) return NULL; csl_kv* kv = csl_iter_get(&it); return kv ? make_link(IMPL(sp), kv->key, kv->val) : NULL; }

link* con_read_prev(connector* sp) { if (!sp) return NULL; if (!csl_iter_prev(IMPL(sp)->sl, &IMPL(sp)->it)) return NULL; csl_kv* kv = csl_iter_get(&IMPL(sp)->it); if (!kv) return NULL; if (sp->cursor > 0) sp->cursor--; return make_link(IMPL(sp), kv->key, kv->val); }

boolean con_eos(connector* sp) { if (!sp) return true; csl_iter tmp = IMPL(sp)->it; return !csl_iter_next(&tmp); }

boolean con_write(connector* sp, int key, void* val) { if (!sp) return false; int r = csl_append(IMPL(sp)->sl, key, val); if (r < 0) return false; sp->last = IMPL(sp)->sl->size - 1; return true; }

boolean con_insert(connector* sp, int key, void* val) { if (!sp) return false; int r = csl_insert(IMPL(sp)->sl, key, val); if (r < 0) return false; sp->last = IMPL(sp)->sl->size - 1; return true; }

int con_get_cursor(connector* sp) { return sp ? sp->cursor : -1; }
void con_set_cursor(connector* sp, int cur) { if (sp) sp->cursor = cur; }

/* Export keys into the provided buffer (up to max entries). Returns number of keys written. */
int con_export_keys(connector* sp, int* out_buf, int max) {
    if (!sp || !out_buf || max <= 0) return 0;
    csl_iter it;
    int written = 0;
    if (!csl_iter_first(IMPL(sp)->sl, &it)) return 0;
    do {
        csl_kv* kv = csl_iter_get(&it);
        if (!kv) break;
        out_buf[written++] = kv->key;
        if (written >= max) break;
    } while (csl_iter_next(&it));
    return written;
}
