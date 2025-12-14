#include "connector.h"
#include "cskiplist.h"
#include <stdlib.h>

/* Adapter: implement connector API atop cskiplist. Maintains semantics used by set2 code. */

typedef struct conn_impl {
    cskiplist* sl;
    csl_iter it;       /* iterator state for forward reads */
    int cursor_global; /* global position (approx) for compatibility */
    int last_global;   /* total items - 1 */
} conn_impl;

/* Redefine connector struct to point to adapter internals */
struct connector {
    int length;  /* logical capacity (not used) */
    int last;    /* last occupied index (derived from skiplist size - 1) */
    int cursor;  /* index of last returned element */
    link* seq;   /* not used; kept for ABI; NULL */
    conn_impl* impl;
};

static link* make_link(int key, void* val) {
    link* l = (link*)malloc(sizeof(link));
    if (!l) return NULL;
    l->key = key; l->val = val; return l;
}

connector* con_alloc() {
    connector* c = (connector*)calloc(1, sizeof(connector));
    if (!c) return NULL;
    c->impl = (conn_impl*)calloc(1, sizeof(conn_impl));
    if (!c->impl) { free(c); return NULL; }
    c->impl->sl = csl_create();
    if (!c->impl->sl) { free(c->impl); free(c); return NULL; }
    c->length = 0; c->last = -1; c->cursor = -1; c->seq = NULL; c->impl->cursor_global = -1; c->impl->last_global = -1;
    return c;
}

boolean con_free(connector* sp) {
    if (!sp) return false;
    csl_free(sp->impl->sl, NULL);
    free(sp->impl);
    free(sp);
    return true;
}

boolean con_sort(connector* sp) { (void)sp; return true; }

int con_size(connector* sp) { return sp ? (int)sp->impl->sl->size : 0; }

void con_print_keys(connector* sp, FILE* f) {
    if (!sp) return; csl_iter it; if (!csl_iter_first(sp->impl->sl, &it)) return; do { csl_kv* kv = csl_iter_get(&it); fprintf(f, "%d\n", kv->key); } while (csl_iter_next(&it)); }

/* Lookup using search; updates cursor to index <= found key position */
link* con_lookup(connector* sp, int key) {
    if (!sp) return NULL; csl_iter it; int exact=0; if (!csl_iter_seek(sp->impl->sl, key, &it, &exact)) { sp->cursor = sp->impl->sl->size - 1; return NULL; }
    /* compute global index by walking from beginning if needed */
    int gidx = 0; csl_iter wit; if (csl_iter_first(sp->impl->sl, &wit)) { do { if (wit.b == it.b && wit.idx == it.idx) break; gidx++; } while (csl_iter_next(&wit)); }
    sp->cursor = gidx; sp->last = sp->impl->sl->size - 1; sp->impl->last_global = sp->last;
    if (!exact) return NULL;
    csl_kv* kv = csl_iter_get(&it); return kv ? make_link(kv->key, kv->val) : NULL;
}

boolean con_member(connector* sp, int key) { return con_lookup(sp, key) != NULL; }

boolean con_open(connector* sp) { if (!sp) return false; csl_iter_first(sp->impl->sl, &sp->impl->it); sp->cursor = -1; sp->last = sp->impl->sl->size - 1; sp->impl->last_global = sp->last; return true; }

boolean con_open_at(connector* sp, int key) { if (!sp) return false; int exact=0; csl_iter_seek(sp->impl->sl, key, &sp->impl->it, &exact); /* position just before desired key for con_read semantics */ sp->cursor = -1; return exact; }

link* con_peek(connector* sp) { if (!sp) return NULL; csl_iter it = sp->impl->it; /* copy */ csl_iter_next(&it); csl_kv* kv = csl_iter_get(&it); return kv ? make_link(kv->key, kv->val) : NULL; }

link* con_read(connector* sp) { if (!sp) return NULL; if (!csl_iter_next(&sp->impl->it)) return NULL; csl_kv* kv = csl_iter_get(&sp->impl->it); if (!kv) return NULL; sp->cursor++; return make_link(kv->key, kv->val); }

link* con_current(connector* sp) { if (!sp) return NULL; csl_kv* kv = csl_iter_get(&sp->impl->it); return kv ? make_link(kv->key, kv->val) : NULL; }

link* con_peek_prev(connector* sp) { if (!sp) return NULL; csl_iter it = sp->impl->it; if (!csl_iter_prev(sp->impl->sl, &it)) return NULL; csl_kv* kv = csl_iter_get(&it); return kv ? make_link(kv->key, kv->val) : NULL; }

link* con_read_prev(connector* sp) { if (!sp) return NULL; if (!csl_iter_prev(sp->impl->sl, &sp->impl->it)) return NULL; csl_kv* kv = csl_iter_get(&sp->impl->it); if (!kv) return NULL; if (sp->cursor > 0) sp->cursor--; return make_link(kv->key, kv->val); }

boolean con_eos(connector* sp) { if (!sp) return true; return sp->cursor >= (int)sp->impl->sl->size - 1; }

boolean con_write(connector* sp, int key, void* val) { if (!sp) return false; int r = csl_append(sp->impl->sl, key, val); if (r < 0) return false; sp->last = sp->impl->sl->size - 1; return true; }

boolean con_insert(connector* sp, int key, void* val) { if (!sp) return false; int r = csl_insert(sp->impl->sl, key, val); if (r < 0) return false; sp->last = sp->impl->sl->size - 1; return true; }

int con_get_cursor(connector* sp) { return sp ? sp->cursor : -1; }
void con_set_cursor(connector* sp, int cur) { if (sp) sp->cursor = cur; }

/* Export keys into the provided buffer (up to max entries). Returns number of keys written. */
int con_export_keys(connector* sp, int* out_buf, int max) {
    if (!sp || !out_buf || max <= 0) return 0;
    csl_iter it;
    int written = 0;
    if (!csl_iter_first(sp->impl->sl, &it)) return 0;
    do {
        csl_kv* kv = csl_iter_get(&it);
        if (!kv) break;
        out_buf[written++] = kv->key;
        if (written >= max) break;
    } while (csl_iter_next(&it));
    return written;
}
