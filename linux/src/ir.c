/* ir.c */
#include "ir.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

IrProgram *ir_program_new(void) {
    IrProgram *p = xmalloc(sizeof(IrProgram));
    p->funcs = NULL; p->func_count = 0; p->func_cap = 0;
    return p;
}

IrFunc *ir_func_new(IrProgram *prog, const char *name, int param_count) {
    IrFunc *f = xmalloc(sizeof(IrFunc));
    f->name = xstrndup(name, strlen(name));
    f->param_count = param_count;
    f->vreg_count = 0;
    f->label_count = 0;
    f->instrs = NULL; f->instr_count = 0; f->instr_cap = 0;
    if (prog->func_count >= prog->func_cap) {
        int nc = prog->func_cap == 0 ? 8 : prog->func_cap * 2;
        prog->funcs = xrealloc(prog->funcs, (size_t)nc * sizeof(IrFunc *));
        prog->func_cap = nc;
    }
    prog->funcs[prog->func_count++] = f;
    return f;
}

int ir_find_func(IrProgram *prog, const char *name) {
    for (int i = 0; i < prog->func_count; i++)
        if (strcmp(prog->funcs[i]->name, name) == 0) return i;
    return -1;
}

int ir_new_vreg(IrFunc *f) { return f->vreg_count++; }
int ir_new_label(IrFunc *f) { return f->label_count++; }

void ir_emit(IrFunc *f, IrInstr instr) {
    if (f->instr_count >= f->instr_cap) {
        int nc = f->instr_cap == 0 ? 32 : f->instr_cap * 2;
        f->instrs = xrealloc(f->instrs, (size_t)nc * sizeof(IrInstr));
        f->instr_cap = nc;
    }
    f->instrs[f->instr_count++] = instr;
}

static const char *op_name(IrOp op) {
    switch (op) {
        case IR_CONST: return "const"; case IR_MOVE: return "move";
        case IR_ADD: return "add"; case IR_SUB: return "sub"; case IR_MUL: return "mul";
        case IR_SDIV: return "sdiv"; case IR_SMOD: return "smod";
        case IR_AND: return "and"; case IR_OR: return "or"; case IR_XOR: return "xor";
        case IR_SHL: return "shl"; case IR_SAR: return "sar";
        case IR_NEG: return "neg"; case IR_NOT: return "not";
        case IR_CMP_EQ: return "eq"; case IR_CMP_NE: return "ne"; case IR_CMP_LT: return "lt";
        case IR_CMP_LE: return "le"; case IR_CMP_GT: return "gt"; case IR_CMP_GE: return "ge";
        case IR_PARAM: return "param"; case IR_CALL: return "call"; case IR_RET: return "ret";
        case IR_LABEL: return "label"; case IR_JUMP: return "jump"; case IR_JUMP_IF_ZERO: return "jz";
        default: return "?";
    }
}

void ir_print(IrProgram *prog) {
    for (int fi = 0; fi < prog->func_count; fi++) {
        IrFunc *f = prog->funcs[fi];
        printf("func %s(%d params, %d vregs):\n", f->name, f->param_count, f->vreg_count);
        for (int i = 0; i < f->instr_count; i++) {
            IrInstr *ins = &f->instrs[i];
            printf("  v%d = %s", ins->dst, op_name(ins->op));
            if (ins->op == IR_CALL) {
                printf(" %s(", prog->funcs[ins->call_func]->name);
                for (int j = 0; j < ins->arg_count; j++) printf("%sv%d", j ? "," : "", ins->args[j]);
                printf(")\n");
                continue;
            }
            if (ins->a >= 0) printf(" v%d", ins->a);
            if (ins->b >= 0) printf(", v%d", ins->b);
            if (ins->op == IR_CONST || ins->op == IR_PARAM || ins->op == IR_LABEL ||
                ins->op == IR_JUMP || ins->op == IR_JUMP_IF_ZERO) printf(" #%lld", ins->imm);
            printf("\n");
        }
    }
}
