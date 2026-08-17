/* checker.c — Stage 2意味解析の実装 */
#include "checker.h"
#include "comptime.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ===== エラー報告 ===== */
static void check_errorf(Checker *ck, int line, int col, const char *fmt, ...) {
    ck->total_error_count++;
    if (ck->error_count >= CHECKER_MAX_ERRORS) return;
    CheckerError *e = &ck->errors[ck->error_count++];
    e->line = line; e->col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
}

/* ===== "プレリュード"的に暗黙で使える名前(標準ライブラリ未実装のため暫定措置) =====
   Vec/String等は本来 std.collections 等からのimportを要するはずだが、
   実際のサンプルコードでは修飾なしで使われているため、実体不明の外部型として
   信頼する。標準ライブラリ実装(Stage 4)時に正式なpreludeとして仕様化する。 */
static const char *PRELUDE_TYPES[] = {
    "Vec", "String", "HashMap", "HashSet", "Deque", "BTreeMap",
    "Path", "Allocator", "Result", "Option", NULL
};

static int name_eq_cstr(const char *name, size_t len, const char *cstr) {
    size_t n = strlen(cstr);
    return len == n && memcmp(name, cstr, n) == 0;
}

static int is_prelude_type_name(const char *name, size_t len) {
    for (int i = 0; PRELUDE_TYPES[i]; i++) if (name_eq_cstr(name, len, PRELUDE_TYPES[i])) return 1;
    return 0;
}

/* ===== プリミティブ型名 ===== */
typedef struct { const char *name; SemTypeKind kind; } PrimEntry;
static const PrimEntry PRIMS[] = {
    {"i8",ST_I8},{"i16",ST_I16},{"i32",ST_I32},{"i64",ST_I64},{"i128",ST_I128},{"isize",ST_ISIZE},
    {"u8",ST_U8},{"u16",ST_U16},{"u32",ST_U32},{"u64",ST_U64},{"u128",ST_U128},{"usize",ST_USIZE},
    {"f16",ST_F16},{"f32",ST_F32},{"f64",ST_F64},
    {"bool",ST_BOOL},{"char",ST_CHAR},{"void",ST_VOID},{"str",ST_STR},
    {NULL, ST_UNKNOWN},
};

static int lookup_primitive(const char *name, size_t len, SemTypeKind *out) {
    for (int i = 0; PRIMS[i].name; i++) {
        if (name_eq_cstr(name, len, PRIMS[i].name)) { *out = PRIMS[i].kind; return 1; }
    }
    return 0;
}

/* ===== 前方宣言 ===== */
static SemType *resolve_type(Checker *ck, Scope *sc, Type *t);
static SemType *check_expr(Checker *ck, Scope *sc, Expr *e);
static void      check_stmt(Checker *ck, Scope *sc, Stmt *s);
static void      check_item(Checker *ck, Scope *sc, Item *it);

