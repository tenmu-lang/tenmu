/* parser.c — Tenmu (tmc0) 再帰下降パーサー実装
   tenmu-spec.md §13 のEBNFに準拠。for/while/loop式・名前付き引数は
   元のEBNFに漏れていたため、この実装で仕様として確定させる。
   extern module { ... } ブロック(C拡張の宣言的ロード構文)と型エイリアス(type X = Y)は
   まだ正式なEBNFに落とし込まれていないため本パーサーの対象外(既知の未実装)。 */
#include "parser.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

/* ===== 基本ヘルパ ===== */

static int check(Parser *p, TokenKind k) { return p->cur.kind == k; }

static Token peek_next(Parser *p) {
    if (!p->has_peek) { p->peek_tok = lexer_next(&p->lexer); p->has_peek = 1; }
    return p->peek_tok;
}
static int peek_next_is(Parser *p, TokenKind k) { return peek_next(p).kind == k; }

static Token advance_tok(Parser *p) {
    Token old = p->cur;
    if (p->has_peek) { p->cur = p->peek_tok; p->has_peek = 0; }
    else { p->cur = lexer_next(&p->lexer); }
    return old;
}

static int match(Parser *p, TokenKind k) { if (check(p, k)) { advance_tok(p); return 1; } return 0; }
static int at_terminator(Parser *p) { return check(p, TOK_SEMI) || check(p, TOK_NEWLINE); }
static void skip_terminators(Parser *p) { while (at_terminator(p)) advance_tok(p); }

static void parse_error(Parser *p, const char *msg) {
    p->error_count++;
    if (!p->had_error) {
        p->had_error = 1;
        p->error_line = p->cur.line;
        p->error_col = p->cur.col;
        snprintf(p->error_msg, sizeof(p->error_msg), "%s (got '%.*s')", msg, (int)p->cur.len, p->cur.start);
    }
}

static Token expect(Parser *p, TokenKind k, const char *what) {
    if (check(p, k)) return advance_tok(p);
    char buf[160];
    snprintf(buf, sizeof(buf), "expected %s", what);
    parse_error(p, buf);
    return p->cur; /* エラー回復: 消費せず現トークンをそのまま返す */
}

static char *token_dup(Token t) { return xstrndup(t.start, t.len); }
static int token_text_eq(Token t, const char *s) {
    size_t n = strlen(s);
    return t.len == n && memcmp(t.start, s, n) == 0;
}

static Expr *new_expr_at(int line, int col, ExprKind kind) {
    Expr *e = xmalloc(sizeof(Expr));
    memset(e, 0, sizeof(*e));
    e->kind = kind; e->line = line; e->col = col;
    return e;
}
static Stmt *new_stmt_at(int line, int col, StmtKind kind) {
    Stmt *s = xmalloc(sizeof(Stmt));
    memset(s, 0, sizeof(*s));
    s->kind = kind; s->line = line; s->col = col;
    return s;
}
static Item *new_item(void) {
    Item *it = xmalloc(sizeof(Item));
    memset(it, 0, sizeof(*it));
    return it;
}

/* ===== 前方宣言 ===== */
static Type *parse_type(Parser *p);
static Expr *parse_expr(Parser *p);
static Expr *parse_block(Parser *p);
static Expr *parse_pattern(Parser *p);
static Stmt *parse_stmt(Parser *p);
static Item *parse_item(Parser *p);
static void  parse_fn_body(Parser *p, Item *item);

/* ===== 型 ===== */

static Type *new_type(int line, int col, TypeKind kind) {
    Type *t = xmalloc(sizeof(Type));
    memset(t, 0, sizeof(*t));
    t->kind = kind; t->line = line; t->col = col;
    return t;
}

static Type *parse_path_type(Parser *p) {
    Token first = p->cur;
    /* void はキーワードだが§3.1のプリミティブ型でもあるため、型位置では識別子として許可する */
    if (!check(p, TOK_IDENT) && !check(p, TOK_SELF_TYPE) && !check(p, TOK_VOID)) {
        expect(p, TOK_IDENT, "type name");
    }
    advance_tok(p);
    Type *t = new_type(first.line, first.col, TY_PATH);
    DynArray segs; dynarray_init(&segs, sizeof(char *));
    char *s0 = token_dup(first);
    dynarray_push(&segs, &s0);
    while (check(p, TOK_DOT)) {
        advance_tok(p);
        Token id = expect(p, TOK_IDENT, "path segment after '.'");
        char *s = token_dup(id);
        dynarray_push(&segs, &s);
    }
    t->segments = dynarray_take(&segs, &t->segment_count);
    if (check(p, TOK_LT)) {
        advance_tok(p);
        DynArray args; dynarray_init(&args, sizeof(Type *));
        if (!check(p, TOK_GT)) {
            for (;;) {
                Type *arg = parse_type(p);
                dynarray_push(&args, &arg);
                if (!match(p, TOK_COMMA)) break;
            }
        }
        expect(p, TOK_GT, "'>' to close generic arguments");
        t->generic_args = dynarray_take(&args, &t->generic_arg_count);
    }
    return t;
}

static Type *parse_tensor_type(Parser *p) {
    Token start = p->cur;
    advance_tok(p); /* 'Tensor' */
    expect(p, TOK_LT, "'<' after Tensor");
    Type *elem = parse_type(p);
    expect(p, TOK_COMMA, "',' in Tensor<T, [dims]>");
    expect(p, TOK_LBRACKET, "'[' for Tensor dimensions");
    DynArray dims; dynarray_init(&dims, sizeof(Expr *));
    if (!check(p, TOK_RBRACKET)) {
        for (;;) {
            Expr *d = NULL; /* NULL = '?' による動的次元 */
            if (check(p, TOK_QUESTION)) advance_tok(p);
            else d = parse_expr(p);
            dynarray_push(&dims, &d);
            if (!match(p, TOK_COMMA)) break;
        }
    }
    expect(p, TOK_RBRACKET, "']' to close Tensor dimensions");
    expect(p, TOK_GT, "'>' to close Tensor<...>");
    Type *t = new_type(start.line, start.col, TY_TENSOR);
    t->inner = elem;
    t->dims = dynarray_take(&dims, &t->dim_count);
    return t;
}

