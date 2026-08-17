/* irgen.c */
#include "irgen.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

typedef struct { const char *name; int vreg; } LocalVar;

typedef struct {
    LocalVar *vars; int count; int cap;
    IrFunc *f;
    IrProgram *prog;
    int failed;
    char fail_reason[128];
    /* break/continueが飛ぶべきラベルのスタック(ループのネスト分) */
    int loop_break[32];
    int loop_continue[32];
    int loop_depth;
} LowerCtx;

static void fail(LowerCtx *ctx, const char *reason) {
    if (!ctx->failed) { ctx->failed = 1; snprintf(ctx->fail_reason, sizeof(ctx->fail_reason), "%s", reason); }
}

static int local_mark(LowerCtx *ctx) { return ctx->count; }
static void local_release(LowerCtx *ctx, int mark) { ctx->count = mark; }

static void local_declare(LowerCtx *ctx, const char *name, int vreg) {
    if (ctx->count >= ctx->cap) {
        int nc = ctx->cap == 0 ? 16 : ctx->cap * 2;
        ctx->vars = xrealloc(ctx->vars, (size_t)nc * sizeof(LocalVar));
        ctx->cap = nc;
    }
    ctx->vars[ctx->count].name = name;
    ctx->vars[ctx->count].vreg = vreg;
    ctx->count++;
}

static int local_lookup(LowerCtx *ctx, const char *name, size_t len) {
    for (int i = ctx->count - 1; i >= 0; i--) {
        if (strlen(ctx->vars[i].name) == len && memcmp(ctx->vars[i].name, name, len) == 0) return ctx->vars[i].vreg;
    }
    return -1;
}

/* このコード生成パスが対応するスカラ型かどうか(構造体/文字列/参照/ポインタ/
   ジェネリクス等は非対応)。 */
static int is_supported_scalar_type(Type *t) {
    if (!t) return 0;
    if (t->kind != TY_PATH || t->segment_count != 1) return 0;
    static const char *ok[] = {
        "i8","i16","i32","i64","isize","u8","u16","u32","u64","usize","bool","void", NULL
    };
    for (int i = 0; ok[i]; i++) if (strcmp(t->segments[0], ok[i]) == 0) return 1;
    return 0;
}

static int emit_binop(LowerCtx *ctx, IrOp op, int a, int b) {
    int dst = ir_new_vreg(ctx->f);
    IrInstr ins; memset(&ins, 0, sizeof(ins));
    ins.op = op; ins.dst = dst; ins.a = a; ins.b = b; ins.imm = 0; ins.call_func = -1;
    ir_emit(ctx->f, ins);
    return dst;
}

static int emit_const(LowerCtx *ctx, long long v) {
    int dst = ir_new_vreg(ctx->f);
    IrInstr ins; memset(&ins, 0, sizeof(ins));
    ins.op = IR_CONST; ins.dst = dst; ins.a = -1; ins.b = -1; ins.imm = v; ins.call_func = -1;
    ir_emit(ctx->f, ins);
    return dst;
}

static void emit_label(LowerCtx *ctx, int label) {
    IrInstr ins; memset(&ins, 0, sizeof(ins));
    ins.op = IR_LABEL; ins.dst = -1; ins.a = -1; ins.b = -1; ins.imm = label; ins.call_func = -1;
    ir_emit(ctx->f, ins);
}
static void emit_jump(LowerCtx *ctx, int label) {
    IrInstr ins; memset(&ins, 0, sizeof(ins));
    ins.op = IR_JUMP; ins.dst = -1; ins.a = -1; ins.b = -1; ins.imm = label; ins.call_func = -1;
    ir_emit(ctx->f, ins);
}
static void emit_jz(LowerCtx *ctx, int cond, int label) {
    IrInstr ins; memset(&ins, 0, sizeof(ins));
    ins.op = IR_JUMP_IF_ZERO; ins.dst = -1; ins.a = cond; ins.b = -1; ins.imm = label; ins.call_func = -1;
    ir_emit(ctx->f, ins);
}

static int lower_expr(LowerCtx *ctx, Expr *e);