/* ===== 型解決 ===== */
static SemType *resolve_path_type(Checker *ck, Scope *sc, Type *t) {
    if (t->segment_count == 1) {
        SemTypeKind prim;
        if (lookup_primitive(t->segments[0], strlen(t->segments[0]), &prim))
            return semtype_primitive(prim);
        if (is_prelude_type_name(t->segments[0], strlen(t->segments[0]))) {
            SemType *st = semtype_external();
            for (int i = 0; i < t->generic_arg_count; i++) resolve_type(ck, sc, t->generic_args[i]);
            return st;
        }
        Symbol *sym = scope_lookup(sc, t->segments[0], strlen(t->segments[0]));
        if (!sym) {
            check_errorf(ck, t->line, t->col, "undefined type '%s'", t->segments[0]);
            return semtype_unknown();
        }
        if (sym->kind == SYM_GENERIC_PARAM) {
            SemType *g = semtype_new(ST_GENERIC_PARAM);
            g->name = (char *)sym->name;
            return g;
        }
        if (sym->kind == SYM_STRUCT || sym->kind == SYM_ENUM || sym->kind == SYM_ERROR ||
            sym->kind == SYM_UNION || sym->kind == SYM_TRAIT) {
            SemTypeKind k = sym->kind == SYM_STRUCT ? ST_STRUCT : sym->kind == SYM_ENUM ? ST_ENUM :
                             sym->kind == SYM_ERROR ? ST_ERROR : sym->kind == SYM_UNION ? ST_UNION : ST_TRAIT;
            SemType *st = semtype_new(k);
            st->name = (char *)sym->name;
            st->decl = sym->item;
            for (int i = 0; i < t->generic_arg_count; i++) resolve_type(ck, sc, t->generic_args[i]);
            return st;
        }
        check_errorf(ck, t->line, t->col, "'%s' is not a type", t->segments[0]);
        return semtype_unknown();
    }
    /* 複数セグメント: 先頭がimportエイリアスとして解決できれば外部型として信頼する */
    Symbol *first = scope_lookup(sc, t->segments[0], strlen(t->segments[0]));
    if (!first || first->kind != SYM_IMPORT) {
        check_errorf(ck, t->line, t->col, "undefined name '%s' in type path", t->segments[0]);
        return semtype_unknown();
    }
    for (int i = 0; i < t->generic_arg_count; i++) resolve_type(ck, sc, t->generic_args[i]);
    return semtype_external();
}

static SemType *resolve_type(Checker *ck, Scope *sc, Type *t) {
    if (!t) return semtype_unknown();
    switch (t->kind) {
        case TY_PATH: return resolve_path_type(ck, sc, t);
        case TY_PTR: { SemType *r = semtype_new(ST_PTR); r->is_mut = t->is_mut; r->inner = resolve_type(ck, sc, t->inner); return r; }
        case TY_REF: { SemType *r = semtype_new(ST_REF); r->is_mut = t->is_mut; r->inner = resolve_type(ck, sc, t->inner); return r; }
        case TY_ARRAY: { SemType *r = semtype_new(t->array_size ? ST_ARRAY : ST_SLICE); r->inner = resolve_type(ck, sc, t->inner);
            if (t->array_size) check_expr(ck, sc, t->array_size);
            return r; }
        case TY_OPTIONAL: { SemType *r = semtype_new(ST_OPTIONAL); r->inner = resolve_type(ck, sc, t->inner); return r; }
        case TY_NEVER: return semtype_new(ST_NEVER);
        case TY_TUPLE: {
            SemType *r = semtype_new(ST_TUPLE);
            r->items = xmalloc(sizeof(SemType *) * (size_t)(t->item_count ? t->item_count : 1));
            for (int i = 0; i < t->item_count; i++) r->items[i] = resolve_type(ck, sc, t->items[i]);
            r->item_count = t->item_count;
            return r;
        }
        case TY_TENSOR: {
            SemType *r = semtype_new(ST_TENSOR);
            r->inner = resolve_type(ck, sc, t->inner);
            for (int i = 0; i < t->dim_count; i++) if (t->dims[i]) check_expr(ck, sc, t->dims[i]);
            return r;
        }
        case TY_FN: {
            SemType *r = semtype_new(ST_FN);
            r->items = xmalloc(sizeof(SemType *) * (size_t)(t->item_count ? t->item_count : 1));
            for (int i = 0; i < t->item_count; i++) r->items[i] = resolve_type(ck, sc, t->items[i]);
            r->item_count = t->item_count;
            r->ret = t->ret ? resolve_type(ck, sc, t->ret) : semtype_primitive(ST_VOID);
            return r;
        }
        default: return semtype_unknown();
    }
}

/* ===== 式の検査 ===== */

static int is_bool_incompatible(SemType *t) {
    if (!t) return 0;
    if (t->kind == ST_UNKNOWN || t->kind == ST_EXTERNAL || t->kind == ST_NEVER) return 0;
    return t->kind != ST_BOOL;
}

