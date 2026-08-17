/* test_parser.c — パーサーの結合テスト。ASTを再帰的にダンプし、
   パースエラーがあれば行:列とメッセージを表示して非0で終了する。 */
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static void indent(int n) { for (int i = 0; i < n; i++) printf("  "); }

static const char *expr_kind_name(ExprKind k) {
    switch (k) {
        case EX_INT: return "Int"; case EX_FLOAT: return "Float"; case EX_STRING: return "String";
        case EX_STRING_INTERP: return "StringInterp"; case EX_CHAR: return "Char";
        case EX_TRUE: return "True"; case EX_FALSE: return "False"; case EX_NULL: return "Null"; case EX_VOID: return "Void";
        case EX_IDENT: return "Ident"; case EX_SELF: return "Self";
        case EX_BINARY: return "Binary"; case EX_UNARY: return "Unary"; case EX_ASSIGN: return "Assign"; case EX_CAST: return "Cast";
        case EX_CALL: return "Call"; case EX_INDEX: return "Index"; case EX_FIELD: return "Field"; case EX_TRY_POSTFIX: return "TryPostfix";
        case EX_PAREN: return "Paren"; case EX_BLOCK: return "Block"; case EX_IF: return "If"; case EX_MATCH: return "Match";
        case EX_CLOSURE: return "Closure"; case EX_UNSAFE: return "Unsafe"; case EX_TRY_CATCH: return "TryCatch";
        case EX_STRUCT_LITERAL: return "StructLiteral"; case EX_ARRAY_LITERAL: return "ArrayLiteral"; case EX_TUPLE_LITERAL: return "TupleLiteral";
        case EX_FOR: return "For"; case EX_WHILE: return "While"; case EX_LOOP: return "Loop";
        case EX_NAMED_ARG: return "NamedArg";
        default: return "?Expr";
    }
}

static void print_type(Type *t, int d) {
    if (!t) { indent(d); printf("<null type>\n"); return; }
    indent(d);
    switch (t->kind) {
        case TY_PATH: {
            printf("Type.Path ");
            for (int i = 0; i < t->segment_count; i++) printf("%s%s", i ? "." : "", t->segments[i]);
            if (t->generic_arg_count) printf("<%d generic args>", t->generic_arg_count);
            printf("\n");
            for (int i = 0; i < t->generic_arg_count; i++) print_type(t->generic_args[i], d + 1);
            break;
        }
        case TY_PTR: printf("Type.Ptr mut=%d\n", t->is_mut); print_type(t->inner, d + 1); break;
        case TY_REF: printf("Type.Ref mut=%d\n", t->is_mut); print_type(t->inner, d + 1); break;
        case TY_ARRAY: printf("Type.Array (size_expr=%s)\n", t->array_size ? "yes" : "slice"); print_type(t->inner, d + 1); break;
        case TY_TUPLE: printf("Type.Tuple (%d items)\n", t->item_count); for (int i = 0; i < t->item_count; i++) print_type(t->items[i], d + 1); break;
        case TY_TENSOR: printf("Type.Tensor (%d dims)\n", t->dim_count); print_type(t->inner, d + 1); break;
        case TY_OPTIONAL: printf("Type.Optional\n"); print_type(t->inner, d + 1); break;
        case TY_NEVER: printf("Type.Never\n"); break;
        case TY_FN: printf("Type.Fn (%d params)\n", t->item_count); for (int i = 0; i < t->item_count; i++) print_type(t->items[i], d + 1); if (t->ret) print_type(t->ret, d + 1); break;
        default: printf("Type.?\n");
    }
}

static void print_stmt(Stmt *s, int d);

