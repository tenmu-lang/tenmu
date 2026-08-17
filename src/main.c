/* main.c — tmc0 CLIエントリポイント。
   Stage 1(フロントエンド)時点では「構文的に正しいTenmuソースかどうか」を
   検査し、トップレベル宣言の一覧を報告するところまでを担う。
   意味解析・コード生成はStage 2以降で追加する。 */
#include "parser.h"
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
        "tmc0 — Tenmu bootstrap compiler (Stage 1: frontend only)\n"
        "usage: %s <command> <file.tm>\n"
        "commands:\n"
        "  check   parse the file and report syntax errors (default)\n"
        "  items   parse the file and list its top-level items\n"
        "  tokens  dump the raw token stream\n",
        prog);
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
    if (argc >= 3 && (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "items") == 0 || strcmp(argv[1], "tokens") == 0)) {
        cmd = argv[1];
        path = argv[2];
    } else {
        cmd = "check";
        path = argv[1];
    }

    if (strcmp(cmd, "tokens") == 0) return cmd_tokens(path);
    if (strcmp(cmd, "items") == 0) return cmd_check_or_items(path, 1);
    return cmd_check_or_items(path, 0);
}