static SemType *check_block(Checker *ck, Scope *parent, Expr *block) {
    Scope *sc = scope_new(parent);
    SemType *last = semtype_primitive(ST_VOID);
    for (int i = 0; i < block->stmt_count; i++) {
        check_stmt(ck, sc, block->stmts[i]);
        if (i == block->stmt_count - 1 && block->stmts[i]->kind == ST_EXPR) {
            last = check_expr(ck, sc, block->stmts[i]->expr);
        }
    }
    return last;
}

static SemType *check_call_args_generic(Checker *ck, Scope *sc, Expr **args, int count) {
    for (int i = 0; i < count; i++) check_expr(ck, sc, args[i]);
    return semtype_unknown();
}

static SemType *fn_signature_type(Checker *ck, Scope *sc, Item *fn_item) {
    SemType *r = semtype_new(ST_FN);
    r->items = xmalloc(sizeof(SemType *) * (size_t)(fn_item->param_count ? fn_item->param_count : 1));
    for (int i = 0; i < fn_item->param_count; i++) {
        Param *p = &fn_item->params[i];
        r->items[i] = p->type ? resolve_type(ck, sc, p->type) : semtype_unknown();
    }
    r->item_count = fn_item->param_count;
    r->ret = fn_item->return_type ? resolve_type(ck, sc, fn_item->return_type) : semtype_primitive(ST_VOID);
    return r;
}