static Type *parse_type(Parser *p) {
    Token t = p->cur;
    if (check(p, TOK_STAR)) {
        advance_tok(p);
        int is_mut = match(p, TOK_MUT);
        Type *ty = new_type(t.line, t.col, TY_PTR);
        ty->is_mut = is_mut;
        ty->inner = parse_type(p);
        return ty;
    }
    if (check(p, TOK_AMP)) {
        advance_tok(p);
        int is_mut = match(p, TOK_MUT);
        Type *ty = new_type(t.line, t.col, TY_REF);
        ty->is_mut = is_mut;
        ty->inner = parse_type(p);
        return ty;
    }
    if (check(p, TOK_LBRACKET)) {
        advance_tok(p);
        Expr *size = NULL;
        if (!check(p, TOK_RBRACKET)) size = parse_expr(p);
        expect(p, TOK_RBRACKET, "']'");
        Type *ty = new_type(t.line, t.col, TY_ARRAY);
        ty->array_size = size;
        ty->inner = parse_type(p);
        return ty;
    }
    if (check(p, TOK_QUESTION)) {
        advance_tok(p);
        Type *ty = new_type(t.line, t.col, TY_OPTIONAL);
        ty->inner = parse_type(p);
        return ty;
    }
    if (check(p, TOK_BANG)) {
        advance_tok(p);
        return new_type(t.line, t.col, TY_NEVER);
    }
    if (check(p, TOK_FN)) {
        advance_tok(p);
        expect(p, TOK_LPAREN, "'('");
        DynArray params; dynarray_init(&params, sizeof(Type *));
        if (!check(p, TOK_RPAREN)) {
            for (;;) {
                Type *pt = parse_type(p);
                dynarray_push(&params, &pt);
                if (!match(p, TOK_COMMA)) break;
            }
        }
        expect(p, TOK_RPAREN, "')'");
        Type *ty = new_type(t.line, t.col, TY_FN);
        ty->items = dynarray_take(&params, &ty->item_count);
        if (match(p, TOK_ARROW)) ty->ret = parse_type(p);
        return ty;
    }
    if (check(p, TOK_LPAREN)) {
        advance_tok(p);
        if (check(p, TOK_RPAREN)) { advance_tok(p); return new_type(t.line, t.col, TY_TUPLE); }
        Type *first = parse_type(p);
        if (check(p, TOK_RPAREN)) { advance_tok(p); return first; }
        DynArray items; dynarray_init(&items, sizeof(Type *));
        dynarray_push(&items, &first);
        while (match(p, TOK_COMMA)) {
            if (check(p, TOK_RPAREN)) break;
            Type *ty2 = parse_type(p);
            dynarray_push(&items, &ty2);
        }
        expect(p, TOK_RPAREN, "')'");
        Type *ty = new_type(t.line, t.col, TY_TUPLE);
        ty->items = dynarray_take(&items, &ty->item_count);
        return ty;
    }
    if (check(p, TOK_IDENT) && token_text_eq(p->cur, "Tensor")) return parse_tensor_type(p);
    return parse_path_type(p);
}

/* ===== ジェネリクス・パラメータ ===== */

static void parse_generic_params(Parser *p, GenericParam **out, int *out_count) {
    *out = NULL; *out_count = 0;
    if (!match(p, TOK_LT)) return;
    DynArray gs; dynarray_init(&gs, sizeof(GenericParam));
    if (!check(p, TOK_GT)) {
        for (;;) {
            GenericParam g; memset(&g, 0, sizeof(g));
            if (match(p, TOK_CONST)) {
                g.is_const = 1;
                Token name = expect(p, TOK_IDENT, "const generic parameter name");
                g.name = token_dup(name);
                expect(p, TOK_COLON, "':' in const generic parameter");
                g.const_type = parse_type(p);
            } else {
                Token name = expect(p, TOK_IDENT, "generic parameter name");
                g.name = token_dup(name);
                if (match(p, TOK_COLON)) {
                    DynArray bounds; dynarray_init(&bounds, sizeof(Type *));
                    Type *b1 = parse_type(p);
                    dynarray_push(&bounds, &b1);
                    while (match(p, TOK_PLUS)) {
                        Type *b2 = parse_type(p);
                        dynarray_push(&bounds, &b2);
                    }
                    g.bounds = dynarray_take(&bounds, &g.bound_count);
                }
            }
            dynarray_push(&gs, &g);
            if (!match(p, TOK_COMMA)) break;
        }
    }
    expect(p, TOK_GT, "'>' to close generic parameters");
    *out = dynarray_take(&gs, out_count);
}

static void parse_params(Parser *p, Param **out, int *out_count) {
    expect(p, TOK_LPAREN, "'('");
    DynArray ps; dynarray_init(&ps, sizeof(Param));
    skip_terminators(p);
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        Param prm; memset(&prm, 0, sizeof(prm));
        if (check(p, TOK_SELF)) {
            Token s = advance_tok(p);
            prm.name = token_dup(s);
        } else if (check(p, TOK_AMP) && peek_next_is(p, TOK_MUT)) {
            advance_tok(p); advance_tok(p);
            Token s = expect(p, TOK_SELF, "'self' after '&mut'");
            prm.name = token_dup(s);
        } else if (check(p, TOK_AMP) && peek_next_is(p, TOK_SELF)) {
            advance_tok(p);
            Token s = expect(p, TOK_SELF, "'self' after '&'");
            prm.name = token_dup(s);
        } else {
            Token name = expect(p, TOK_IDENT, "parameter name");
            prm.name = token_dup(name);
            expect(p, TOK_COLON, "':' in parameter");
            prm.type = parse_type(p);
            if (match(p, TOK_EQ)) prm.default_value = parse_expr(p);
        }
        dynarray_push(&ps, &prm);
        skip_terminators(p);
        if (!match(p, TOK_COMMA)) break;
        skip_terminators(p);
    }
    skip_terminators(p);
    expect(p, TOK_RPAREN, "')'");
    *out = dynarray_take(&ps, out_count);
}

