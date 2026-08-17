/* ir.h — Tenmu IR (TIR): コード生成のための単純な三番地コード。
   Stage 3の最初の切り出しとして、i8..i64/u8..u64/usize/isize/bool/void の
   スカラ値のみを扱う(構造体・文字列・配列・参照・ジェネリクス・クロージャは対象外。
   README/コメントで明示する既知の制限)。 */
#ifndef TMC_IR_H
#define TMC_IR_H

typedef enum {
    IR_CONST,          /* dst = imm */
    IR_MOVE,           /* dst = a */
    IR_ADD, IR_SUB, IR_MUL, IR_SDIV, IR_SMOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SAR,
    IR_NEG, IR_NOT,
    IR_CMP_EQ, IR_CMP_NE, IR_CMP_LT, IR_CMP_LE, IR_CMP_GT, IR_CMP_GE,
    IR_PARAM,          /* dst = 第imm引数 */
    IR_CALL,           /* dst = call funcs[call_func](args...) */
    IR_RET,            /* return a (aが-1ならvoid) */
    IR_LABEL,          /* ラベルimm番の位置 */
    IR_JUMP,           /* ラベルimm番へ無条件ジャンプ */
    IR_JUMP_IF_ZERO,   /* aが0ならラベルimm番へジャンプ */
} IrOp;

typedef struct {
    IrOp op;
    int dst;            /* 結果を書く仮想レジスタ番号。使わない命令は-1 */
    int a, b;             /* オペランドの仮想レジスタ番号。未使用は-1 */
    long long imm;         /* 即値 / ラベル番号 / パラメータ番号 */
    int call_func;           /* IR_CALL: 呼び出し先関数のIrProgram内インデックス */
    int *args; int arg_count; /* IR_CALL: 引数の仮想レジスタ番号列 */
} IrInstr;

typedef struct {
    char *name;
    int param_count;
    int vreg_count;      /* このIR中で使われた仮想レジスタの総数 */
    int label_count;
    IrInstr *instrs; int instr_count; int instr_cap;
} IrFunc;

typedef struct {
    IrFunc **funcs; int func_count; int func_cap;
} IrProgram;

IrProgram *ir_program_new(void);
IrFunc    *ir_func_new(IrProgram *prog, const char *name, int param_count);
int        ir_find_func(IrProgram *prog, const char *name);

int  ir_new_vreg(IrFunc *f);
int  ir_new_label(IrFunc *f);
void ir_emit(IrFunc *f, IrInstr instr);

void ir_print(IrProgram *prog); /* デバッグ用ダンプ */

#endif /* TMC_IR_H */
