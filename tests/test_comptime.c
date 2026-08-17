/* test_comptime.c — const宣言をcomptime評価し、実際の計算結果を表示する。
   正しさの検証(例: square(8)==64, fib(10)==55)に使う。 */
#include "checker.h"
#include "comptime.h"
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
    if (argc < 2) { fprintf(stderr, "usage: %s <file.tm>\n", argv[0]); return 1; }
    size_t len;
    char *src = read_file(argv[1], &len);

    Parser p;
    parser_init(&p, src, len);
    Program *prog = parser_parse_program(&p);
    if (p.had_error) { fprintf(stderr, "PARSE ERROR: %s\n", p.error_msg); return 1; }

    Checker ck;
    checker_init(&ck);
    check_program(&ck, prog);
    for (int i = 0; i < ck.error_count; i++)
        fprintf(stderr, "checker error at %d:%d: %s\n", ck.errors[i].line, ck.errors[i].col, ck.errors[i].msg);

    ComptimeEvalCtx cctx;
    comptime_init(&cctx, ck.global);
    int any_error = ck.total_error_count > 0;

    for (int i = 0; i < prog->item_count; i++) {
        Item *it = prog->items[i];
        if (it->kind != IT_CONST) continue;
        ComptimeValue v = comptime_eval_expr(&cctx, it->const_value);
        char buf[128];
        comptime_format_value(v, buf, sizeof(buf));
        printf("const %s = %s\n", it->name, buf);
        if (v.kind == CV_ERROR) any_error = 1;
    }

    return any_error ? 1 : 0;
}
