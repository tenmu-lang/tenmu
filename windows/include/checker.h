/* checker.h — Stage 2: 名前解決 + 基本的な型検査
   スコープに無い外部シンボル(importしたモジュールのメンバー)は実体不明の
   ST_EXTERNAL として扱い、そこから先の型はUNKNOWN/EXTERNALとして伝播させ
   誤検出の連鎖を防ぐ。ローカルに宣言された関数/構造体/変数についてはきちんと
   名前解決・型検査を行う。所有権/借用検査・comptime評価は対象外(Stage 2の
   後続作業)。 */
#ifndef TMC_CHECKER_H
#define TMC_CHECKER_H

#include "ast.h"
#include "symtab.h"
#include "semtype.h"

#define CHECKER_MAX_ERRORS 128

typedef struct {
    int line, col;
    char msg[256];
} CheckerError;

typedef struct {
    Scope *global;
    int in_unsafe;
    SemType *current_fn_return;

    CheckerError errors[CHECKER_MAX_ERRORS];
    int error_count;      /* 記録した件数(上限あり) */
    int total_error_count; /* 上限を超えても実際に検出した総数 */
} Checker;

void checker_init(Checker *ck);
void check_program(Checker *ck, Program *prog);

#endif /* TMC_CHECKER_H */