static IrOp binop_to_irop(const char *op, size_t len, int *is_cmp) {
    *is_cmp = 0;
    if (len == 1) {
        if (op[0] == '+') return IR_ADD;
        if (op[0] == '-') return IR_SUB;
        if (op[0] == '*') return IR_MUL;
        if (op[0] == '/') return IR_SDIV;
        if (op[0] == '%') return IR_SMOD;
        if (op[0] == '&') return IR_AND;
        if (op[0] == '|') return IR_OR;
        if (op[0] == '^') return IR_XOR;
        if (op[0] == '<') { *is_cmp = 1; return IR_CMP_LT; }
        if (op[0] == '>') { *is_cmp = 1; return IR_CMP_GT; }
    }
    if (len == 2) {
        if (memcmp(op, "==", 2) == 0) { *is_cmp = 1; return IR_CMP_EQ; }
        if (memcmp(op, "!=", 2) == 0) { *is_cmp = 1; return IR_CMP_NE; }
        if (memcmp(op, "<=", 2) == 0) { *is_cmp = 1; return IR_CMP_LE; }
        if (memcmp(op, ">=", 2) == 0) { *is_cmp = 1; return IR_CMP_GE; }
        if (memcmp(op, "&&", 2) == 0) return IR_AND; /* boolは0/1表現なので論理積とビット積が一致する */
        if (memcmp(op, "||", 2) == 0) return IR_OR;
        if (memcmp(op, "<<", 2) == 0) return IR_SHL;
        if (memcmp(op, ">>", 2) == 0) return IR_SAR;
    }
    return IR_ADD; /* 到達しない想定 */
}

static int lower_expr(LowerCtx *ctx, Expr *e) {
    if (ctx->failed) return -1;
    switch (e->kind) {
        case EX_INT: return emit_const(ctx, e->int_val);
        case EX_TRUE: return emit_const(ctx, 1);
        case EX_FALSE: return emit_const(ctx, 0);
        case EX_PAREN: return lower_expr(ctx, e->a);
        case EX_IDENT: {
            int v = local_lookup(ctx, e->str, e->str_len);
            if (v < 0) { fail(ctx, "reference to a name this backend cannot resolve"); return -1; }
            return v;
        }
        case EX_UNARY: {
            if (strcmp(e->str, "-") == 0) { int a = lower_expr(ctx, e->a); return emit_binop(ctx, IR_NEG, a, -1); }
            if (strcmp(e->str, "!") == 0) { int a = lower_expr(ctx, e->a); return emit_binop(ctx, IR_CMP_EQ, a, emit_const(ctx, 0)); }
            fail(ctx, "unary '&'/'*' (references/raw pointers) are not supported by this codegen pass");
            return -1;
        }
        case EX_BINARY: {
            int a = lower_expr(ctx, e->a);
            int b = lower_expr(ctx, e->b);
            int is_cmp;
            IrOp op = binop_to_irop(e->str, e->str_len, &is_cmp);
            return emit_binop(ctx, op, a, b);
        }
        case EX_CALL: {
            if (e->a->kind != EX_IDENT) { fail(ctx, "only direct calls to named functions are supported"); return -1; }
            int fi = ir_find_func(ctx->prog, xstrndup(e->a->str, e->a->str_len));
            if (fi < 0) { fail(ctx, "call to a function this backend hasn't compiled (external/unsupported)"); return -1; }
            int *args = e->list_count ? xmalloc(sizeof(int) * (size_t)e->list_count) : NULL;
            for (int i = 0; i < e->list_count; i++) args[i] = lower_expr(ctx, e->list[i]);
            int dst = ir_new_vreg(ctx->f);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_CALL; ins.dst = dst; ins.a = -1; ins.b = -1; ins.call_func = fi;
            ins.args = args; ins.arg_count = e->list_count;
            ir_emit(ctx->f, ins);
            return dst;
        }
        case EX_CAST: return lower_expr(ctx, e->a); /* スカラ間キャストは値をそのまま使う(幅の厳密な扱いは今後の課題) */
        default:
            fail(ctx, "expression kind not supported by this codegen pass (structs/strings/closures/etc.)");
            return -1;
    }
}