static SemType *check_expr(Checker *ck, Scope *sc, Expr *e) {
    if (!e) return semtype_unknown();
    switch (e->kind) {
        case EX_INT: return semtype_primitive(ST_I32);
        case EX_FLOAT: return semtype_primitive(ST_F64);
        case EX_STRING: return semtype_primitive(ST_STR);
        case EX_CHAR: return semtype_primitive(ST_CHAR);
        case EX_TRUE: case EX_FALSE: return semtype_primitive(ST_BOOL);
        case EX_NULL: { SemType *o = semtype_new(ST_OPTIONAL); o->inner = semtype_unknown(); return o; }
        case EX_VOID: return semtype_primitive(ST_VOID);

        case EX_STRING_INTERP:
            for (int i = 0; i < e->interp_part_count; i++)
                if (e->interp_parts[i].expr) check_expr(ck, sc, e->interp_parts[i].expr);
            return semtype_primitive(ST_STR);

        case EX_SELF: case EX_IDENT: {
            const char *nm = e->kind == EX_SELF ? "self" : e->str;
            size_t nl = e->kind == EX_SELF ? 4 : e->str_len;
            if (e->kind == EX_IDENT && (name_eq_cstr(nm, nl, "Ok") || name_eq_cstr(nm, nl, "Err") || name_eq_cstr(nm, nl, "Some")))
                return semtype_external(); /* Result/OptionのコンストラクタとしてBuild-in扱い */
            Symbol *sym = scope_lookup(sc, nm, nl);
            if (!sym) {
                check_errorf(ck, e->line, e->col, "undefined name '%.*s'", (int)nl, nm);
                return semtype_unknown();
            }
            switch (sym->kind) {
                case SYM_LOCAL: case SYM_PARAM: return sym->type ? sym->type : semtype_unknown();
                case SYM_GENERIC_PARAM: { SemType *g = semtype_new(ST_GENERIC_PARAM); g->name = (char *)sym->name; return g; }
                case SYM_IMPORT: return semtype_external();
                case SYM_FN: return fn_signature_type(ck, sc, sym->item);
                case SYM_STRUCT: { SemType *st = semtype_new(ST_STRUCT); st->name = (char *)sym->name; st->decl = sym->item; return st; }
                case SYM_ENUM: { SemType *st = semtype_new(ST_ENUM); st->name = (char *)sym->name; st->decl = sym->item; return st; }
                case SYM_ERROR: { SemType *st = semtype_new(ST_ERROR); st->name = (char *)sym->name; st->decl = sym->item; return st; }
                case SYM_CONST: return sym->item->const_type ? resolve_type(ck, sc, sym->item->const_type) : semtype_unknown();
                default: return semtype_unknown();
            }
        }

        case EX_BINARY: {
            SemType *a = check_expr(ck, sc, e->a);
            SemType *b = check_expr(ck, sc, e->b);
            int is_cmp = name_eq_cstr(e->str, e->str_len, "==") || name_eq_cstr(e->str, e->str_len, "!=") ||
                         name_eq_cstr(e->str, e->str_len, "<") || name_eq_cstr(e->str, e->str_len, ">") ||
                         name_eq_cstr(e->str, e->str_len, "<=") || name_eq_cstr(e->str, e->str_len, ">=") ||
                         name_eq_cstr(e->str, e->str_len, "&&") || name_eq_cstr(e->str, e->str_len, "||");
            if (!semtype_compatible(a, b)) {
                char ba[64], bb[64];
                semtype_format(a, ba, sizeof(ba)); semtype_format(b, bb, sizeof(bb));
                check_errorf(ck, e->line, e->col, "type mismatch in '%.*s': %s vs %s", (int)e->str_len, e->str, ba, bb);
            }
            return is_cmp ? semtype_primitive(ST_BOOL) : a;
        }

        case EX_UNARY: {
            SemType *a = check_expr(ck, sc, e->a);
            if (e->str[0] == '*') {
                /* unsafeが必要なのは生ポインタ(*T)の外し(deref)のみ。
                   &T/&mut T という安全な参照の外しはRust同様*構文を使うが
                   借用チェッカーが保証を与えるためunsafe不要。型が
                   UNKNOWN/EXTERNAL(判断できない)場合は誤検出を避け要求しない。 */
                if (a && a->kind == ST_PTR && !ck->in_unsafe) {
                    check_errorf(ck, e->line, e->col, "dereferencing a raw pointer requires an 'unsafe' block");
                }
                if (a && (a->kind == ST_PTR || a->kind == ST_REF)) return a->inner;
                return semtype_unknown();
            }
            if (e->str[0] == '&') {
                SemType *r = semtype_new(ST_REF);
                r->is_mut = strcmp(e->str, "&mut") == 0;
                r->inner = a;
                return r;
            }
            return a;
        }

        case EX_ASSIGN: {
            SemType *lhs = check_expr(ck, sc, e->a);
            SemType *rhs = check_expr(ck, sc, e->b);
            if (!semtype_compatible(lhs, rhs)) {
                char bl[64], br[64];
                semtype_format(lhs, bl, sizeof(bl)); semtype_format(rhs, br, sizeof(br));
                check_errorf(ck, e->line, e->col, "cannot assign %s to %s", br, bl);
            }
            return semtype_primitive(ST_VOID);
        }

        case EX_CAST:
            check_expr(ck, sc, e->a);
            return resolve_type(ck, sc, e->type);

        case EX_CALL: {
            /* 呼び出し対象がローカルに解決できる関数名なら引数個数/型を検査する */
            if (e->a->kind == EX_IDENT) {
                Symbol *sym = scope_lookup(sc, e->a->str, e->a->str_len);
                if (sym && sym->kind == SYM_FN) {
                    Item *fn = sym->item;
                    int declared = fn->param_count;
                    int given = e->list_count;
                    if (declared != given) {
                        check_errorf(ck, e->line, e->col,
                            "'%.*s' expects %d argument%s, got %d",
                            (int)e->a->str_len, e->a->str, declared, declared == 1 ? "" : "s", given);
                    }
                    int n = declared < given ? declared : given;
                    for (int i = 0; i < n; i++) {
                        SemType *argt = check_expr(ck, sc, e->list[i]);
                        SemType *want = fn->params[i].type ? resolve_type(ck, sc, fn->params[i].type) : semtype_unknown();
                        if (!semtype_compatible(want, argt)) {
                            char bw[64], bg[64];
                            semtype_format(want, bw, sizeof(bw)); semtype_format(argt, bg, sizeof(bg));
                            check_errorf(ck, e->list[i]->line, e->list[i]->col,
                                "argument %d of '%.*s': expected %s, got %s",
                                i + 1, (int)e->a->str_len, e->a->str, bw, bg);
                        }
                    }
                    for (int i = n; i < given; i++) check_expr(ck, sc, e->list[i]);
                    return fn->return_type ? resolve_type(ck, sc, fn->return_type) : semtype_primitive(ST_VOID);
                }
            }
            check_expr(ck, sc, e->a);
            return check_call_args_generic(ck, sc, e->list, e->list_count);
        }

        case EX_INDEX: {
            SemType *base = check_expr(ck, sc, e->a);
            check_expr(ck, sc, e->b);
            if (base && (base->kind == ST_ARRAY || base->kind == ST_SLICE)) return base->inner;
            return semtype_unknown();
        }

        case EX_FIELD: {
            SemType *base = check_expr(ck, sc, e->a);
            if (e->list) { for (int i = 0; i < e->list_count; i++) check_expr(ck, sc, e->list[i]); }
            if (!base || base->kind == ST_EXTERNAL || base->kind == ST_UNKNOWN) return semtype_unknown();
            if (base->kind == ST_STRUCT && base->decl && !e->list) {
                Item *sd = base->decl;
                for (int i = 0; i < sd->field_count; i++) {
                    if (name_eq_cstr(e->str, e->str_len, sd->fields[i].name))
                        return resolve_type(ck, sc, sd->fields[i].type);
                }
                check_errorf(ck, e->line, e->col, "struct '%s' has no field '%.*s'", sd->name, (int)e->str_len, e->str);
                return semtype_unknown();
            }
            return semtype_unknown();
        }

        case EX_TRY_POSTFIX:
            check_expr(ck, sc, e->a);
            return semtype_unknown();

        case EX_PAREN: return check_expr(ck, sc, e->a);

        case EX_BLOCK: return check_block(ck, sc, e);

        case EX_IF: {
            SemType *cond = check_expr(ck, sc, e->a);
            if (is_bool_incompatible(cond)) {
                char bc[64]; semtype_format(cond, bc, sizeof(bc));
                check_errorf(ck, e->a->line, e->a->col, "'if' condition must be bool, got %s", bc);
            }
            SemType *tb = check_expr(ck, sc, e->b);
            if (e->c) {
                SemType *eb = check_expr(ck, sc, e->c);
                if (semtype_compatible(tb, eb)) return tb;
                return semtype_unknown();
            }
            return semtype_primitive(ST_VOID);
        }

        case EX_FOR: {
            check_expr(ck, sc, e->a);
            Scope *body_sc = scope_new(sc);
            scope_declare(body_sc, SYM_LOCAL, e->str, e->str_len, e->line, e->col)->type = semtype_unknown();
            check_block(ck, body_sc, e->b);
            return semtype_primitive(ST_VOID);
        }
        case EX_WHILE: {
            SemType *cond = check_expr(ck, sc, e->a);
            if (is_bool_incompatible(cond)) {
                char bc[64]; semtype_format(cond, bc, sizeof(bc));
                check_errorf(ck, e->a->line, e->a->col, "'while' condition must be bool, got %s", bc);
            }
            check_expr(ck, sc, e->b);
            return semtype_primitive(ST_VOID);
        }
        case EX_LOOP: check_expr(ck, sc, e->a); return semtype_primitive(ST_NEVER);

        case EX_MATCH: {
            check_expr(ck, sc, e->a);
            for (int i = 0; i < e->arm_count; i++) {
                Scope *arm_sc = scope_new(sc);
                /* パターン中の単純識別子は束縛とみなしてスコープへ導入する
                   (Ok(s) の s、単なる e など)。Enum定数参照との区別はStage 2では簡略化。 */
                Expr *pat = e->arms[i].pattern;
                if (pat->kind == EX_IDENT) {
                    scope_declare(arm_sc, SYM_LOCAL, pat->str, pat->str_len, pat->line, pat->col)->type = semtype_unknown();
                } else if (pat->kind == EX_CALL) {
                    for (int j = 0; j < pat->list_count; j++) {
                        Expr *sub = pat->list[j];
                        if (sub->kind == EX_IDENT)
                            scope_declare(arm_sc, SYM_LOCAL, sub->str, sub->str_len, sub->line, sub->col)->type = semtype_unknown();
                    }
                }
                check_expr(ck, arm_sc, e->arms[i].body);
            }
            return semtype_unknown();
        }

        case EX_CLOSURE: {
            Scope *csc = scope_new(sc);
            for (int i = 0; i < e->param_count; i++) {
                SemType *pt = e->params[i].type ? resolve_type(ck, sc, e->params[i].type) : semtype_unknown();
                scope_declare(csc, SYM_PARAM, e->params[i].name, strlen(e->params[i].name), e->line, e->col)->type = pt;
            }
            check_expr(ck, csc, e->a);
            return semtype_unknown();
        }

        case EX_UNSAFE: {
            int saved = ck->in_unsafe;
            ck->in_unsafe = 1;
            SemType *r = check_expr(ck, sc, e->a);
            ck->in_unsafe = saved;
            return r;
        }

        case EX_TRY_CATCH: {
            check_expr(ck, sc, e->a);
            Scope *csc = scope_new(sc);
            scope_declare(csc, SYM_LOCAL, e->str, e->str_len, e->line, e->col)->type = semtype_unknown();
            check_expr(ck, csc, e->b);
            return semtype_unknown();
        }

        case EX_STRUCT_LITERAL: {
            Symbol *sym = scope_lookup(sc, e->str, e->str_len);
            if (!sym || sym->kind != SYM_STRUCT) {
                check_errorf(ck, e->line, e->col, "undefined struct '%.*s'", (int)e->str_len, e->str);
                for (int i = 0; i < e->field_init_count; i++) check_expr(ck, sc, e->field_inits[i].value);
                return semtype_unknown();
            }
            Item *sd = sym->item;
            for (int i = 0; i < e->field_init_count; i++) {
                SemType *vt = check_expr(ck, sc, e->field_inits[i].value);
                int found = 0;
                for (int j = 0; j < sd->field_count; j++) {
                    if (strcmp(e->field_inits[i].name, sd->fields[j].name) == 0) {
                        found = 1;
                        SemType *ft = resolve_type(ck, sc, sd->fields[j].type);
                        if (!semtype_compatible(ft, vt)) {
                            char bf[64], bv[64];
                            semtype_format(ft, bf, sizeof(bf)); semtype_format(vt, bv, sizeof(bv));
                            check_errorf(ck, e->line, e->col, "field '%s' of '%s': expected %s, got %s",
                                         e->field_inits[i].name, sd->name, bf, bv);
                        }
                        break;
                    }
                }
                if (!found) check_errorf(ck, e->line, e->col, "struct '%s' has no field '%s'", sd->name, e->field_inits[i].name);
            }
            SemType *st = semtype_new(ST_STRUCT); st->name = sd->name; st->decl = sd;
            return st;
        }

        case EX_ARRAY_LITERAL: {
            SemType *first = NULL;
            for (int i = 0; i < e->list_count; i++) {
                SemType *t = check_expr(ck, sc, e->list[i]);
                if (!first) first = t;
            }
            SemType *r = semtype_new(ST_ARRAY);
            r->inner = first ? first : semtype_unknown();
            return r;
        }

        case EX_TUPLE_LITERAL: {
            SemType *r = semtype_new(ST_TUPLE);
            r->items = xmalloc(sizeof(SemType *) * (size_t)(e->list_count ? e->list_count : 1));
            for (int i = 0; i < e->list_count; i++) r->items[i] = check_expr(ck, sc, e->list[i]);
            r->item_count = e->list_count;
            return r;
        }

        case EX_NAMED_ARG: return check_expr(ck, sc, e->b);

        default: return semtype_unknown();
    }
}

