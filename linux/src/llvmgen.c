/* llvmgen.c — TIR -> LLVM IR -> (libLLVM経由) オブジェクトファイル。
   全仮想レジスタをalloca(スタックスロット)にし、各命令をload/計算/storeで
   愚直に組み立てる(直接バックエンドと同じ簡潔さ優先の方針)。SSA化・最適化は
   LLVM自身のmem2reg等のパスに委ねる。 */
#include "llvmgen.h"
#include "util.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <string.h>
#include <stdio.h>

static LlvmGenResult ok_result(void) { LlvmGenResult r; r.ok = 1; r.error_msg[0] = '\0'; return r; }
static LlvmGenResult err_result(const char *msg) {
    LlvmGenResult r; r.ok = 0; snprintf(r.error_msg, sizeof(r.error_msg), "%s", msg); return r;
}

typedef struct {
    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef builder;
    LLVMTypeRef i64_ty;
    LLVMValueRef *fn_values;   /* IrProgram内インデックスに対応するLLVM関数値 */
    LLVMTypeRef  *fn_types;
} LGen;

static LLVMValueRef build_binop(LGen *g, IrOp op, LLVMValueRef a, LLVMValueRef b) {
    switch (op) {
        case IR_ADD: return LLVMBuildAdd(g->builder, a, b, "add");
        case IR_SUB: return LLVMBuildSub(g->builder, a, b, "sub");
        case IR_MUL: return LLVMBuildMul(g->builder, a, b, "mul");
        case IR_SDIV: return LLVMBuildSDiv(g->builder, a, b, "sdiv");
        case IR_SMOD: return LLVMBuildSRem(g->builder, a, b, "smod");
        case IR_AND: return LLVMBuildAnd(g->builder, a, b, "and");
        case IR_OR:  return LLVMBuildOr(g->builder, a, b, "or");
        case IR_XOR: return LLVMBuildXor(g->builder, a, b, "xor");
        case IR_SHL: return LLVMBuildShl(g->builder, a, b, "shl");
        case IR_SAR: return LLVMBuildAShr(g->builder, a, b, "sar");
        default: return NULL;
    }
}

static LLVMIntPredicate cmp_predicate(IrOp op) {
    switch (op) {
        case IR_CMP_EQ: return LLVMIntEQ;
        case IR_CMP_NE: return LLVMIntNE;
        case IR_CMP_LT: return LLVMIntSLT;
        case IR_CMP_LE: return LLVMIntSLE;
        case IR_CMP_GT: return LLVMIntSGT;
        default: return LLVMIntSGE;
    }
}

static int is_cmp_op(IrOp op) { return op >= IR_CMP_EQ && op <= IR_CMP_GE; }

