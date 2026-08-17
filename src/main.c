/* main.c — tmc0 CLIエントリポイント。
   Stage 1(フロントエンド)時点では「構文的に正しいTenmuソースかどうか」を
   検査し、トップレベル宣言の一覧を報告するところまでを担う。
   意味解析・コード生成はStage 2以降で追加する。 */
#include "parser.h"
#include "checker.h"
#include "irgen.h"
#include "codegen.h"
#include "elf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "tmc0: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

static const char *item_kind_str(ItemKind k) {
    switch (k) {
        case IT_MODULE: return "module"; case IT_IMPORT: return "import"; case IT_FN: return "fn";
        case IT_STRUCT: return "struct"; case IT_ENUM: return "enum"; case IT_UNION: return "union";
        case IT_TRAIT: return "trait"; case IT_IMPL: return "impl"; case IT_ERROR: return "error"; case IT_CONST: return "const";
        default: return "?";
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "tmc0 — Tenmu bootstrap compiler (Stage 1-3: frontend, semantic analysis, native codegen)\n"
        "usage: %s <command> <file.tm> [-o output]\n"
        "commands:\n"
        "  check   parse + name/type-check the file, report errors (default)\n"
        "  items   parse the file and list its top-level items\n"
        "  tokens  dump the raw token stream\n"
        "  build   compile to a native x86-64 Linux executable (-o to name it)\n"
        "note: build only supports functions whose params/return are scalar integer\n"
        "      or bool types (no structs/strings/generics yet) — see README.\n",
        prog);
}

static int cmd_build(const char *path, const char *out_path) {
    size_t len;
    char *src = read_file(path, &len);
    if (!src) return 1;

    Parser p;
    parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);
    if (p.had_error) {
        fprintf(stderr, "%s:%d:%d: error: %s\n", path, p.error_line, p.error_col, p.error_msg);
        return 1;
    }

    Checker ck;
    checker_init(&ck);
    check_program(&ck, prog);
    for (int i = 0; i < ck.error_count; i++)
        fprintf(stderr, "%s:%d:%d: error: %s\n", path, ck.errors[i].line, ck.errors[i].col, ck.errors[i].msg);
    if (ck.total_error_count > 0) {
        fprintf(stderr, "tmc0: %d error(s), not compiling\n", ck.total_error_count);
        return 1;
    }

    LowerResult lr = lower_program(prog);
    for (int i = 0; i < lr.skipped_count; i++)
        fprintf(stderr, "tmc0: note: '%s' not compiled to native code (%s)\n", lr.skipped[i].fn_name, lr.skipped[i].reason);

    int main_idx = ir_find_func(lr.ir, "main");
    if (main_idx < 0) {
        fprintf(stderr, "tmc0: no compilable 'main' function found (its signature may use unsupported types)\n");
        return 1;
    }

    CodegenResult cg = codegen_program(lr.ir);
    size_t main_offset = 0;
    for (int i = 0; i < cg.func_offset_count; i++)
        if (strcmp(cg.func_offsets[i].name, "main") == 0) { main_offset = cg.func_offsets[i].offset; break; }

    if (elf_write_executable(out_path, cg.code.data, cg.code.len, main_offset) != 0) {
        fprintf(stderr, "tmc0: failed to write '%s'\n", out_path);
        return 1;
    }
    fprintf(stderr, "tmc0: wrote %s (%zu bytes of code, %d function(s) compiled)\n",
            out_path, cg.code.len, cg.func_offset_count);
    return 0;
}

static int cmd_tokens(const char *path) {
    size_t len;
    char *src = read_file(path, &len);
    if (!src) return 1;
    Lexer lx;
    lexer_init(&lx, src, len);
    int count = 0;
    for (;;) {
        Token t = lexer_next(&lx);
        printf("%4d:%-3d %-18s '%.*s'\n", t.line, t.col, token_kind_name(t.kind), (int)t.len, t.start);
        count++;
        if (t.kind == TOK_EOF || t.kind == TOK_ILLEGAL) break;
        if (count > 1000000) { fprintf(stderr, "tmc0: token stream too long, aborting\n"); return 1; }
    }
    if (lx.had_error) {
        fprintf(stderr, "tmc0: lex error at %d:%d: %s\n", lx.error_line, lx.error_col, lx.error_msg);
        return 1;
    }
    return 0;
}

static int cmd_check_or_items(const char *path, int show_items) {
    size_t len;
    char *src = read_file(path, &len);
    if (!src) return 1;

    Parser p;
    parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);

    if (p.had_error) {
        fprintf(stderr, "%s:%d:%d: error: %s\n", path, p.error_line, p.error_col, p.error_msg);
        return 1;
    }

    if (show_items) {
        for (int i = 0; i < prog->item_count; i++) {
            Item *it = prog->items[i];
            printf("%s:%d: %s %s\n", path, it->line, item_kind_str(it->kind), it->name ? it->name : "");
        }
    }
    fprintf(stderr, "tmc0: %s — OK (%d top-level items)\n", path, prog->item_count);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    const char *cmd;
    const char *path;
    int arg_start;
    if (argc >= 3 && (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "items") == 0 ||
                      strcmp(argv[1], "tokens") == 0 || strcmp(argv[1], "build") == 0)) {
        cmd = argv[1];
        path = argv[2];
        arg_start = 3;
    } else {
        cmd = "check";
        path = argv[1];
        arg_start = 2;
    }

    if (strcmp(cmd, "tokens") == 0) return cmd_tokens(path);
    if (strcmp(cmd, "items") == 0) return cmd_check_or_items(path, 1);
    if (strcmp(cmd, "build") == 0) {
        const char *out_path = "a.out";
        for (int i = arg_start; i < argc - 1; i++) if (strcmp(argv[i], "-o") == 0) out_path = argv[i + 1];
        return cmd_build(path, out_path);
    }
    return cmd_check_or_items(path, 0);
}