/* ===== 文の検査 ===== */
static void check_stmt(Checker *ck, Scope *sc, Stmt *s) {
    switch (s->kind) {
        case ST_LET: {
            SemType *init_t = check_expr(ck, sc, s->expr);
            if (s->names) {
                for (int i = 0; i < s->name_count; i++)
                    scope_declare(sc, SYM_LOCAL, s->names[i], strlen(s->names[i]), s->line, s->col)->type = semtype_unknown();
                return;
            }
            SemType *final_t = init_t;
            if (s->type_ann) {
                SemType *ann = resolve_type(ck, sc, s->type_ann);
                if (!semtype_compatible(ann, init_t)) {
                    char ba[64], bi[64];
                    semtype_format(ann, ba, sizeof(ba)); semtype_format(init_t, bi, sizeof(bi));
                    check_errorf(ck, s->line, s->col, "'%s': declared as %s but initialized with %s", s->name, ba, bi);
                }
                final_t = ann;
            }
            Symbol *sym = scope_declare(sc, SYM_LOCAL, s->name, strlen(s->name), s->line, s->col);
            sym->type = final_t;
            sym->is_mut = s->is_mut;
            return;
        }
        case ST_EXPR: check_expr(ck, sc, s->expr); return;
        case ST_RETURN: {
            SemType *rt = s->expr ? check_expr(ck, sc, s->expr) : semtype_primitive(ST_VOID);
            if (ck->current_fn_return && !semtype_compatible(ck->current_fn_return, rt)) {
                char be[64], ba[64];
                semtype_format(ck->current_fn_return, be, sizeof(be)); semtype_format(rt, ba, sizeof(ba));
                check_errorf(ck, s->line, s->col, "return type mismatch: function returns %s, got %s", be, ba);
            }
            return;
        }
        case ST_BREAK: if (s->expr) check_expr(ck, sc, s->expr); return;
        case ST_CONTINUE: return;
        case ST_DEFER: check_expr(ck, sc, s->expr); return;
        case ST_ITEM: check_item(ck, sc, s->item); return;
    }
}

