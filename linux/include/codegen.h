/* codegen.h — TIRからx86-64機械語(Linux: SysV AMD64 ABI / Windows: Microsoft x64 ABI)への変換。
   非最適化: 全ての仮想レジスタをスタックスロットに割り当て、各命令ごとに
   ロード/計算/ストアする単純な方式(正しさ優先、速度は最適化しない)。 */
#ifndef TMC_CODEGEN_H
#define TMC_CODEGEN_H

#include "ir.h"
#include <stddef.h>

typedef enum { CG_TARGET_LINUX, CG_TARGET_WINDOWS } CodegenTarget;

typedef struct {
    unsigned char *data;
    size_t len, cap;
} CodeBuf;

typedef struct {
    char *name;
    size_t offset; /* コードバッファ先頭からのバイトオフセット */
} FuncOffset;

typedef struct {
    CodeBuf code;
    FuncOffset *func_offsets; int func_offset_count;
    int entry_func_index; /* mainのIrProgram内インデックス。無ければ-1 */
} CodegenResult;

CodegenResult codegen_program(IrProgram *prog, CodegenTarget target);

#endif /* TMC_CODEGEN_H */