static void lower_block(LowerCtx *ctx, Expr *block);

static void lower_if(LowerCtx *ctx, Expr *ifexpr) {
    int cond = lower_expr(ctx, ifexpr->a);
    int else_label = ir_new_label(ctx->f);
    int end_label = ir_new_label(ctx->f);
    emit_jz(ctx, cond, else_label);
    int mark = local_mark(ctx);
    lower_block(ctx, ifexpr->b);
    local_release(ctx, mark);
    emit_jump(ctx, end_label);
    emit_label(ctx, else_label);
    if (ifexpr->c) {
        mark = local_mark(ctx);
        if (ifexpr->c->kind == EX_IF) lower_if(ctx, ifexpr->c);
        else lower_block(ctx, ifexpr->c);
        local_release(ctx, mark);
    }
    emit_label(ctx, end_label);
}

static void lower_while(LowerCtx *ctx, Expr *wexpr) {
    if (ctx->loop_depth >= 32) { fail(ctx, "loop nesting too deep"); return; }
    int start_label = ir_new_label(ctx->f);
    int end_label = ir_new_label(ctx->f);
    emit_label(ctx, start_label);
    int cond = lower_expr(ctx, wexpr->a);
    emit_jz(ctx, cond, end_label);
    ctx->loop_break[ctx->loop_depth] = end_label;
    ctx->loop_continue[ctx->loop_depth] = start_label;
    ctx->loop_depth++;
    int mark = local_mark(ctx);
    lower_block(ctx, wexpr->b);
    local_release(ctx, mark);
    ctx->loop_depth--;
    emit_jump(ctx, start_label);
    emit_label(ctx, end_label);
}

static void lower_stmt(LowerCtx *ctx, Stmt *s) {
    if (ctx->failed) return;
    switch (s->kind) {
        case ST_LET: {
            if (s->names) { fail(ctx, "tuple-destructuring let is not supported by this codegen pass"); return; }
            int v = lower_expr(ctx, s->expr);
            int slot = ir_new_vreg(ctx->f);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_MOVE; ins.dst = slot; ins.a = v; ins.b = -1; ins.call_func = -1;
            ir_emit(ctx->f, ins);
            local_declare(ctx, s->name, slot);
            return;
        }
        case ST_EXPR:
            if (s->expr->kind == EX_IF) { lower_if(ctx, s->expr); return; }
            if (s->expr->kind == EX_WHILE) { lower_while(ctx, s->expr); return; }
            if (s->expr->kind == EX_BLOCK) {
                int mark = local_mark(ctx);
                lower_block(ctx, s->expr);
                local_release(ctx, mark);
                return;
            }
            if (s->expr->kind == EX_ASSIGN) {
                if (s->expr->a->kind != EX_IDENT) { fail(ctx, "assignment target is not a simple local (fields/indexing not supported)"); return; }
                int target = local_lookup(ctx, s->expr->a->str, s->expr->a->str_len);
                if (target < 0) { fail(ctx, "assignment to an unresolved local"); return; }
                int v = lower_expr(ctx, s->expr->b);
                IrInstr ins; memset(&ins, 0, sizeof(ins));
                ins.op = IR_MOVE; ins.dst = target; ins.a = v; ins.b = -1; ins.call_func = -1;
                ir_emit(ctx->f, ins);
                return;
            }
            lower_expr(ctx, s->expr);
            return;
        case ST_RETURN: {
            int v = s->expr ? lower_expr(ctx, s->expr) : -1;
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_RET; ins.dst = -1; ins.a = v; ins.b = -1; ins.call_func = -1;
            ir_emit(ctx->f, ins);
            return;
        }
        case ST_BREAK:
            if (ctx->loop_depth == 0) { fail(ctx, "break outside of a loop"); return; }
            emit_jump(ctx, ctx->loop_break[ctx->loop_depth - 1]);
            return;
        case ST_CONTINUE:
            if (ctx->loop_depth == 0) { fail(ctx, "continue outside of a loop"); return; }
            emit_jump(ctx, ctx->loop_continue[ctx->loop_depth - 1]);
            return;
        default:
            fail(ctx, "statement kind not supported by this codegen pass (defer/nested items/etc.)");
            return;
    }
}