/* ===== アイテムの検査 ===== */
static void check_fn(Checker *ck, Scope *outer, Item *fn) {
    Scope *sc = scope_new(outer);
    for (int i = 0; i < fn->generic_count; i++) {
        GenericParam *g = &fn->generics[i];
        if (g->is_const) {
            scope_declare(sc, SYM_LOCAL, g->name, strlen(g->name), fn->line, fn->col)->type =
                g->const_type ? resolve_type(ck, sc, g->const_type) : semtype_primitive(ST_USIZE);
        } else {
            scope_declare(sc, SYM_GENERIC_PARAM, g->name, strlen(g->name), fn->line, fn->col);
            for (int b = 0; b < g->bound_count; b++) resolve_type(ck, sc, g->bounds[b]);
        }
    }
    for (int i = 0; i < fn->param_count; i++) {
        Param *p = &fn->params[i];
        SemType *pt = p->type ? resolve_type(ck, sc, p->type) : semtype_external(); /* self 等 */
        scope_declare(sc, SYM_PARAM, p->name, strlen(p->name), fn->line, fn->col)->type = pt;
        if (p->default_value) check_expr(ck, sc, p->default_value);
    }
    SemType *saved_ret = ck->current_fn_return;
    ck->current_fn_return = fn->return_type ? resolve_type(ck, sc, fn->return_type) : semtype_primitive(ST_VOID);
    if (fn->body) check_expr(ck, sc, fn->body);
    ck->current_fn_return = saved_ret;
}