static void print_expr(Expr *e, int d) {
    if (!e) { indent(d); printf("<null expr>\n"); return; }
    indent(d);
    printf("%s", expr_kind_name(e->kind));
    switch (e->kind) {
        case EX_INT: printf(" %lld\n", e->int_val); return;
        case EX_FLOAT: printf(" %g\n", e->float_val); return;
        case EX_CHAR: printf(" cp=%lld\n", e->int_val); return;
        case EX_STRING: printf(" \"%.*s\"\n", (int)e->str_len, e->str); return;
        case EX_IDENT: printf(" %.*s\n", (int)e->str_len, e->str); return;
        case EX_BINARY: printf(" %.*s\n", (int)e->str_len, e->str); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_UNARY: printf(" %.*s\n", (int)e->str_len, e->str); print_expr(e->a, d + 1); return;
        case EX_ASSIGN: printf(" %.*s\n", (int)e->str_len, e->str); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_NAMED_ARG: printf(" %.*s:\n", (int)e->str_len, e->str); print_expr(e->b, d + 1); return;
        case EX_CAST: printf("\n"); print_expr(e->a, d + 1); print_type(e->type, d + 1); return;
        case EX_CALL: printf(" (%d args)\n", e->list_count); print_expr(e->a, d + 1); for (int i = 0; i < e->list_count; i++) print_expr(e->list[i], d + 1); return;
        case EX_INDEX: printf("\n"); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_FIELD: printf(" .%.*s%s\n", (int)e->str_len, e->str, e->list ? "(...)" : ""); print_expr(e->a, d + 1); for (int i = 0; i < e->list_count; i++) print_expr(e->list[i], d + 1); return;
        case EX_TRY_POSTFIX: printf("\n"); print_expr(e->a, d + 1); return;
        case EX_PAREN: printf("\n"); print_expr(e->a, d + 1); return;
        case EX_BLOCK: printf(" (%d stmts)\n", e->stmt_count); for (int i = 0; i < e->stmt_count; i++) print_stmt(e->stmts[i], d + 1); return;
        case EX_IF: printf("\n"); print_expr(e->a, d + 1); print_expr(e->b, d + 1); if (e->c) print_expr(e->c, d + 1); return;
        case EX_FOR: printf(" %.*s in\n", (int)e->str_len, e->str); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_WHILE: printf("\n"); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_LOOP: printf("\n"); print_expr(e->a, d + 1); return;
        case EX_MATCH: printf(" (%d arms)\n", e->arm_count); print_expr(e->a, d + 1);
            for (int i = 0; i < e->arm_count; i++) { indent(d + 1); printf("Arm:\n"); print_expr(e->arms[i].pattern, d + 2); print_expr(e->arms[i].body, d + 2); }
            return;
        case EX_CLOSURE: printf(" (%d params)\n", e->param_count);
            for (int i = 0; i < e->param_count; i++) { indent(d + 1); printf("param %s\n", e->params[i].name); }
            print_expr(e->a, d + 1); return;
        case EX_UNSAFE: printf("\n"); print_expr(e->a, d + 1); return;
        case EX_TRY_CATCH: printf(" catch(%.*s)\n", (int)e->str_len, e->str); print_expr(e->a, d + 1); print_expr(e->b, d + 1); return;
        case EX_STRUCT_LITERAL: printf(" %.*s (%d fields)\n", (int)e->str_len, e->str, e->field_init_count);
            for (int i = 0; i < e->field_init_count; i++) { indent(d + 1); printf("%s:\n", e->field_inits[i].name); print_expr(e->field_inits[i].value, d + 2); }
            return;
        case EX_ARRAY_LITERAL: case EX_TUPLE_LITERAL: printf(" (%d elems)\n", e->list_count); for (int i = 0; i < e->list_count; i++) print_expr(e->list[i], d + 1); return;
        case EX_STRING_INTERP: printf(" (%d parts)\n", e->interp_part_count);
            for (int i = 0; i < e->interp_part_count; i++) {
                if (e->interp_parts[i].expr) print_expr(e->interp_parts[i].expr, d + 1);
                else { indent(d + 1); printf("lit \"%.*s\"\n", (int)e->interp_parts[i].text_len, e->interp_parts[i].text); }
            }
            return;
        default: printf("\n"); return;
    }
}

static void print_item(Item *it, int d);

static void print_stmt(Stmt *s, int d) {
    if (!s) { indent(d); printf("<null stmt>\n"); return; }
    indent(d);
    switch (s->kind) {
        case ST_LET:
            if (s->names) {
                printf("Let (");
                for (int i = 0; i < s->name_count; i++) printf("%s%s", i ? ", " : "", s->names[i]);
                printf(") mut=%d\n", s->is_mut);
            } else {
                printf("Let %s mut=%d%s\n", s->name, s->is_mut, s->type_ann ? " (typed)" : "");
                if (s->type_ann) print_type(s->type_ann, d + 1);
            }
            print_expr(s->expr, d + 1); return;
        case ST_EXPR: printf("ExprStmt\n"); print_expr(s->expr, d + 1); return;
        case ST_RETURN: printf("Return\n"); if (s->expr) print_expr(s->expr, d + 1); return;
        case ST_BREAK: printf("Break\n"); if (s->expr) print_expr(s->expr, d + 1); return;
        case ST_CONTINUE: printf("Continue\n"); return;
        case ST_DEFER: printf("Defer\n"); print_expr(s->expr, d + 1); return;
        case ST_ITEM: printf("ItemStmt\n"); print_item(s->item, d + 1); return;
        default: printf("?Stmt\n"); return;
    }
}

