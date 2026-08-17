/* symtab.c */
#include "symtab.h"
#include "util.h"
#include <string.h>

Scope *scope_new(Scope *parent) {
    Scope *sc = xmalloc(sizeof(Scope));
    sc->parent = parent;
    sc->symbols = NULL;
    sc->count = 0;
    sc->cap = 0;
    return sc;
}

Symbol *scope_declare(Scope *sc, SymbolKind kind, const char *name, size_t name_len, int line, int col) {
    Symbol *s = xmalloc(sizeof(Symbol));
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->name = name;
    s->name_len = name_len;
    s->line = line;
    s->col = col;
    if (sc->count >= sc->cap) {
        int new_cap = sc->cap == 0 ? 8 : sc->cap * 2;
        sc->symbols = xrealloc(sc->symbols, (size_t)new_cap * sizeof(Symbol *));
        sc->cap = new_cap;
    }
    sc->symbols[sc->count++] = s;
    return s;
}

Symbol *scope_lookup_local(Scope *sc, const char *name, size_t name_len) {
    for (int i = sc->count - 1; i >= 0; i--) {
        Symbol *s = sc->symbols[i];
        if (s->name_len == name_len && memcmp(s->name, name, name_len) == 0) return s;
    }
    return NULL;
}

Symbol *scope_lookup(Scope *sc, const char *name, size_t name_len) {
    for (Scope *cur = sc; cur; cur = cur->parent) {
        Symbol *s = scope_lookup_local(cur, name, name_len);
        if (s) return s;
    }
    return NULL;
}