static void check_item(Checker *ck, Scope *sc, Item *it) {
    switch (it->kind) {
        case IT_FN: check_fn(ck, sc, it); return;
        case IT_STRUCT: case IT_UNION:
            for (int i = 0; i < it->field_count; i++) resolve_type(ck, sc, it->fields[i].type);
            return;
        case IT_ENUM: case IT_ERROR:
            for (int i = 0; i < it->variant_count; i++) {
                for (int j = 0; j < it->variants[i].tuple_type_count; j++) resolve_type(ck, sc, it->variants[i].tuple_types[j]);
                for (int j = 0; j < it->variants[i].struct_field_count; j++) resolve_type(ck, sc, it->variants[i].struct_fields[j].type);
            }
            return;
        case IT_TRAIT:
            for (int i = 0; i < it->method_count; i++) check_fn(ck, sc, it->methods[i]);
            return;
        case IT_CONST: {
            SemType *declared = resolve_type(ck, sc, it->const_type);
            SemType *actual = check_expr(ck, sc, it->const_value);
            if (!semtype_compatible(declared, actual)) {
                char bd[64], ba[64];
                semtype_format(declared, bd, sizeof(bd)); semtype_format(actual, ba, sizeof(ba));
                check_errorf(ck, it->line, it->col, "const '%s': declared as %s but initialized with %s", it->name, bd, ba);
            }
            ComptimeEvalCtx cctx;
            comptime_init(&cctx, ck->global);
            ComptimeValue v = comptime_eval_expr(&cctx, it->const_value);
            if (v.kind == CV_ERROR) {
                check_errorf(ck, it->line, it->col, "const '%s' is not a valid compile-time constant: %s", it->name, v.error_msg);
            }
            return;
        }
        case IT_IMPL: {
            Scope *impl_sc = scope_new(sc);
            for (int i = 0; i < it->generic_count; i++)
                scope_declare(impl_sc, SYM_GENERIC_PARAM, it->generics[i].name, strlen(it->generics[i].name), it->line, it->col);
            if (it->impl_type) resolve_type(ck, impl_sc, it->impl_type);
            if (it->impl_trait_type) resolve_type(ck, impl_sc, it->impl_trait_type);
            for (int i = 0; i < it->method_count; i++) check_fn(ck, impl_sc, it->methods[i]);
            return;
        }
        case IT_MODULE: case IT_IMPORT: return;
        default: return;
    }
}

