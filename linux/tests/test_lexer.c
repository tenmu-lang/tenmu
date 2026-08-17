/* test_lexer.c — レキサーの単体テストドライバ。全トークンをダンプする。 */
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file.tm>\n", argv[0]); return 1; }
    size_t len;
    char *src = read_file(argv[1], &len);

    Lexer lx;
    lexer_init(&lx, src, len);

    int count = 0;
    for (;;) {
        Token t = lexer_next(&lx);
        printf("%4d:%-3d %-18s '%.*s'", t.line, t.col, token_kind_name(t.kind), (int)t.len, t.start);
        if (t.kind == TOK_INT) printf("  (int=%lld)", t.value.int_val);
        if (t.kind == TOK_FLOAT) printf("  (float=%g)", t.value.float_val);
        if (t.kind == TOK_CHAR) printf("  (cp=%lld)", t.value.int_val);
        printf("\n");
        count++;
        if (t.kind == TOK_EOF) break;
        if (t.kind == TOK_ILLEGAL) { fprintf(stderr, "ILLEGAL token encountered\n"); break; }
        if (count > 100000) { fprintf(stderr, "runaway lexer (>100000 tokens), aborting\n"); return 1; }
    }

    if (lx.had_error) {
        fprintf(stderr, "LEXER ERROR at %d:%d: %s\n", lx.error_line, lx.error_col, lx.error_msg);
        return 1;
    }
    fprintf(stderr, "OK: %d tokens, no errors\n", count);
    return 0;
}
