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

    /* 所有権/借用チェック用の状態(Stage 2後半)。SYM_LOCAL/SYM_PARAMのみ使う。 */
    int moved;               /* ムーブ済みで以後読み出し不可か */
    int shared_borrows;       /* このシンボルに対して現在有効な共有借用(&)の数 */
    int mutable_borrow;        /* このシンボルに対して現在有効な可変借用(&mut)があるか */
    struct Symbol *borrows_from; /* このシンボル自身が「let r = &x」等で作られた
                                     参照である場合、借用元xのSymbol。所属スコープの
                                     終了時にxの借用状態を解放するために使う。 */
    int borrow_is_mut;
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