/* ===== エントリポイント ===== */
void checker_init(Checker *ck) {
    memset(ck, 0, sizeof(*ck));
    ck->global = scope_new(NULL);
}

static void declare_globals(Checker *ck, Program *prog) {
    for (int i = 0; i < prog->item_count; i++) {
        Item *it = prog->items[i];
        const char *name = NULL; size_t name_len = 0;
        SymbolKind kind;
        switch (it->kind) {
            case IT_FN: kind = SYM_FN; name = it->name; name_len = strlen(it->name); break;
            case IT_STRUCT: kind = SYM_STRUCT; name = it->name; name_len = strlen(it->name); break;
            case IT_ENUM: kind = SYM_ENUM; name = it->name; name_len = strlen(it->name); break;
            case IT_ERROR: kind = SYM_ERROR; name = it->name; name_len = strlen(it->name); break;
            case IT_UNION: kind = SYM_UNION; name = it->name; name_len = strlen(it->name); break;
            case IT_TRAIT: kind = SYM_TRAIT; name = it->name; name_len = strlen(it->name); break;
            case IT_CONST: kind = SYM_CONST; name = it->name; name_len = strlen(it->name); break;
            case IT_IMPORT: {
                kind = SYM_IMPORT;
                if (it->alias) { name = it->alias; name_len = strlen(it->alias); }
                else if (it->path_segment_count > 0) {
                    const char *last = it->path_segments[it->path_segment_count - 1];
                    name = last; name_len = strlen(last);
                }
                break;
            }
            case IT_MODULE: case IT_IMPL: continue;
            default: continue;
        }
        if (!name) continue;
        Symbol *existing = scope_lookup_local(ck->global, name, name_len);
        if (existing) {
            check_errorf(ck, it->line, it->col, "redeclaration of '%.*s' (first declared at line %d)",
                         (int)name_len, name, existing->line);
            continue;
        }
        Symbol *sym = scope_declare(ck->global, kind, name, name_len, it->line, it->col);
        sym->item = it;
    }
}

void check_program(Checker *ck, Program *prog) {
    declare_globals(ck, prog);
    for (int i = 0; i < prog->item_count; i++) check_item(ck, ck->global, prog->items[i]);
}
