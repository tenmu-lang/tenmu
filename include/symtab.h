/* symtab.h — スコープ・シンボルテーブル */
#ifndef TMC_SYMTAB_H
#define TMC_SYMTAB_H

#include "ast.h"
#include "semtype.h"

typedef enum {
    SYM_FN, SYM_STRUCT, SYM_ENUM, SYM_ERROR, SYM_UNION, SYM_TRAIT,
    SYM_IMPORT, SYM_LOCAL, SYM_PARAM, SYM_GENERIC_PARAM, SYM_CONST,
} SymbolKind;

typedef struct Symbol {
    SymbolKind kind;
    const char *name; size_t name_len;
    Item *item;      /* SYM_FN/STRUCT/ENUM/ERROR/UNION/TRAIT の宣言ノード。それ以外はNULL可 */
    SemType *type;    /* SYM_LOCAL/PARAM: 変数の型。SYM_FN: 未使用(item経由でシグネチャを見る) */
    int is_mut;
    int line, col;
} Symbol;

typedef struct Scope {
    struct Scope *parent;
    Symbol **symbols; int count; int cap;
} Scope;

Scope   *scope_new(Scope *parent);
Symbol  *scope_declare(Scope *sc, SymbolKind kind, const char *name, size_t name_len, int line, int col);
/* 現在スコープから親へ遡って探す(通常の名前解決用) */
Symbol  *scope_lookup(Scope *sc, const char *name, size_t name_len);
/* 現在スコープのみを見る(同一スコープ内での再宣言検出用) */
Symbol  *scope_lookup_local(Scope *sc, const char *name, size_t name_len);

#endif /* TMC_SYMTAB_H */
