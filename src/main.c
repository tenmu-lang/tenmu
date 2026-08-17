/* main.c — tmc0 CLIエントリポイント。
   Stage 1(フロントエンド)時点では「構文的に正しいTenmuソースかどうか」を
   検査し、トップレベル宣言の一覧を報告するところまでを担う。
   意味解析・コード生成はStage 2以降で追加する。 */
#include "parser.h"
#include "checker.h"
#include "irgen.h"
#include "codegen.h"
#include "elf.h"
#include "pe.h"
#include "llvmgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

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
        "usage: %s <command> <file.tm> [options]\n"
        "commands:\n"
        "  check   parse + name/type-check the file, report errors (default)\n"
        "  items   parse the file and list its top-level items\n"
        "  tokens  dump the raw token stream\n"
        "  build   compile to a native executable\n"
        "build options:\n"
        "  -o <path>          output path (default a.out / a.exe)\n"
        "  --backend=llvm|direct   codegen backend (default: llvm; direct = self-written x86-64/ELF, Linux only)\n"
        "  --target=<triple>       LLVM target triple (default: x86_64-pc-linux-gnu; use x86_64-pc-windows-gnu for Windows)\n"
        "note: only supports functions whose params/return are scalar integer\n"
        "      or bool types (no structs/strings/generics yet) — see README.\n",
        prog);
}

static int run_cmd(const char *fmt, ...) {
    char cmd[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    return system(cmd);
}

static const char *LINUX_START_ASM =
    "    .global _start\n"
    "    .text\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    and $-16, %rsp\n"
    "    call main\n"
    "    mov %eax, %edi\n"
    "    mov $60, %eax\n"
    "    syscall\n"
    "    .section .note.GNU-stack,\"\",@progbits\n";

static int is_windows_triple(const char *triple) { return strstr(triple, "windows") != NULL; }

static int cmd_build(const char *path, const char *out_path, const char *backend, const char *target_triple) {
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

    if (strcmp(backend, "direct") == 0) {
        int windows = is_windows_triple(target_triple);
        CodegenResult cg = codegen_program(lr.ir, windows ? CG_TARGET_WINDOWS : CG_TARGET_LINUX);
        size_t main_offset = 0;
        for (int i = 0; i < cg.func_offset_count; i++)
            if (strcmp(cg.func_offsets[i].name, "main") == 0) { main_offset = cg.func_offsets[i].offset; break; }
        int wr = windows
            ? pe_write_executable(out_path, cg.code.data, cg.code.len, main_offset)
            : elf_write_executable(out_path, cg.code.data, cg.code.len, main_offset);
        if (wr != 0) {
            fprintf(stderr, "tmc0: failed to write '%s'\n", out_path);
            return 1;
        }
        fprintf(stderr, "tmc0: [direct backend, %s] wrote %s (%zu bytes of code, %d function(s) compiled)\n",
                windows ? "Windows/PE" : "Linux/ELF", out_path, cg.code.len, cg.func_offset_count);
        return 0;
    }

    /* backend == "llvm" (既定): TIR -> LLVM IR -> オブジェクトファイル -> リンク */
    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path), "%s.o", out_path);
    LlvmGenResult gr = llvmgen_emit_object(lr.ir, target_triple, obj_path);
    if (!gr.ok) {
        fprintf(stderr, "tmc0: LLVM codegen failed: %s\n", gr.error_msg);
        return 1;
    }

    int link_status;
    if (is_windows_triple(target_triple)) {
        /* mingw-w64の標準CRTスタートアップを使う(main()を正しく呼び出し、
           戻り値でExitProcessする一式が揃っている)。 */
        link_status = run_cmd("x86_64-w64-mingw32-gcc -o %s %s 2>&1", out_path, obj_path);
    } else {
        /* 自前の最小_startスタブをアセンブルしてリンクする(libc全体は使わず、
           mainを正しい関数呼び出しとして起動しexitするためだけの最小構成)。 */
        char asm_path[512], start_obj[512];
        snprintf(asm_path, sizeof(asm_path), "%s.start.s", out_path);
        snprintf(start_obj, sizeof(start_obj), "%s.start.o", out_path);
        FILE *sf = fopen(asm_path, "w");
        if (sf) { fputs(LINUX_START_ASM, sf); fclose(sf); }
        run_cmd("as --64 -o %s %s", start_obj, asm_path);
        link_status = run_cmd("ld -o %s %s %s -e _start --static 2>&1", out_path, start_obj, obj_path);
        remove(asm_path);
        remove(start_obj);
    }
    if (link_status != 0) {
        fprintf(stderr, "tmc0: link step failed (object file kept at %s)\n", obj_path);
        return 1;
    }
    fprintf(stderr, "tmc0: [llvm backend, target=%s] wrote %s\n", target_triple, out_path);
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
        const char *out_path = NULL;
        const char *backend = "llvm";
        const char *target = "x86_64-pc-linux-gnu";
        for (int i = arg_start; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out_path = argv[++i]; }
            else if (strncmp(argv[i], "--backend=", 10) == 0) { backend = argv[i] + 10; }
            else if (strncmp(argv[i], "--target=", 9) == 0) { target = argv[i] + 9; }
        }
        if (!out_path) out_path = is_windows_triple(target) ? "a.exe" : "a.out";
        return cmd_build(path, out_path, backend, target);
    }
    return cmd_check_or_items(path, 0);
}
