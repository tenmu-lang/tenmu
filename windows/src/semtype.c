/* semtype.c */
#include "semtype.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

SemType *semtype_new(SemTypeKind kind) {
    SemType *t = xmalloc(sizeof(SemType));
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    return t;
}

/* プリミティブ型はプロセス内で使い回す(比較や生成コストを減らすため)。 */
static SemType *g_prims[ST_STRING + 1];

SemType *semtype_primitive(SemTypeKind kind) {
    if (kind <= ST_STRING && !g_prims[kind]) g_prims[kind] = semtype_new(kind);
    if (kind <= ST_STRING) return g_prims[kind];
    return semtype_new(kind);
}

SemType *semtype_unknown(void) {
    static SemType *u = NULL;
    if (!u) u = semtype_new(ST_UNKNOWN);
    return u;
}

SemType *semtype_external(void) {
    static SemType *e = NULL;
    if (!e) e = semtype_new(ST_EXTERNAL);
    return e;
}

static int is_int_kind(SemTypeKind k) {
    return k >= ST_I8 && k <= ST_USIZE;
}
static int is_float_kind(SemTypeKind k) {
    return k >= ST_F16 && k <= ST_F64;
}

int semtype_compatible(SemType *a, SemType *b) {
    if (!a || !b) return 1;
    if (a->kind == ST_UNKNOWN || b->kind == ST_UNKNOWN) return 1;
    if (a->kind == ST_EXTERNAL || b->kind == ST_EXTERNAL) return 1;
    if (a->kind == ST_NEVER || b->kind == ST_NEVER) return 1; /* !はどの型とも単一化できる */
    if (a->kind != b->kind) {
        /* 整数リテラル同士・浮動小数点リテラル同士は既定でi32/f64扱いだが、
           異なる幅指定間は不一致として検出する。ここでは種別(整数/浮動小数点)の
           大枠が一致していれば許容し、細かな幅チェックは将来のリテラル型推論の
           精緻化に譲る(誤検出でユーザーの正しいコードを止めない方を優先)。 */
        if (is_int_kind(a->kind) && is_int_kind(b->kind)) return 1;
        if (is_float_kind(a->kind) && is_float_kind(b->kind)) return 1;
        return 0;
    }
    switch (a->kind) {
        case ST_PTR: case ST_REF: case ST_ARRAY: case ST_SLICE: case ST_OPTIONAL:
            return semtype_compatible(a->inner, b->inner);
        case ST_STRUCT: case ST_ENUM: case ST_ERROR: case ST_UNION: case ST_TRAIT:
            return a->name && b->name && strcmp(a->name, b->name) == 0;
        case ST_GENERIC_PARAM:
            return a->name && b->name && strcmp(a->name, b->name) == 0;
        default:
            return 1;
    }
}

static const char *prim_name(SemTypeKind k) {
    switch (k) {
        case ST_I8: return "i8"; case ST_I16: return "i16"; case ST_I32: return "i32";
        case ST_I64: return "i64"; case ST_I128: return "i128"; case ST_ISIZE: return "isize";
        case ST_U8: return "u8"; case ST_U16: return "u16"; case ST_U32: return "u32";
        case ST_U64: return "u64"; case ST_U128: return "u128"; case ST_USIZE: return "usize";
        case ST_F16: return "f16"; case ST_F32: return "f32"; case ST_F64: return "f64";
        case ST_BOOL: return "bool"; case ST_CHAR: return "char"; case ST_VOID: return "void";
        case ST_NEVER: return "!"; case ST_STR: return "str"; case ST_STRING: return "String";
        default: return "?";
    }
}

void semtype_format(SemType *t, char *buf, size_t bufsize) {
    if (!t) { snprintf(buf, bufsize, "<null>"); return; }
    switch (t->kind) {
        case ST_UNKNOWN: snprintf(buf, bufsize, "<unknown>"); return;
        case ST_EXTERNAL: snprintf(buf, bufsize, "<external>"); return;
        case ST_STRUCT: snprintf(buf, bufsize, "struct %s", t->name ? t->name : "?"); return;
        case ST_ENUM: snprintf(buf, bufsize, "enum %s", t->name ? t->name : "?"); return;
        case ST_ERROR: snprintf(buf, bufsize, "error %s", t->name ? t->name : "?"); return;
        case ST_UNION: snprintf(buf, bufsize, "union %s", t->name ? t->name : "?"); return;
        case ST_TRAIT: snprintf(buf, bufsize, "trait %s", t->name ? t->name : "?"); return;
        case ST_GENERIC_PARAM: snprintf(buf, bufsize, "%s", t->name ? t->name : "?"); return;
        case ST_PTR: { char inner[128]; semtype_format(t->inner, inner, sizeof(inner)); snprintf(buf, bufsize, "*%s%s", t->is_mut ? "mut " : "", inner); return; }
        case ST_REF: { char inner[128]; semtype_format(t->inner, inner, sizeof(inner)); snprintf(buf, bufsize, "&%s%s", t->is_mut ? "mut " : "", inner); return; }
        case ST_OPTIONAL: { char inner[128]; semtype_format(t->inner, inner, sizeof(inner)); snprintf(buf, bufsize, "?%s", inner); return; }
        case ST_ARRAY: { char inner[128]; semtype_format(t->inner, inner, sizeof(inner)); snprintf(buf, bufsize, "[]%s", inner); return; }
        case ST_TENSOR: snprintf(buf, bufsize, "Tensor<...>"); return;
        case ST_TUPLE: snprintf(buf, bufsize, "(tuple, %d items)", t->item_count); return;
        case ST_FN: snprintf(buf, bufsize, "fn(...)"); return;
        default: snprintf(buf, bufsize, "%s", prim_name(t->kind)); return;
    }
}