static void build_function_body(LGen *g, IrProgram *prog, int fi) {
    IrFunc *f = prog->funcs[fi];
    LLVMValueRef fn = g->fn_values[fi];

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    int nslots = f->vreg_count > 0 ? f->vreg_count : 1;
    LLVMValueRef *slots = xmalloc(sizeof(LLVMValueRef) * (size_t)nslots);
    for (int i = 0; i < nslots; i++) {
        char name[32]; snprintf(name, sizeof(name), "v%d", i);
        slots[i] = LLVMBuildAlloca(g->builder, g->i64_ty, name);
    }

    /* ラベル -> 基本ブロック の対応表を事前に作る(前方ジャンプに対応するため) */
    int max_label = -1;
    for (int i = 0; i < f->instr_count; i++)
        if (f->instrs[i].op == IR_LABEL && (int)f->instrs[i].imm > max_label) max_label = (int)f->instrs[i].imm;
    LLVMBasicBlockRef *label_blocks = NULL;
    if (max_label >= 0) {
        label_blocks = xmalloc(sizeof(LLVMBasicBlockRef) * (size_t)(max_label + 1));
        for (int i = 0; i <= max_label; i++) {
            char name[32]; snprintf(name, sizeof(name), "L%d", i);
            label_blocks[i] = LLVMAppendBasicBlockInContext(g->ctx, fn, name);
        }
    }

    int terminated = 0; /* 現在位置しているブロックが既にret/brで終端済みか */

    for (int i = 0; i < f->instr_count; i++) {
        IrInstr *ins = &f->instrs[i];

        if (ins->op == IR_LABEL) {
            if (!terminated) LLVMBuildBr(g->builder, label_blocks[ins->imm]); /* フォールスルーを明示的な分岐にする */
            LLVMPositionBuilderAtEnd(g->builder, label_blocks[ins->imm]);
            terminated = 0;
            continue;
        }
        if (terminated) continue; /* 到達不能命令(ret/jump直後で次のlabelまでの間)は生成しない */

        switch (ins->op) {
            case IR_CONST:
                LLVMBuildStore(g->builder, LLVMConstInt(g->i64_ty, (unsigned long long)ins->imm, 1), slots[ins->dst]);
                break;
            case IR_MOVE:
                LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "ld"), slots[ins->dst]);
                break;
            case IR_PARAM:
                LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)ins->imm), slots[ins->dst]);
                break;
            case IR_NEG: {
                LLVMValueRef a = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "ld");
                LLVMBuildStore(g->builder, LLVMBuildNeg(g->builder, a, "neg"), slots[ins->dst]);
                break;
            }
            case IR_NOT: {
                LLVMValueRef a = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "ld");
                LLVMBuildStore(g->builder, LLVMBuildNot(g->builder, a, "not"), slots[ins->dst]);
                break;
            }
            case IR_CALL: {
                LLVMValueRef *args = ins->arg_count ? xmalloc(sizeof(LLVMValueRef) * (size_t)ins->arg_count) : NULL;
                for (int a = 0; a < ins->arg_count; a++)
                    args[a] = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->args[a]], "arg");
                LLVMValueRef callee = g->fn_values[ins->call_func];
                LLVMTypeRef callee_ty = g->fn_types[ins->call_func];
                LLVMValueRef result = LLVMBuildCall2(g->builder, callee_ty, callee, args, (unsigned)ins->arg_count, "calltmp");
                LLVMBuildStore(g->builder, result, slots[ins->dst]);
                break;
            }
            case IR_RET: {
                LLVMValueRef v = ins->a >= 0 ? LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "ld")
                                              : LLVMConstInt(g->i64_ty, 0, 0);
                LLVMBuildRet(g->builder, v);
                terminated = 1;
                break;
            }
            case IR_JUMP:
                LLVMBuildBr(g->builder, label_blocks[ins->imm]);
                terminated = 1;
                break;
            case IR_JUMP_IF_ZERO: {
                LLVMValueRef cond = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "cond");
                LLVMValueRef iszero = LLVMBuildICmp(g->builder, LLVMIntEQ, cond, LLVMConstInt(g->i64_ty, 0, 0), "iszero");
                LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, fn, "cont");
                LLVMBuildCondBr(g->builder, iszero, label_blocks[ins->imm], cont);
                LLVMPositionBuilderAtEnd(g->builder, cont);
                break; /* このIR自体は終端しない: contブロックへ移って後続命令を続ける */
            }
            default: {
                LLVMValueRef a = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->a], "a");
                LLVMValueRef b = LLVMBuildLoad2(g->builder, g->i64_ty, slots[ins->b], "b");
                if (is_cmp_op(ins->op)) {
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, cmp_predicate(ins->op), a, b, "cmp");
                    LLVMValueRef ext = LLVMBuildZExt(g->builder, cmp, g->i64_ty, "ext");
                    LLVMBuildStore(g->builder, ext, slots[ins->dst]);
                } else {
                    LLVMValueRef v = build_binop(g, ins->op, a, b);
                    LLVMBuildStore(g->builder, v, slots[ins->dst]);
                }
                break;
            }
        }
    }
    if (!terminated) LLVMBuildRet(g->builder, LLVMConstInt(g->i64_ty, 0, 0)); /* 安全策 */
}