static const char *item_kind_name(ItemKind k) {
    switch (k) {
        case IT_MODULE: return "Module"; case IT_IMPORT: return "Import"; case IT_FN: return "Fn";
        case IT_STRUCT: return "Struct"; case IT_ENUM: return "Enum"; case IT_UNION: return "Union";
        case IT_TRAIT: return "Trait"; case IT_IMPL: return "Impl"; case IT_ERROR: return "Error"; case IT_CONST: return "Const";
        default: return "?Item";
    }
}

static void print_item(Item *it, int d) {
    indent(d);
    printf("%s %s%s (attrs=%d, pub=%d)\n", item_kind_name(it->kind), it->name ? it->name : "", it->kind == IT_IMPL ? "<impl>" : "", it->attr_count, it->is_pub);
    for (int i = 0; i < it->attr_count; i++) {
        indent(d + 1);
        printf("#[%s", it->attrs[i].name);
        for (int j = 0; j < it->attrs[i].arg_count; j++) printf("%s%s", j ? "," : "(", it->attrs[i].args[j]);
        printf("%s]\n", it->attrs[i].arg_count ? ")" : "");
    }
    if (it->kind == IT_MODULE || it->kind == IT_IMPORT) {
        indent(d + 1); printf("path: ");
        for (int i = 0; i < it->path_segment_count; i++) printf("%s%s", i ? "." : "", it->path_segments[i]);
        if (it->alias) printf(" as %s", it->alias);
        printf("\n");
    }
    if (it->kind == IT_FN) {
        for (int i = 0; i < it->param_count; i++) {
            indent(d + 1); printf("param %s\n", it->params[i].name);
            if (it->params[i].type) print_type(it->params[i].type, d + 2);
        }
        if (it->return_type) { indent(d + 1); printf("returns:\n"); print_type(it->return_type, d + 2); }
        if (it->body) { indent(d + 1); printf("body:\n"); print_expr(it->body, d + 2); }
        else { indent(d + 1); printf("(no body / prototype)\n"); }
    }
    if (it->kind == IT_STRUCT || it->kind == IT_UNION) {
        for (int i = 0; i < it->field_count; i++) {
            indent(d + 1); printf("field %s\n", it->fields[i].name);
            print_type(it->fields[i].type, d + 2);
        }
    }
    if (it->kind == IT_ENUM || it->kind == IT_ERROR) {
        for (int i = 0; i < it->variant_count; i++) {
            indent(d + 1); printf("variant %s (tuple=%d, struct_fields=%d)\n",
                   it->variants[i].name, it->variants[i].tuple_type_count, it->variants[i].struct_field_count);
        }
    }
    if (it->kind == IT_TRAIT || it->kind == IT_IMPL) {
        if (it->impl_type) { indent(d + 1); printf("impl_type:\n"); print_type(it->impl_type, d + 2); }
        if (it->impl_trait_type) { indent(d + 1); printf("for_trait:\n"); print_type(it->impl_trait_type, d + 2); }
        for (int i = 0; i < it->method_count; i++) print_item(it->methods[i], d + 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file.tm> [--quiet]\n", argv[0]); return 1; }
    int quiet = (argc >= 3 && strcmp(argv[2], "--quiet") == 0);
    size_t len;
    char *src = read_file(argv[1], &len);

    Parser p;
    parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);

    if (p.had_error) {
        fprintf(stderr, "PARSE ERROR in %s at %d:%d: %s\n", argv[1], p.error_line, p.error_col, p.error_msg);
        return 1;
    }

    if (!quiet) {
        printf("=== %s: %d top-level items ===\n", argv[1], prog->item_count);
        for (int i = 0; i < prog->item_count; i++) print_item(prog->items[i], 0);
    }
    fprintf(stderr, "OK: parsed %s successfully (%d top-level items, %d parse errors)\n", argv[1], prog->item_count, p.error_count);
    return 0;
}