/* ===== 式: 優先順位ごとの再帰下降 ===== */

static Expr *parse_primary(Parser *p);

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        if (check(p, TOK_DOT)) {
            Token dotTok = p->cur;
            advance_tok(p);
            Token name = expect(p, TOK_IDENT, "field or method name after '.'");
            Expr *f = new_expr_at(dotTok.line, dotTok.col, EX_FIELD);
            f->a = e; f->str = name.start; f->str_len = name.len;
            if (check(p, TOK_LPAREN)) {
                advance_tok(p);
                DynArray args; dynarray_init(&args, sizeof(Expr *));
                int saved = p->no_struct_literal; p->no_struct_literal = 0;
                skip_terminators(p);
                if (!check(p, TOK_RPAREN)) {
                    for (;;) {
                        Expr *arg = parse_expr(p);
                        dynarray_push(&args, &arg);
                        skip_terminators(p);
                        if (!match(p, TOK_COMMA)) break;
                        skip_terminators(p);
                    }
                }
                p->no_struct_literal = saved;
                expect(p, TOK_RPAREN, "')'");
                f->list = dynarray_take(&args, &f->list_count);
            }
            e = f;
            continue;
        }
        if (check(p, TOK_LPAREN)) {
            Token pt = p->cur;
            advance_tok(p);
            Expr *call = new_expr_at(pt.line, pt.col, EX_CALL);
            call->a = e;
            DynArray args; dynarray_init(&args, sizeof(Expr *));
            int saved = p->no_struct_literal; p->no_struct_literal = 0;
            skip_terminators(p);
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    Expr *arg;
                    if (check(p, TOK_IDENT) && peek_next_is(p, TOK_COLON)) {
                        Token label = advance_tok(p);
                        advance_tok(p); /* ':' */
                        Expr *val = parse_expr(p);
                        arg = new_expr_at(label.line, label.col, EX_NAMED_ARG);
                        arg->str = label.start; arg->str_len = label.len;
                        arg->b = val;
                    } else {
                        arg = parse_expr(p);
                    }
                    dynarray_push(&args, &arg);
                    skip_terminators(p);
                    if (!match(p, TOK_COMMA)) break;
                    skip_terminators(p);
                }
            }
            p->no_struct_literal = saved;
            expect(p, TOK_RPAREN, "')'");
            call->list = dynarray_take(&args, &call->list_count);
            e = call;
            continue;
        }
        if (check(p, TOK_LBRACKET)) {
            Token bt = p->cur;
            advance_tok(p);
            int saved = p->no_struct_literal; p->no_struct_literal = 0;
            Expr *idx = parse_expr(p);
            p->no_struct_literal = saved;
            expect(p, TOK_RBRACKET, "']'");
            Expr *ie = new_expr_at(bt.line, bt.col, EX_INDEX);
            ie->a = e; ie->b = idx;
            e = ie;
            continue;
        }
        if (check(p, TOK_QUESTION)) {
            Token qt = p->cur;
            advance_tok(p);
            Expr *te = new_expr_at(qt.line, qt.col, EX_TRY_POSTFIX);
            te->a = e;
            e = te;
            continue;
        }
        break;
    }
    return e;
}

