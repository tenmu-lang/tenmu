/* comptime.c */
#include "comptime.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

typedef struct EnvBinding {
    const char *name; size_t name_len;
    ComptimeValue value;
    struct EnvBinding *next;
} EnvBinding;

typedef struct Env {
    EnvBinding *bindings;
    struct Env *parent;
} Env;

static ComptimeValue cv_int(long long i)   { ComptimeValue v; memset(&v,0,sizeof(v)); v.kind = CV_INT; v.as.i = i; return v; }
static ComptimeValue cv_float(double f)    { ComptimeValue v; memset(&v,0,sizeof(v)); v.kind = CV_FLOAT; v.as.f = f; return v; }
static ComptimeValue cv_bool(int b)        { ComptimeValue v; memset(&v,0,sizeof(v)); v.kind = CV_BOOL; v.as.b = b ? 1 : 0; return v; }
static ComptimeValue cv_void(void)         { ComptimeValue v; memset(&v,0,sizeof(v)); v.kind = CV_VOID; return v; }
static ComptimeValue cv_error(ComptimeEvalCtx *ctx, const char *fmt, ...) {
    ComptimeValue v; memset(&v,0,sizeof(v)); v.kind = CV_ERROR;
    va_list ap; va_start(ap, fmt);
    vsnprintf(v.error_msg, sizeof(v.error_msg), fmt, ap);
    va_end(ap);
    ctx->had_error = 1;
    return v;
}

static double as_f(ComptimeValue v) { return v.kind == CV_FLOAT ? v.as.f : (double)v.as.i; }
static int is_num(ComptimeValue v) { return v.kind == CV_INT || v.kind == CV_FLOAT; }

static Env *env_push(Env *parent) {
    Env *e = xmalloc(sizeof(Env));
    e->bindings = NULL;
    e->parent = parent;
    return e;
}

static void env_bind(Env *env, const char *name, size_t len, ComptimeValue v) {
    EnvBinding *b = xmalloc(sizeof(EnvBinding));
    b->name = name; b->name_len = len; b->value = v;
    b->next = env->bindings;
    env->bindings = b;
}

static int env_lookup(Env *env, const char *name, size_t len, ComptimeValue *out) {
    for (Env *e = env; e; e = e->parent) {
        for (EnvBinding *b = e->bindings; b; b = b->next) {
            if (b->name_len == len && memcmp(b->name, name, len) == 0) { *out = b->value; return 1; }
        }
    }
    return 0;
}

void comptime_init(ComptimeEvalCtx *ctx, Scope *global) {
    ctx->global = global;
    ctx->call_depth = 0;
    ctx->step_count = 0;
    ctx->had_error = 0;
}

static ComptimeValue eval_block_stmts(ComptimeEvalCtx *ctx, Env *env, Expr *block);

static ComptimeValue eval_expr_env(ComptimeEvalCtx *ctx, Env *env, Expr *e);

static ComptimeValue eval_call(ComptimeEvalCtx *ctx, Env *env, Expr *e) {
    if (e->a->kind != EX_IDENT) return cv_error(ctx, "comptime: only direct calls to named functions are supported");
    ComptimeValue args[32];
    int n = e->list_count < 32 ? e->list_count : 32;
    for (int i = 0; i < n; i++) {
        args[i] = eval_expr_env(ctx, env, e->list[i]);
        if (args[i].kind == CV_ERROR) return args[i];
    }
    Symbol *sym = scope_lookup(ctx->global, e->a->str, e->a->str_len);
    if (!sym || sym->kind != SYM_FN) return cv_error(ctx, "comptime: '%.*s' is not a known function", (int)e->a->str_len, e->a->str);
    if (!sym->item->is_comptime) return cv_error(ctx, "comptime: '%.*s' is not marked comptime", (int)e->a->str_len, e->a->str);
    return comptime_call_fn(ctx, sym->item, args, n);
}

