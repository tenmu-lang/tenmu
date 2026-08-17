/* llvmgen.h — TIRをLLVM IRへロワリングし、libLLVM(内蔵、動的リンク)経由で
   指定ターゲットのオブジェクトファイルを直接生成する。既存のirgen.cが作る
   IrProgramをそのまま入力に取るため、AST->IRの変換は自前バックエンドと共有する。
   ここが tenmu-spec.md §9 で言う「通常時はLLVM」の実体。 */
#ifndef TMC_LLVMGEN_H
#define TMC_LLVMGEN_H

#include "ir.h"

typedef struct {
    int ok;
    char error_msg[256];
} LlvmGenResult;

/* IrProgramをLLVM IRへ変換し、target_tripleに対するオブジェクトファイルを
   out_obj_path に書き出す(まだリンクしていない .o)。
   例: target_triple="x86_64-pc-linux-gnu" / "x86_64-pc-windows-gnu" */
LlvmGenResult llvmgen_emit_object(IrProgram *prog, const char *target_triple, const char *out_obj_path);

/* デバッグ/検証用: 生成したLLVM IRをテキスト(.ll)としてダンプする */
LlvmGenResult llvmgen_emit_ir_text(IrProgram *prog, const char *out_ll_path);

#endif /* TMC_LLVMGEN_H */