static Expr *parse_unary(Parser *p) {
    Token t = p->cur;
    if (check(p, TOK_MINUS) || check(p, TOK_BANG) || check(p, TOK_STAR)) {
        const char *op = check(p, TOK_MINUS) ? "-" : check(p, TOK_BANG) ? "!" : "*";
        advance_tok(p);
        Expr *e = new_expr_at(t.line, t.col, EX_UNARY);
        e->str = op; e->str_len = strlen(op);
        e->a = parse_unary(p);
        return e;
    }
    if (check(p, TOK_AMP)) {
        advance_tok(p);
        int is_mut = match(p, TOK_MUT);
        Expr *e = new_expr_at(t.line, t.col, EX_UNARY);
        e->str = is_mut ? "&mut" : "&"; e->str_len = strlen(e->str);
        e->a = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *parse_cast(Parser *p) {
    Expr *e = parse_unary(p);
    while (check(p, TOK_AS)) {
        advance_tok(p);
        Expr *c = new_expr_at(e->line, e->col, EX_CAST);
        c->a = e; c->type = parse_type(p);
        e = c;
    }
    return e;
}

#define DEFINE_BINARY_LEVEL(FNNAME, NEXT, ...)                               \
    static Expr *FNNAME(Parser *p) {                                        \
        Expr *lhs = NEXT(p);                                                \
        static const TokenKind ops[] = { __VA_ARGS__, TOK_ILLEGAL };        \
        for (;;) {                                                          \
            int matched = 0;                                                \
            for (int i = 0; ops[i] != TOK_ILLEGAL; i++) {                   \
                if (check(p, ops[i])) {                                     \
                    Token opTok = p->cur;                                   \
                    const char *opname = token_kind_name(opTok.kind);       \
                    advance_tok(p);                                         \
                    Expr *e = new_expr_at(opTok.line, opTok.col, EX_BINARY);\
                    e->str = opname; e->str_len = strlen(opname);           \
                    e->a = lhs; e->b = NEXT(p);                             \
                    lhs = e;                                                \
                    matched = 1;                                            \
                    break;                                                  \
                }                                                           \
            }                                                               \
            if (!matched) break;                                           \
        }                                                                   \
        return lhs;                                                        \
    }

DEFINE_BINARY_LEVEL(parse_mul, parse_cast, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_AT)
DEFINE_BINARY_LEVEL(parse_add, parse_mul, TOK_PLUS, TOK_MINUS)
DEFINE_BINARY_LEVEL(parse_shift, parse_add, TOK_SHL, TOK_SHR)
DEFINE_BINARY_LEVEL(parse_bitand, parse_shift, TOK_AMP)
DEFINE_BINARY_LEVEL(parse_bitxor, parse_bitand, TOK_CARET)
DEFINE_BINARY_LEVEL(parse_bitor, parse_bitxor, TOK_PIPE)

static Expr *parse_cmp(Parser *p) {
    Expr *lhs = parse_bitor(p);
    static const TokenKind ops[] = { TOK_EQEQ, TOK_NE, TOK_LE, TOK_GE, TOK_LT, TOK_GT, TOK_ILLEGAL };
    for (int i = 0; ops[i] != TOK_ILLEGAL; i++) {
        if (check(p, ops[i])) {
            Token opTok = p->cur;
            const char *opname = token_kind_name(opTok.kind);
            advance_tok(p);
            Expr *e = new_expr_at(opTok.line, opTok.col, EX_BINARY);
            e->str = opname; e->str_len = strlen(opname);
            e->a = lhs; e->b = parse_bitor(p);
            return e;
        }
    }
    return lhs;
}

DEFINE_BINARY_LEVEL(parse_and, parse_cmp, TOK_AMPAMP)
DEFINE_BINARY_LEVEL(parse_or, parse_and, TOK_PIPEPIPE)

static Expr *parse_assign(Parser *p) {
    Expr *lhs = parse_or(p);
    static const TokenKind ops[] = { TOK_EQ, TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ, TOK_ILLEGAL };
    for (int i = 0; ops[i] != TOK_ILLEGAL; i++) {
        if (check(p, ops[i])) {
            Token opTok = p->cur;
            const char *opname = token_kind_name(opTok.kind);
            advance_tok(p);
            Expr *e = new_expr_at(opTok.line, opTok.col, EX_ASSIGN);
            e->str = opname; e->str_len = strlen(opname);
            e->a = lhs; e->b = parse_assign(p); /* 右結合 */
            return e;
        }
    }
    return lhs;
}

static Expr *parse_expr(Parser *p) { return parse_assign(p); }

/* ===== 文字列補間 ===== */
static Expr *parse_string_interp(Parser *p) {
    Token t = p->cur;
    DynArray parts; dynarray_init(&parts, sizeof(InterpPart));
    InterpPart lit; lit.text = t.start; lit.text_len = t.len; lit.expr = NULL;
    dynarray_push(&parts, &lit);
    advance_tok(p);
    for (;;) {
        Expr *sub = parse_expr(p);
        InterpPart ep; ep.text = NULL; ep.text_len = 0; ep.expr = sub;
        dynarray_push(&parts, &ep);
        if (check(p, TOK_STR_INTERP_MID)) {
            InterpPart lit2; lit2.text = p->cur.start; lit2.text_len = p->cur.len; lit2.expr = NULL;
            dynarray_push(&parts, &lit2);
            advance_tok(p);
            continue;
        }
        if (check(p, TOK_STR_INTERP_END)) {
            InterpPart lit3; lit3.text = p->cur.start; lit3.text_len = p->cur.len; lit3.expr = NULL;
            dynarray_push(&parts, &lit3);
            advance_tok(p);
            break;
        }
        parse_error(p, "malformed string interpolation");
        break;
    }
    Expr *e = new_expr_at(t.line, t.col, EX_STRING_INTERP);
    e->interp_parts = dynarray_take(&parts, &e->interp_part_count);
    return e;
}

/* ===== ブロック・制御構造・リテラル ===== */

static Expr *parse_block(Parser *p) {
    Token t = p->cur;
    expect(p, TOK_LBRACE, "'{'");
    DynArray stmts; dynarray_init(&stmts, sizeof(Stmt *));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Stmt *s = parse_stmt(p);
        dynarray_push(&stmts, &s);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    Expr *e = new_expr_at(t.line, t.col, EX_BLOCK);
    e->stmts = (Stmt **)dynarray_take(&stmts, &e->stmt_count);
    return e;
}

static Expr *parse_if(Parser *p) {
    Token t = p->cur;
    advance_tok(p);
    int saved = p->no_struct_literal; p->no_struct_literal = 1;
    Expr *cond = parse_expr(p);
    p->no_struct_literal = saved;
    Expr *then_b = parse_block(p);
    Expr *else_b = NULL;
    skip_terminators(p);
    /* elseは同じ行/次の行どちらでもよいが、ここでは直後のトークンのみ見る簡易実装 */
    if (check(p, TOK_ELSE)) {
        advance_tok(p);
        if (check(p, TOK_IF)) else_b = parse_if(p);
        else else_b = parse_block(p);
    }
    Expr *e = new_expr_at(t.line, t.col, EX_IF);
    e->a = cond; e->b = then_b; e->c = else_b;
    return e;
}

static Expr *parse_for(Parser *p) {
    Token t = p->cur; advance_tok(p);
    Token var = expect(p, TOK_IDENT, "loop variable");
    expect(p, TOK_IN, "'in'");
    int saved = p->no_struct_literal; p->no_struct_literal = 1;
    Expr *iter = parse_expr(p);
    p->no_struct_literal = saved;
    Expr *body = parse_block(p);
    Expr *e = new_expr_at(t.line, t.col, EX_FOR);
    e->str = var.start; e->str_len = var.len;
    e->a = iter; e->b = body;
    return e;
}

static Expr *parse_while(Parser *p) {
    Token t = p->cur; advance_tok(p);
    int saved = p->no_struct_literal; p->no_struct_literal = 1;
    Expr *cond = parse_expr(p);
    p->no_struct_literal = saved;
    Expr *body = parse_block(p);
    Expr *e = new_expr_at(t.line, t.col, EX_WHILE);
    e->a = cond; e->b = body;
    return e;
}

static Expr *parse_loop(Parser *p) {
    Token t = p->cur; advance_tok(p);
    Expr *body = parse_block(p);
    Expr *e = new_expr_at(t.line, t.col, EX_LOOP);
    e->a = body;
    return e;
}

static Expr *parse_match_arm_body(Parser *p) {
    /* return/break/continue は文法上は文だが、match腕の本体としてよく書かれるため、
       それらが来た場合は1文だけを持つブロック式として包んで返す。 */
    if (check(p, TOK_RETURN) || check(p, TOK_BREAK) || check(p, TOK_CONTINUE)) {
        Token t = p->cur;
        Stmt *s = parse_stmt(p);
        Expr *blk = new_expr_at(t.line, t.col, EX_BLOCK);
        Stmt **arr = xmalloc(sizeof(Stmt *));
        arr[0] = s;
        blk->stmts = arr; blk->stmt_count = 1;
        return blk;
    }
    return parse_expr(p);
}

static Expr *parse_match(Parser *p) {
    Token t = p->cur; advance_tok(p);
    int saved = p->no_struct_literal; p->no_struct_literal = 1;
    Expr *scrut = parse_expr(p);
    p->no_struct_literal = saved;
    expect(p, TOK_LBRACE, "'{' to start match arms");
    skip_terminators(p);
    DynArray arms; dynarray_init(&arms, sizeof(MatchArm));
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Expr *pat = parse_pattern(p);
        expect(p, TOK_FATARROW, "'=>'");
        Expr *body = parse_match_arm_body(p);
        MatchArm arm; arm.pattern = pat; arm.body = body;
        dynarray_push(&arms, &arm);
        skip_terminators(p);
        match(p, TOK_COMMA);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    Expr *e = new_expr_at(t.line, t.col, EX_MATCH);
    e->a = scrut;
    e->arms = dynarray_take(&arms, &e->arm_count);
    return e;
}

static Expr *parse_closure(Parser *p) {
    Token t = p->cur;
    advance_tok(p); /* '|' */
    DynArray params; dynarray_init(&params, sizeof(ClosureParam));
    if (!check(p, TOK_PIPE)) {
        for (;;) {
            Token name = expect(p, TOK_IDENT, "closure parameter name");
            ClosureParam cp; cp.name = token_dup(name); cp.type = NULL;
            if (match(p, TOK_COLON)) cp.type = parse_type(p);
            dynarray_push(&params, &cp);
            if (!match(p, TOK_COMMA)) break;
        }
    }
    expect(p, TOK_PIPE, "'|' to close closure parameters");
    Expr *e = new_expr_at(t.line, t.col, EX_CLOSURE);
    e->params = dynarray_take(&params, &e->param_count);
    if (match(p, TOK_ARROW)) e->type = parse_type(p);
    e->a = check(p, TOK_LBRACE) ? parse_block(p) : parse_expr(p);
    return e;
}

static Expr *parse_closure_no_params(Parser *p) {
    Token t = p->cur;
    advance_tok(p); /* '||' 全体を1トークンとして消費 */
    Expr *e = new_expr_at(t.line, t.col, EX_CLOSURE);
    if (match(p, TOK_ARROW)) e->type = parse_type(p);
    e->a = check(p, TOK_LBRACE) ? parse_block(p) : parse_expr(p);
    return e;
}

static Expr *parse_try_catch(Parser *p) {
    Token t = p->cur; advance_tok(p);
    Expr *try_block = parse_block(p);
    skip_terminators(p);
    expect(p, TOK_CATCH, "'catch'");
    expect(p, TOK_LPAREN, "'('");
    Token name = expect(p, TOK_IDENT, "catch variable name");
    expect(p, TOK_RPAREN, "')'");
    Expr *catch_block = parse_block(p);
    Expr *e = new_expr_at(t.line, t.col, EX_TRY_CATCH);
    e->a = try_block; e->str = name.start; e->str_len = name.len; e->b = catch_block;
    return e;
}

static Expr *parse_struct_literal(Parser *p) {
    Token t = p->cur; advance_tok(p); /* 型名(IDENT) */
    expect(p, TOK_LBRACE, "'{'");
    DynArray inits; dynarray_init(&inits, sizeof(StructFieldInit));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token name = expect(p, TOK_IDENT, "field name");
        expect(p, TOK_COLON, "':'");
        Expr *val = parse_expr(p);
        StructFieldInit fi; fi.name = token_dup(name); fi.value = val;
        dynarray_push(&inits, &fi);
        skip_terminators(p);
        match(p, TOK_COMMA);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    Expr *e = new_expr_at(t.line, t.col, EX_STRUCT_LITERAL);
    e->str = t.start; e->str_len = t.len;
    e->field_inits = dynarray_take(&inits, &e->field_init_count);
    return e;
}

static Expr *parse_array_literal(Parser *p) {
    Token t = p->cur; advance_tok(p); /* '[' */
    DynArray elems; dynarray_init(&elems, sizeof(Expr *));
    int saved = p->no_struct_literal; p->no_struct_literal = 0;
    skip_terminators(p);
    if (!check(p, TOK_RBRACKET)) {
        for (;;) {
            Expr *el = parse_expr(p);
            dynarray_push(&elems, &el);
            skip_terminators(p);
            if (!match(p, TOK_COMMA)) break;
            skip_terminators(p);
        }
    }
    p->no_struct_literal = saved;
    expect(p, TOK_RBRACKET, "']'");
    Expr *e = new_expr_at(t.line, t.col, EX_ARRAY_LITERAL);
    e->list = dynarray_take(&elems, &e->list_count);
    return e;
}

static Expr *parse_paren_or_tuple(Parser *p) {
    Token t = p->cur; advance_tok(p); /* '(' */
    int saved = p->no_struct_literal; p->no_struct_literal = 0;
    skip_terminators(p);
    if (check(p, TOK_RPAREN)) {
        advance_tok(p); p->no_struct_literal = saved;
        return new_expr_at(t.line, t.col, EX_TUPLE_LITERAL);
    }
    Expr *first = parse_expr(p);
    skip_terminators(p);
    if (check(p, TOK_RPAREN)) {
        advance_tok(p); p->no_struct_literal = saved;
        Expr *e = new_expr_at(t.line, t.col, EX_PAREN);
        e->a = first;
        return e;
    }
    DynArray elems; dynarray_init(&elems, sizeof(Expr *));
    dynarray_push(&elems, &first);
    while (match(p, TOK_COMMA)) {
        skip_terminators(p);
        if (check(p, TOK_RPAREN)) break;
        Expr *el = parse_expr(p);
        dynarray_push(&elems, &el);
        skip_terminators(p);
    }
    p->no_struct_literal = saved;
    expect(p, TOK_RPAREN, "')'");
    Expr *e = new_expr_at(t.line, t.col, EX_TUPLE_LITERAL);
    e->list = dynarray_take(&elems, &e->list_count);
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token t = p->cur;
    switch (t.kind) {
        case TOK_INT: { advance_tok(p); Expr *e = new_expr_at(t.line, t.col, EX_INT); e->int_val = t.value.int_val; return e; }
        case TOK_FLOAT: { advance_tok(p); Expr *e = new_expr_at(t.line, t.col, EX_FLOAT); e->float_val = t.value.float_val; return e; }
        case TOK_CHAR: { advance_tok(p); Expr *e = new_expr_at(t.line, t.col, EX_CHAR); e->int_val = t.value.int_val; return e; }
        case TOK_TRUE: advance_tok(p); return new_expr_at(t.line, t.col, EX_TRUE);
        case TOK_FALSE: advance_tok(p); return new_expr_at(t.line, t.col, EX_FALSE);
        case TOK_NULL: advance_tok(p); return new_expr_at(t.line, t.col, EX_NULL);
        case TOK_VOID: advance_tok(p); return new_expr_at(t.line, t.col, EX_VOID);
        case TOK_SELF: advance_tok(p); return new_expr_at(t.line, t.col, EX_SELF);
        case TOK_STRING: { advance_tok(p); Expr *e = new_expr_at(t.line, t.col, EX_STRING); e->str = t.start; e->str_len = t.len; return e; }
        case TOK_STR_INTERP_START: return parse_string_interp(p);
        case TOK_IDENT:
            if (!p->no_struct_literal && peek_next_is(p, TOK_LBRACE)) return parse_struct_literal(p);
            advance_tok(p);
            { Expr *e = new_expr_at(t.line, t.col, EX_IDENT); e->str = t.start; e->str_len = t.len; return e; }
        case TOK_LPAREN: return parse_paren_or_tuple(p);
        case TOK_LBRACKET: return parse_array_literal(p);
        case TOK_LBRACE: return parse_block(p);
        case TOK_IF: return parse_if(p);
        case TOK_MATCH: return parse_match(p);
        case TOK_FOR: return parse_for(p);
        case TOK_WHILE: return parse_while(p);
        case TOK_LOOP: return parse_loop(p);
        case TOK_PIPE: return parse_closure(p);
        case TOK_PIPEPIPE: return parse_closure_no_params(p);
        case TOK_UNSAFE: { advance_tok(p); Expr *e = new_expr_at(t.line, t.col, EX_UNSAFE); e->a = parse_block(p); return e; }
        case TOK_TRY: return parse_try_catch(p);
        default:
            parse_error(p, "unexpected token in expression");
            advance_tok(p);
            return new_expr_at(t.line, t.col, EX_VOID);
    }
}

/* ===== パターン(簡易版: リテラル/識別子/ワイルドカード/enumパス/タプルバリアント) ===== */
static Expr *parse_pattern(Parser *p) {
    if (check(p, TOK_INT) || check(p, TOK_FLOAT) || check(p, TOK_STRING) || check(p, TOK_CHAR) ||
        check(p, TOK_TRUE) || check(p, TOK_FALSE) || check(p, TOK_NULL)) {
        return parse_primary(p);
    }
    if (check(p, TOK_IDENT)) {
        Token t = p->cur; advance_tok(p);
        Expr *e = new_expr_at(t.line, t.col, EX_IDENT);
        e->str = t.start; e->str_len = t.len;
        while (check(p, TOK_DOT)) {
            advance_tok(p);
            Token name = expect(p, TOK_IDENT, "pattern path segment");
            Expr *f = new_expr_at(t.line, t.col, EX_FIELD);
            f->a = e; f->str = name.start; f->str_len = name.len;
            e = f;
        }
        if (check(p, TOK_LPAREN)) {
            advance_tok(p);
            Expr *call = new_expr_at(t.line, t.col, EX_CALL);
            call->a = e;
            DynArray args; dynarray_init(&args, sizeof(Expr *));
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    Expr *sub = parse_pattern(p);
                    dynarray_push(&args, &sub);
                    if (!match(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN, "')'");
            call->list = dynarray_take(&args, &call->list_count);
            e = call;
        }
        return e;
    }
    parse_error(p, "expected pattern");
    Token t = p->cur; advance_tok(p);
    return new_expr_at(t.line, t.col, EX_VOID);
}

/* ===== 文 ===== */
static Stmt *parse_stmt(Parser *p) {
    Token t = p->cur;

    if (check(p, TOK_LET)) {
        advance_tok(p);
        Stmt *s = new_stmt_at(t.line, t.col, ST_LET);
        s->is_mut = match(p, TOK_MUT);
        if (check(p, TOK_LPAREN)) {
            /* タプル分解: let (a, b, ...) = expr */
            advance_tok(p);
            DynArray names; dynarray_init(&names, sizeof(char *));
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    Token nm = expect(p, TOK_IDENT, "variable name in tuple pattern");
                    char *dup = token_dup(nm);
                    dynarray_push(&names, &dup);
                    if (!match(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN, "')' to close tuple pattern");
            s->names = dynarray_take(&names, &s->name_count);
        } else {
            Token name = expect(p, TOK_IDENT, "variable name");
            s->name = token_dup(name);
            if (match(p, TOK_COLON)) s->type_ann = parse_type(p);
        }
        expect(p, TOK_EQ, "'=' in let statement");
        s->expr = parse_expr(p);
        return s;
    }
    if (check(p, TOK_RETURN)) {
        advance_tok(p);
        Stmt *s = new_stmt_at(t.line, t.col, ST_RETURN);
        if (!at_terminator(p) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) s->expr = parse_expr(p);
        return s;
    }
    if (check(p, TOK_BREAK)) {
        advance_tok(p);
        Stmt *s = new_stmt_at(t.line, t.col, ST_BREAK);
        if (!at_terminator(p) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) s->expr = parse_expr(p);
        return s;
    }
    if (check(p, TOK_CONTINUE)) { advance_tok(p); return new_stmt_at(t.line, t.col, ST_CONTINUE); }
    if (check(p, TOK_DEFER)) {
        advance_tok(p);
        Stmt *s = new_stmt_at(t.line, t.col, ST_DEFER);
        s->expr = parse_expr(p);
        return s;
    }
    if (check(p, TOK_FN) || check(p, TOK_STRUCT) || check(p, TOK_ENUM) || check(p, TOK_TRAIT) ||
        check(p, TOK_IMPL) || check(p, TOK_ERROR) || check(p, TOK_UNION) || check(p, TOK_HASHBRACKET) ||
        check(p, TOK_PUB) || check(p, TOK_COMPTIME)) {
        Stmt *s = new_stmt_at(t.line, t.col, ST_ITEM);
        s->item = parse_item(p);
        return s;
    }

    Stmt *s = new_stmt_at(t.line, t.col, ST_EXPR);
    s->expr = parse_expr(p);
    return s;
}

/* ===== アイテム(宣言) ===== */

static char *parse_attribute_arg_text(Parser *p) {
    if (check(p, TOK_STRING)) { Token s = advance_tok(p); return token_dup(s); }
    Token first = expect(p, TOK_IDENT, "attribute argument");
    char buf[256];
    int n = 0;
    for (size_t i = 0; i < first.len && n < 250; i++) buf[n++] = first.start[i];
    /* x86_64-freestanding のようなハイフン入り識別子の並びを1つの文字列に再構築する */
    while (check(p, TOK_MINUS) && peek_next_is(p, TOK_IDENT)) {
        advance_tok(p);
        Token id = advance_tok(p);
        if (n < 250) buf[n++] = '-';
        for (size_t i = 0; i < id.len && n < 250; i++) buf[n++] = id.start[i];
    }
    buf[n] = '\0';
    return xstrndup(buf, (size_t)n);
}

static Attribute parse_one_attribute(Parser *p) {
    advance_tok(p); /* '#[' */
    Attribute a; memset(&a, 0, sizeof(a));
    Token name = expect(p, TOK_IDENT, "attribute name");
    a.name = token_dup(name);
    if (match(p, TOK_LPAREN)) {
        DynArray args; dynarray_init(&args, sizeof(char *));
        if (!check(p, TOK_RPAREN)) {
            for (;;) {
                char *arg = parse_attribute_arg_text(p);
                dynarray_push(&args, &arg);
                if (!match(p, TOK_COMMA)) break;
            }
        }
        expect(p, TOK_RPAREN, "')'");
        a.args = dynarray_take(&args, &a.arg_count);
    }
    expect(p, TOK_RBRACKET, "']'");
    return a;
}

static void parse_dotted_path(Parser *p, char ***out, int *out_count) {
    DynArray segs; dynarray_init(&segs, sizeof(char *));
    Token first = expect(p, TOK_IDENT, "identifier");
    char *s = token_dup(first);
    dynarray_push(&segs, &s);
    while (check(p, TOK_DOT)) {
        advance_tok(p);
        Token id = expect(p, TOK_IDENT, "path segment");
        char *s2 = token_dup(id);
        dynarray_push(&segs, &s2);
    }
    *out = dynarray_take(&segs, out_count);
}

static void parse_fn_body(Parser *p, Item *item) {
    item->kind = IT_FN;
    advance_tok(p); /* 'fn' */
    Token name = expect(p, TOK_IDENT, "function name");
    item->name = token_dup(name);
    parse_generic_params(p, &item->generics, &item->generic_count);
    parse_params(p, &item->params, &item->param_count);
    if (match(p, TOK_ARROW)) item->return_type = parse_type(p);
    if (check(p, TOK_LBRACE)) item->body = parse_block(p);
}

static void parse_field_list(Parser *p, Field **out, int *out_count) {
    expect(p, TOK_LBRACE, "'{'");
    DynArray fields; dynarray_init(&fields, sizeof(Field));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Field f; memset(&f, 0, sizeof(f));
        f.is_pub = match(p, TOK_PUB);
        Token fname = expect(p, TOK_IDENT, "field name");
        f.name = token_dup(fname);
        expect(p, TOK_COLON, "':'");
        f.type = parse_type(p);
        dynarray_push(&fields, &f);
        skip_terminators(p);
        match(p, TOK_COMMA);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    *out = dynarray_take(&fields, out_count);
}

static void parse_struct_body(Parser *p, Item *item) {
    item->kind = IT_STRUCT;
    advance_tok(p); /* 'struct' */
    Token name = expect(p, TOK_IDENT, "struct name");
    item->name = token_dup(name);
    parse_generic_params(p, &item->generics, &item->generic_count);
    parse_field_list(p, &item->fields, &item->field_count);
}

static void parse_union_body(Parser *p, Item *item) {
    item->kind = IT_UNION;
    advance_tok(p); /* 'union' */
    Token name = expect(p, TOK_IDENT, "union name");
    item->name = token_dup(name);
    parse_field_list(p, &item->fields, &item->field_count);
}

static void parse_enum_body(Parser *p, Item *item, ItemKind kind) {
    item->kind = kind;
    advance_tok(p); /* 'enum' または 'error' */
    Token name = expect(p, TOK_IDENT, "name");
    item->name = token_dup(name);
    if (kind == IT_ENUM) parse_generic_params(p, &item->generics, &item->generic_count);
    expect(p, TOK_LBRACE, "'{'");
    DynArray variants; dynarray_init(&variants, sizeof(Variant));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Variant v; memset(&v, 0, sizeof(v));
        Token vname = expect(p, TOK_IDENT, "variant name");
        v.name = token_dup(vname);
        if (check(p, TOK_LPAREN)) {
            advance_tok(p);
            DynArray types; dynarray_init(&types, sizeof(Type *));
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    Type *ty = parse_type(p);
                    dynarray_push(&types, &ty);
                    if (!match(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN, "')'");
            v.tuple_types = dynarray_take(&types, &v.tuple_type_count);
        } else if (check(p, TOK_LBRACE)) {
            parse_field_list(p, &v.struct_fields, &v.struct_field_count);
        }
        dynarray_push(&variants, &v);
        skip_terminators(p);
        match(p, TOK_COMMA);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    item->variants = dynarray_take(&variants, &item->variant_count);
}

static void parse_trait_body(Parser *p, Item *item) {
    item->kind = IT_TRAIT;
    advance_tok(p); /* 'trait' */
    Token name = expect(p, TOK_IDENT, "trait name");
    item->name = token_dup(name);
    parse_generic_params(p, &item->generics, &item->generic_count);
    expect(p, TOK_LBRACE, "'{'");
    DynArray methods; dynarray_init(&methods, sizeof(Item *));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Item *m = new_item();
        m->line = p->cur.line; m->col = p->cur.col;
        parse_fn_body(p, m);
        dynarray_push(&methods, &m);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    item->methods = (Item **)dynarray_take(&methods, &item->method_count);
}

static void parse_impl_body(Parser *p, Item *item) {
    item->kind = IT_IMPL;
    advance_tok(p); /* 'impl' */
    parse_generic_params(p, &item->generics, &item->generic_count);
    Type *first = parse_type(p);
    if (match(p, TOK_FOR)) {
        item->impl_trait_type = first;
        item->impl_type = parse_type(p);
    } else {
        item->impl_type = first;
    }
    expect(p, TOK_LBRACE, "'{'");
    DynArray methods; dynarray_init(&methods, sizeof(Item *));
    skip_terminators(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Item *m = new_item();
        m->line = p->cur.line; m->col = p->cur.col;
        parse_fn_body(p, m);
        dynarray_push(&methods, &m);
        skip_terminators(p);
    }
    expect(p, TOK_RBRACE, "'}'");
    item->methods = (Item **)dynarray_take(&methods, &item->method_count);
}

static void parse_module_body(Parser *p, Item *item) {
    item->kind = IT_MODULE;
    advance_tok(p); /* 'module' */
    parse_dotted_path(p, &item->path_segments, &item->path_segment_count);
}

static void parse_import_body(Parser *p, Item *item) {
    item->kind = IT_IMPORT;
    advance_tok(p); /* 'import' */
    if (check(p, TOK_STRING)) {
        Token s = advance_tok(p);
        item->path_segments = xmalloc(sizeof(char *));
        item->path_segments[0] = token_dup(s);
        item->path_segment_count = 1;
    } else {
        parse_dotted_path(p, &item->path_segments, &item->path_segment_count);
    }
    if (match(p, TOK_AS)) {
        Token alias = expect(p, TOK_IDENT, "import alias");
        item->alias = token_dup(alias);
    }
}

static Item *parse_item(Parser *p) {
    int line = p->cur.line, col = p->cur.col;
    DynArray attrs; dynarray_init(&attrs, sizeof(Attribute));
    while (check(p, TOK_HASHBRACKET)) {
        Attribute a = parse_one_attribute(p);
        dynarray_push(&attrs, &a);
        skip_terminators(p);
    }

    Item *item = new_item();
    item->line = line; item->col = col;
    item->attrs = dynarray_take(&attrs, &item->attr_count);
    item->is_pub = match(p, TOK_PUB);

    if (check(p, TOK_MODULE)) { parse_module_body(p, item); return item; }
    if (check(p, TOK_IMPORT)) { parse_import_body(p, item); return item; }

    item->is_comptime = match(p, TOK_COMPTIME);
    item->is_async = match(p, TOK_ASYNC);

    if (check(p, TOK_EXTERN)) {
        advance_tok(p);
        item->is_extern = 1;
        if (check(p, TOK_STRING)) { item->extern_abi = token_dup(p->cur); advance_tok(p); }
    }

    if (check(p, TOK_FN))     { parse_fn_body(p, item); return item; }
    if (check(p, TOK_STRUCT)) { parse_struct_body(p, item); return item; }
    if (check(p, TOK_ENUM))   { parse_enum_body(p, item, IT_ENUM); return item; }
    if (check(p, TOK_ERROR))  { parse_enum_body(p, item, IT_ERROR); return item; }
    if (check(p, TOK_UNION))  { parse_union_body(p, item); return item; }
    if (check(p, TOK_TRAIT))  { parse_trait_body(p, item); return item; }
    if (check(p, TOK_IMPL))   { parse_impl_body(p, item); return item; }

    parse_error(p, "expected item declaration (fn/struct/enum/trait/impl/error/union/module/import)");
    advance_tok(p);
    item->kind = IT_FN;
    return item;
}

/* ===== エントリポイント ===== */

void parser_init(Parser *p, const char *src, size_t len) {
    memset(p, 0, sizeof(*p));
    lexer_init(&p->lexer, src, len);
    p->cur = lexer_next(&p->lexer);
    p->has_peek = 0;
    p->no_struct_literal = 0;
}

Program *parser_parse_program(Parser *p) {
    DynArray items; dynarray_init(&items, sizeof(Item *));
    skip_terminators(p);
    while (!check(p, TOK_EOF)) {
        Item *it = parse_item(p);
        dynarray_push(&items, &it);
        skip_terminators(p);
    }
    if (p->lexer.had_error && !p->had_error) {
        p->had_error = 1;
        p->error_count++;
        p->error_line = p->lexer.error_line;
        p->error_col = p->lexer.error_col;
        snprintf(p->error_msg, sizeof(p->error_msg), "lexer error: %s", p->lexer.error_msg);
    }
    Program *prog = xmalloc(sizeof(Program));
    prog->items = (Item **)dynarray_take(&items, &prog->item_count);
    return prog;
}