static ComptimeValue eval_expr_env(ComptimeEvalCtx *ctx, Env *env, Expr *e) {
    if (!e) return cv_void();
    if (++ctx->step_count > COMPTIME_MAX_STEPS) return cv_error(ctx, "comptime evaluation exceeded step limit (%d) — possible infinite loop/recursion", COMPTIME_MAX_STEPS);

    switch (e->kind) {
        case EX_INT: return cv_int(e->int_val);
        case EX_FLOAT: return cv_float(e->float_val);
        case EX_TRUE: return cv_bool(1);
        case EX_FALSE: return cv_bool(0);
        case EX_VOID: return cv_void();
        case EX_PAREN: return eval_expr_env(ctx, env, e->a);

        case EX_IDENT: {
            ComptimeValue v;
            if (env_lookup(env, e->str, e->str_len, &v)) return v;
            Symbol *sym = scope_lookup(ctx->global, e->str, e->str_len);
            if (sym && sym->kind == SYM_CONST && sym->item->const_value) {
                return eval_expr_env(ctx, NULL, sym->item->const_value);
            }
            return cv_error(ctx, "comptime: undefined name '%.*s'", (int)e->str_len, e->str);
        }

        case EX_UNARY: {
            ComptimeValue a = eval_expr_env(ctx, env, e->a);
            if (a.kind == CV_ERROR) return a;
            if (strcmp(e->str, "-") == 0) {
                if (a.kind == CV_FLOAT) return cv_float(-a.as.f);
                if (a.kind == CV_INT) return cv_int(-a.as.i);
                return cv_error(ctx, "comptime: unary '-' needs a number");
            }
            if (strcmp(e->str, "!") == 0) {
                if (a.kind != CV_BOOL) return cv_error(ctx, "comptime: unary '!' needs a bool");
                return cv_bool(!a.as.b);
            }
            return cv_error(ctx, "comptime: unsupported unary operator in constant expression");
        }

        case EX_BINARY: {
            ComptimeValue a = eval_expr_env(ctx, env, e->a);
            if (a.kind == CV_ERROR) return a;
            ComptimeValue b = eval_expr_env(ctx, env, e->b);
            if (b.kind == CV_ERROR) return b;
            const char *op = e->str;

            if (strcmp(op, "&&") == 0) { if (a.kind != CV_BOOL || b.kind != CV_BOOL) return cv_error(ctx, "comptime: '&&' needs bools"); return cv_bool(a.as.b && b.as.b); }
            if (strcmp(op, "||") == 0) { if (a.kind != CV_BOOL || b.kind != CV_BOOL) return cv_error(ctx, "comptime: '||' needs bools"); return cv_bool(a.as.b || b.as.b); }

            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
                int eq;
                if (a.kind == CV_BOOL && b.kind == CV_BOOL) eq = (a.as.b == b.as.b);
                else if (is_num(a) && is_num(b)) eq = (as_f(a) == as_f(b));
                else return cv_error(ctx, "comptime: cannot compare these values");
                return cv_bool(strcmp(op, "==") == 0 ? eq : !eq);
            }
            if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
                if (!is_num(a) || !is_num(b)) return cv_error(ctx, "comptime: comparison needs numbers");
                double x = as_f(a), y = as_f(b);
                if (strcmp(op, "<") == 0) return cv_bool(x < y);
                if (strcmp(op, ">") == 0) return cv_bool(x > y);
                if (strcmp(op, "<=") == 0) return cv_bool(x <= y);
                return cv_bool(x >= y);
            }

            if (!is_num(a) || !is_num(b)) return cv_error(ctx, "comptime: arithmetic needs numbers");
            int use_float = (a.kind == CV_FLOAT || b.kind == CV_FLOAT);
            if (strcmp(op, "+") == 0) return use_float ? cv_float(as_f(a) + as_f(b)) : cv_int(a.as.i + b.as.i);
            if (strcmp(op, "-") == 0) return use_float ? cv_float(as_f(a) - as_f(b)) : cv_int(a.as.i - b.as.i);
            if (strcmp(op, "*") == 0) return use_float ? cv_float(as_f(a) * as_f(b)) : cv_int(a.as.i * b.as.i);
            if (strcmp(op, "/") == 0) {
                if (use_float) { if (as_f(b) == 0.0) return cv_error(ctx, "comptime: division by zero"); return cv_float(as_f(a) / as_f(b)); }
                if (b.as.i == 0) return cv_error(ctx, "comptime: division by zero");
                return cv_int(a.as.i / b.as.i);
            }
            if (strcmp(op, "%") == 0) {
                if (use_float) return cv_float(fmod(as_f(a), as_f(b)));
                if (b.as.i == 0) return cv_error(ctx, "comptime: division by zero");
                return cv_int(a.as.i % b.as.i);
            }
            return cv_error(ctx, "comptime: operator '%s' is not supported in constant expressions", op);
        }

        case EX_IF: {
            ComptimeValue cond = eval_expr_env(ctx, env, e->a);
            if (cond.kind == CV_ERROR) return cond;
            if (cond.kind != CV_BOOL) return cv_error(ctx, "comptime: 'if' condition must be bool");
            ComptimeValue r;
            if (cond.as.b) r = eval_expr_env(ctx, env, e->b);
            else if (e->c) r = eval_expr_env(ctx, env, e->c);
            else r = cv_void();
            /* ctx->returning が立っていれば、rは「if式の値」ではなく
               「関数からのreturn値」なので、そのまま上位へ伝播させる
               (呼び出し元のブロック走査ループがctx->returningを見て停止する)。 */
            return r;
        }

        case EX_BLOCK: {
            Env *inner = env_push(env);
            return eval_block_stmts(ctx, inner, e);
        }

        case EX_CALL: return eval_call(ctx, env, e);

        default:
            return cv_error(ctx, "comptime: expression kind not supported in constant evaluation");
    }
}

