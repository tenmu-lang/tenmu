/* test_checker.c — 名前解決+基本型検査の結合テスト */
#include "checker.h"
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
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file.tm> [--expect-errors N]\n", argv[0]); return 1; }
    int expect_errors = -1; /* -1 = 「0件を期待」ではなく単に報告だけする */
    if (argc >= 4 && strcmp(argv[2], "--expect-errors") == 0) expect_errors = atoi(argv[3]);

    size_t len;
    char *src = read_file(argv[1], &len);

    Parser p;
    parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);
    if (p.had_error) {
        fprintf(stderr, "PARSE ERROR in %s at %d:%d: %s\n", argv[1], p.error_line, p.error_col, p.error_msg);
        return 1;
    }

    Checker ck;
    checker_init(&ck);
    check_program(&ck, prog);

    for (int i = 0; i < ck.error_count; i++) {
        fprintf(stderr, "%s:%d:%d: error: %s\n", argv[1], ck.errors[i].line, ck.errors[i].col, ck.errors[i].msg);
    }
    if (ck.total_error_count > ck.error_count) {
        fprintf(stderr, "%s: ... and %d more errors\n", argv[1], ck.total_error_count - ck.error_count);
    }

    if (expect_errors >= 0) {
        if (ck.total_error_count == expect_errors) {
            fprintf(stderr, "OK: %s — got expected %d error(s)\n", argv[1], expect_errors);
            return 0;
        }
        fprintf(stderr, "FAIL: %s — expected %d error(s), got %d\n", argv[1], expect_errors, ck.total_error_count);
        return 1;
    }

    if (ck.total_error_count == 0) {
        fprintf(stderr, "OK: %s — 0 errors\n", argv[1]);
        return 0;
    }
    fprintf(stderr, "%s: %d error(s)\n", argv[1], ck.total_error_count);
    return 1;
}
