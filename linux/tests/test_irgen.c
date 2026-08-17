/* test_irgen.c — AST->IR lowering の確認用ドライバ */
#include "irgen.h"
#include "checker.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0'; fclose(f);
    *out_len = n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file.tm>\n", argv[0]); return 1; }
    size_t len; char *src = read_file(argv[1], &len);
    Parser p; parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);
    if (p.had_error) { fprintf(stderr, "parse error: %s\n", p.error_msg); return 1; }
    Checker ck; checker_init(&ck); check_program(&ck, prog);
    for (int i = 0; i < ck.error_count; i++) fprintf(stderr, "check error: %s\n", ck.errors[i].msg);
    if (ck.total_error_count > 0) return 1;

    LowerResult r = lower_program(prog);
    for (int i = 0; i < r.skipped_count; i++)
        fprintf(stderr, "skipped '%s': %s\n", r.skipped[i].fn_name, r.skipped[i].reason);
    ir_print(r.ir);
    return 0;
}
