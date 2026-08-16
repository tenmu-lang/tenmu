/* util.c */
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) { fprintf(stderr, "tmc0: out of memory (malloc %zu bytes)\n", size); exit(1); }
    return p;
}

void *xrealloc(void *p, size_t size) {
    void *q = realloc(p, size ? size : 1);
    if (!q) { fprintf(stderr, "tmc0: out of memory (realloc %zu bytes)\n", size); exit(1); }
    return q;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void dynarray_init(DynArray *a, size_t elem_size) {
    a->data = NULL;
    a->elem_size = elem_size;
    a->count = 0;
    a->cap = 0;
}

void dynarray_push(DynArray *a, const void *elem) {
    if (a->count >= a->cap) {
        int new_cap = a->cap == 0 ? 8 : a->cap * 2;
        a->data = xrealloc(a->data, (size_t)new_cap * a->elem_size);
        a->cap = new_cap;
    }
    memcpy((char *)a->data + (size_t)a->count * a->elem_size, elem, a->elem_size);
    a->count++;
}

void *dynarray_take(DynArray *a, int *out_count) {
    if (out_count) *out_count = a->count;
    void *data = a->data;
    a->data = NULL;
    a->count = 0;
    a->cap = 0;
    return data;
}