static void lower_block(LowerCtx *ctx, Expr *block) {
    for (int i = 0; i < block->stmt_count && !ctx->failed; i++) lower_stmt(ctx, block->stmts[i]);
}

static int fn_is_eligible(Item *fn) {
    if (!fn->body) return 0;
    if (fn->generic_count > 0) return 0;
    if (fn->is_async) return 0;
    if (fn->return_type && !is_supported_scalar_type(fn->return_type)) return 0;
    if (!fn->return_type) return 0; /* 戻り値型省略は今回未対応(明示を要求) */
    for (int i = 0; i < fn->param_count; i++) {
        if (!fn->params[i].type || !is_supported_scalar_type(fn->params[i].type)) return 0;
    }
    return 1;
}

LowerResult lower_program(Program *prog) {
    LowerResult res; memset(&res, 0, sizeof(res));
    res.ir = ir_program_new();

    /* 1st pass: 対応可能な関数だけを先に登録する(前方参照・相互再帰を許すため) */
    for (int i = 0; i < prog->item_count; i++) {
        Item *it = prog->items[i];
        if (it->kind != IT_FN) continue;
        if (!fn_is_eligible(it)) continue;
        ir_func_new(res.ir, it->name, it->param_count);
    }

    DynArray skipped; dynarray_init(&skipped, sizeof(SkippedFn));

    for (int i = 0; i < prog->item_count; i++) {
        Item *it = prog->items[i];
        if (it->kind != IT_FN) continue;
        if (!fn_is_eligible(it)) {
            SkippedFn sf;
            sf.fn_name = it->name;
            sf.reason = "signature uses a type this codegen pass doesn't support yet (non-scalar param/return, generics, or async)";
            dynarray_push(&skipped, &sf);
            continue;
        }
        int fi = ir_find_func(res.ir, it->name);
        IrFunc *f = res.ir->funcs[fi];
        LowerCtx ctx; memset(&ctx, 0, sizeof(ctx));
        ctx.f = f; ctx.prog = res.ir;

        for (int p = 0; p < it->param_count; p++) {
            int v = ir_new_vreg(f);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_PARAM; ins.dst = v; ins.a = -1; ins.b = -1; ins.imm = p; ins.call_func = -1;
            ir_emit(f, ins);
            local_declare(&ctx, it->params[p].name, v);
        }

        lower_block(&ctx, it->body);
        /* 関数末尾の安全策: 明示returnで終わらない経路があってもクラッシュしないよう
           voidとしてのretを1つ追加しておく(戻り値の正しさはソース側の責務)。 */
        IrInstr ret; memset(&ret, 0, sizeof(ret));
        ret.op = IR_RET; ret.dst = -1; ret.a = -1; ret.b = -1; ret.call_func = -1;
        ir_emit(f, ret);

        if (ctx.failed) {
            SkippedFn sf; sf.fn_name = it->name; sf.reason = xstrndup(ctx.fail_reason, strlen(ctx.fail_reason));
            dynarray_push(&skipped, &sf);
            /* 失敗した関数はIrProgramから除外する: 単純にfunc配列から取り除くのは
               インデックスの整合性を崩すため、代わりに本体を「即returnのみ」に
               差し替えて存在だけは保つ(他関数からの前方参照を壊さないため)。
               呼び出されると未定義動作になるが、そもそも生成失敗を報告しユーザーに
               知らせているため、実運用ではリンク前に発見される想定。 */
            f->instr_count = 0;
            IrInstr r2; memset(&r2, 0, sizeof(r2));
            r2.op = IR_RET; r2.dst = -1; r2.a = -1; r2.b = -1; r2.call_func = -1;
            ir_emit(f, r2);
        }
    }

    res.skipped = dynarray_take(&skipped, &res.skipped_count);
    return res;
}