static ComptimeValue eval_stmt(ComptimeEvalCtx *ctx, Env *env, Stmt *s) {
    switch (s->kind) {
        case ST_LET: {
            ComptimeValue v = eval_expr_env(ctx, env, s->expr);
            if (v.kind == CV_ERROR) return v;
            if (s->names) {
                for (int i = 0; i < s->name_count; i++) env_bind(env, s->names[i], strlen(s->names[i]), v);
            } else {
                env_bind(env, s->name, strlen(s->name), v);
            }
            return cv_void();
        }
        case ST_RETURN: {
            ComptimeValue v = s->expr ? eval_expr_env(ctx, env, s->expr) : cv_void();
            if (v.kind == CV_ERROR) return v;
            ctx->returning = 1;
            ctx->return_value = v;
            return v;
        }
        case ST_EXPR:
            return eval_expr_env(ctx, env, s->expr);
        case ST_BREAK: case ST_CONTINUE:
            return cv_error(ctx, "comptime: break/continue are not yet supported in constant expressions");
        default:
            return cv_void();
    }
}

static ComptimeValue eval_block_stmts(ComptimeEvalCtx *ctx, Env *env, Expr *block) {
    ComptimeValue last = cv_void();
    for (int i = 0; i < block->stmt_count; i++) {
        last = eval_stmt(ctx, env, block->stmts[i]);
        if (last.kind == CV_ERROR) return last;
        if (ctx->returning) return ctx->return_value; /* 以降の文は実行せずここで打ち切る */
    }
    return last;
}

ComptimeValue comptime_eval_expr(ComptimeEvalCtx *ctx, Expr *e) {
    return eval_expr_env(ctx, NULL, e);
}

ComptimeValue comptime_call_fn(ComptimeEvalCtx *ctx, Item *fn, ComptimeValue *args, int arg_count) {
    if (ctx->call_depth >= COMPTIME_MAX_CALL_DEPTH) return cv_error(ctx, "comptime: call depth exceeded %d (possible infinite recursion)", COMPTIME_MAX_CALL_DEPTH);
    if (arg_count != fn->param_count) return cv_error(ctx, "comptime: '%s' expects %d argument(s), got %d", fn->name, fn->param_count, arg_count);
    Env *env = env_push(NULL);
    for (int i = 0; i < fn->param_count; i++) env_bind(env, fn->params[i].name, strlen(fn->params[i].name), args[i]);

    /* 呼び出しごとにreturningフラグを退避する: この関数内でreturnが起きても、
       それはこの呼び出しの境界で「捕捉」され、呼び出し元(例えばfib(n-1)を
       評価した外側のEX_BINARY)には伝播させない。 */
    int saved_returning = ctx->returning;
    ComptimeValue saved_return_value = ctx->return_value;
    ctx->returning = 0;

    ctx->call_depth++;
    ComptimeValue result = eval_block_stmts(ctx, env, fn->body);
    ctx->call_depth--;

    ctx->returning = saved_returning;
    ctx->return_value = saved_return_value;
    return result;
}

void comptime_format_value(ComptimeValue v, char *buf, size_t bufsize) {
    switch (v.kind) {
        case CV_INT: snprintf(buf, bufsize, "%lld", v.as.i); return;
        case CV_FLOAT: snprintf(buf, bufsize, "%g", v.as.f); return;
        case CV_BOOL: snprintf(buf, bufsize, "%s", v.as.b ? "true" : "false"); return;
        case CV_VOID: snprintf(buf, bufsize, "void"); return;
        case CV_ERROR: snprintf(buf, bufsize, "<error: %s>", v.error_msg); return;
    }
}
