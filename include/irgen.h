/* irgen.h — AST(型検査済み)からTIRへのロワリング。
   対応スコープ: パラメータ/戻り値がi8..i64/u8..u64/isize/usize/bool/voidのみの
   非ジェネリクス関数。算術/比較/論理演算、if/while(文としてのみ)、
   ローカル関数呼び出し、let/代入、return、break/continueに対応。
   構造体・文字列・配列・参照・ポインタ・ジェネリクス・closureを使う関数は
   対象外とし、明確な理由とともにスキップする(lower_programが報告する)。 */
#ifndef TMC_IRGEN_H
#define TMC_IRGEN_H

#include "ast.h"
#include "ir.h"

typedef struct {
    char *fn_name;
    char *reason;
} SkippedFn;

typedef struct {
    IrProgram *ir;
    SkippedFn *skipped; int skipped_count;
} LowerResult;

LowerResult lower_program(Program *prog);

#endif /* TMC_IRGEN_H */