static LGen build_module(IrProgram *prog) {
    LGen g;
    g.ctx = LLVMContextCreate();
    g.mod = LLVMModuleCreateWithNameInContext("tenmu_module", g.ctx);
    g.builder = LLVMCreateBuilderInContext(g.ctx);
    g.i64_ty = LLVMInt64TypeInContext(g.ctx);
    g.fn_values = xmalloc(sizeof(LLVMValueRef) * (size_t)(prog->func_count ? prog->func_count : 1));
    g.fn_types = xmalloc(sizeof(LLVMTypeRef) * (size_t)(prog->func_count ? prog->func_count : 1));

    for (int fi = 0; fi < prog->func_count; fi++) {
        IrFunc *f = prog->funcs[fi];
        int pc = f->param_count > 0 ? f->param_count : 0;
        LLVMTypeRef *param_types = pc ? xmalloc(sizeof(LLVMTypeRef) * (size_t)pc) : NULL;
        for (int i = 0; i < pc; i++) param_types[i] = g.i64_ty;
        LLVMTypeRef fn_ty = LLVMFunctionType(g.i64_ty, param_types, (unsigned)pc, 0);
        LLVMValueRef fn = LLVMAddFunction(g.mod, f->name, fn_ty);
        g.fn_values[fi] = fn;
        g.fn_types[fi] = fn_ty;
    }
    for (int fi = 0; fi < prog->func_count; fi++) build_function_body(&g, prog, fi);
    return g;
}

static void dispose(LGen *g) {
    LLVMDisposeBuilder(g->builder);
    LLVMDisposeModule(g->mod);
    LLVMContextDispose(g->ctx);
}

LlvmGenResult llvmgen_emit_ir_text(IrProgram *prog, const char *out_ll_path) {
    LGen g = build_module(prog);
    char *err = NULL;
    if (LLVMVerifyModule(g.mod, LLVMReturnStatusAction, &err)) {
        LlvmGenResult r = err_result(err ? err : "module verification failed");
        if (err) LLVMDisposeMessage(err);
        dispose(&g);
        return r;
    }
    if (err) LLVMDisposeMessage(err);
    char *ir = LLVMPrintModuleToString(g.mod);
    FILE *f = fopen(out_ll_path, "w");
    if (!f) { LLVMDisposeMessage(ir); dispose(&g); return err_result("cannot open output .ll path"); }
    fputs(ir, f);
    fclose(f);
    LLVMDisposeMessage(ir);
    dispose(&g);
    return ok_result();
}

LlvmGenResult llvmgen_emit_object(IrProgram *prog, const char *target_triple, const char *out_obj_path) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    LGen g = build_module(prog);

    char *verr = NULL;
    if (LLVMVerifyModule(g.mod, LLVMReturnStatusAction, &verr)) {
        LlvmGenResult r = err_result(verr ? verr : "module verification failed");
        if (verr) LLVMDisposeMessage(verr);
        dispose(&g);
        return r;
    }
    if (verr) LLVMDisposeMessage(verr);

    LLVMSetTarget(g.mod, target_triple);

    char *tgt_err = NULL;
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(target_triple, &target, &tgt_err)) {
        LlvmGenResult r = err_result(tgt_err ? tgt_err : "unknown target triple");
        if (tgt_err) LLVMDisposeMessage(tgt_err);
        dispose(&g);
        return r;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, target_triple, "generic", "",
        LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault);

    char *emit_err = NULL;
    char *path_copy = xstrndup(out_obj_path, strlen(out_obj_path));
    int fail = LLVMTargetMachineEmitToFile(tm, g.mod, path_copy, LLVMObjectFile, &emit_err);
    LlvmGenResult result = ok_result();
    if (fail) {
        result = err_result(emit_err ? emit_err : "failed to emit object file");
    }
    if (emit_err) LLVMDisposeMessage(emit_err);

    LLVMDisposeTargetMachine(tm);
    dispose(&g);
    return result;
}
