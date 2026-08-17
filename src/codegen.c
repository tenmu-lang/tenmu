/* codegen.c — TIR -> x86-64 (SysV AMD64, Linux) 機械語 */
#include "codegen.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

/* x86-64 GPR番号(標準エンコーディング) */
enum { RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8=8, R9=9 };

static const int ARG_REGS[6] = { RDI, RSI, RDX, RCX, R8, R9 };

/* ===== CodeBuf ===== */
static void buf_init(CodeBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void buf_u8(CodeBuf *b, unsigned char v) {
    if (b->len >= b->cap) { size_t nc = b->cap == 0 ? 256 : b->cap * 2; b->data = xrealloc(b->data, nc); b->cap = nc; }
    b->data[b->len++] = v;
}
static void buf_u32_at(CodeBuf *b, size_t off, unsigned int v) {
    b->data[off+0] = (unsigned char)(v & 0xFF);
    b->data[off+1] = (unsigned char)((v >> 8) & 0xFF);
    b->data[off+2] = (unsigned char)((v >> 16) & 0xFF);
    b->data[off+3] = (unsigned char)((v >> 24) & 0xFF);
}
static void buf_u32(CodeBuf *b, unsigned int v) { size_t at = b->len; buf_u8(b,0);buf_u8(b,0);buf_u8(b,0);buf_u8(b,0); buf_u32_at(b, at, v); }
static void buf_u64(CodeBuf *b, unsigned long long v) {
    for (int i = 0; i < 8; i++) buf_u8(b, (unsigned char)((v >> (8*i)) & 0xFF));
}

/* REX prefix: 0100WRXB */
static void emit_rex(CodeBuf *b, int w, int r, int x, int base_ext) {
    unsigned char rex = (unsigned char)(0x40 | (w?8:0) | (r?4:0) | (x?2:0) | (base_ext?1:0));
    buf_u8(b, rex);
}
static void modrm(CodeBuf *b, int mod, int reg, int rm) {
    buf_u8(b, (unsigned char)(((mod&3)<<6) | ((reg&7)<<3) | (rm&7)));
}

/* mov dst64, src64 (register to register) */
static void emit_mov_rr(CodeBuf *b, int dst, int src) {
    emit_rex(b, 1, src>=8, 0, dst>=8);
    buf_u8(b, 0x89); /* MOV r/m64, r64 */
    modrm(b, 3, src, dst);
}
/* mov dst64, imm64 */
static void emit_mov_ri64(CodeBuf *b, int dst, long long imm) {
    emit_rex(b, 1, 0, 0, dst>=8);
    buf_u8(b, (unsigned char)(0xB8 + (dst & 7)));
    buf_u64(b, (unsigned long long)imm);
}
/* mov dst64, [rbp - disp]  (slotは0始まりの仮想レジスタ番号) */
static void emit_load_slot(CodeBuf *b, int dst, int slot) {
    int disp = -(8 * (slot + 1));
    emit_rex(b, 1, dst>=8, 0, 0);
    buf_u8(b, 0x8B); /* MOV r64, r/m64 */
    modrm(b, 2, dst, 5 /* RBP, disp32 */);
    buf_u32(b, (unsigned int)disp);
}
/* mov [rbp - disp], src64 */
static void emit_store_slot(CodeBuf *b, int slot, int src) {
    int disp = -(8 * (slot + 1));
    emit_rex(b, 1, src>=8, 0, 0);
    buf_u8(b, 0x89); /* MOV r/m64, r64 */
    modrm(b, 2, src, 5);
    buf_u32(b, (unsigned int)disp);
}
static void emit_alu_rr(CodeBuf *b, unsigned char opcode, int dst, int src) {
    emit_rex(b, 1, dst>=8, 0, src>=8);
    buf_u8(b, opcode);
    modrm(b, 3, dst, src);
}
static void emit_add(CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x03, dst, src); }
static void emit_sub(CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x2B, dst, src); }
static void emit_and(CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x23, dst, src); }
static void emit_or (CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x0B, dst, src); }
static void emit_xor(CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x33, dst, src); }
static void emit_cmp(CodeBuf *b, int dst, int src) { emit_alu_rr(b, 0x3B, dst, src); }
static void emit_imul(CodeBuf *b, int dst, int src) {
    emit_rex(b, 1, dst>=8, 0, src>=8);
    buf_u8(b, 0x0F); buf_u8(b, 0xAF);
    modrm(b, 3, dst, src);
}
static void emit_neg(CodeBuf *b, int reg) { emit_rex(b,1,0,0,reg>=8); buf_u8(b,0xF7); modrm(b,3,3,reg); }
static void emit_not(CodeBuf *b, int reg) { emit_rex(b,1,0,0,reg>=8); buf_u8(b,0xF7); modrm(b,3,2,reg); }
static void emit_cqo(CodeBuf *b) { emit_rex(b,1,0,0,0); buf_u8(b, 0x99); }
static void emit_idiv(CodeBuf *b, int reg) { emit_rex(b,1,0,0,reg>=8); buf_u8(b,0xF7); modrm(b,3,7,reg); }
static void emit_shl_cl(CodeBuf *b, int reg) { emit_rex(b,1,0,0,reg>=8); buf_u8(b,0xD3); modrm(b,3,4,reg); }
static void emit_sar_cl(CodeBuf *b, int reg) { emit_rex(b,1,0,0,reg>=8); buf_u8(b,0xD3); modrm(b,3,7,reg); }
static void emit_setcc(CodeBuf *b, unsigned char cc) { buf_u8(b,0x0F); buf_u8(b, (unsigned char)(0x90|cc)); modrm(b,3,0,0); /* setcc al */ }
static void emit_movzx_al(CodeBuf *b, int dst) { emit_rex(b,1,dst>=8,0,0); buf_u8(b,0x0F); buf_u8(b,0xB6); modrm(b,3,dst,0); }
static void emit_push(CodeBuf *b, int reg) { if (reg>=8) emit_rex(b,0,0,0,1); buf_u8(b, (unsigned char)(0x50+(reg&7))); }
static void emit_pop(CodeBuf *b, int reg)  { if (reg>=8) emit_rex(b,0,0,0,1); buf_u8(b, (unsigned char)(0x58+(reg&7))); }
static void emit_sub_rsp(CodeBuf *b, unsigned int imm) { emit_rex(b,1,0,0,0); buf_u8(b,0x81); modrm(b,3,5,RSP); buf_u32(b, imm); }
static void emit_ret(CodeBuf *b) { buf_u8(b, 0xC3); }
static void emit_test_rax_rax(CodeBuf *b) { emit_rex(b,1,0,0,0); buf_u8(b,0x85); modrm(b,3,RAX,RAX); }

/* ===== フィックスアップ(ジャンプ/コールの相対オフセット後埋め) ===== */
typedef struct {
    size_t patch_offset; /* rel32を書き込むべきバッファ位置 */
    int is_call;           /* 1なら関数呼び出し、0ならラベルへのジャンプ */
    int target_func;        /* is_callの場合: IrProgram内の関数インデックス */
    int target_label;        /* !is_callの場合: 関数内ラベル番号 */
} Fixup;

typedef struct { int label; size_t offset; } LabelOffset;

static void emit_jmp_placeholder(CodeBuf *b, unsigned char opcode1, int opcode2,
                                  DynArray *fixups, int is_call, int target_func, int target_label) {
    buf_u8(b, opcode1);
    if (opcode2 >= 0) buf_u8(b, (unsigned char)opcode2);
    Fixup fx; fx.patch_offset = b->len; fx.is_call = is_call; fx.target_func = target_func; fx.target_label = target_label;
    dynarray_push(fixups, &fx);
    buf_u32(b, 0); /* プレースホルダ、後で解決する */
}

static int frame_size_for(int vreg_count) {
    int bytes = (vreg_count > 0 ? vreg_count : 1) * 8;
    return (bytes + 15) & ~15; /* 16バイト境界に切り上げ(呼び出し時のスタック整列のため) */
}

CodegenResult codegen_program(IrProgram *prog) {
    CodegenResult res; memset(&res, 0, sizeof(res));
    buf_init(&res.code);
    res.func_offsets = xmalloc(sizeof(FuncOffset) * (size_t)(prog->func_count ? prog->func_count : 1));
    res.func_offset_count = prog->func_count;
    res.entry_func_index = ir_find_func(prog, "main");

    DynArray fixups; dynarray_init(&fixups, sizeof(Fixup)); /* call先(関数間、全体解決) */

    for (int fi = 0; fi < prog->func_count; fi++) {
        IrFunc *f = prog->funcs[fi];
        res.func_offsets[fi].name = f->name;
        res.func_offsets[fi].offset = res.code.len;

        DynArray labels; dynarray_init(&labels, sizeof(LabelOffset));
        DynArray local_fixups; dynarray_init(&local_fixups, sizeof(Fixup)); /* ラベル(関数内で完結) */

        int frame = frame_size_for(f->vreg_count);

        emit_push(&res.code, RBP);
        emit_mov_rr(&res.code, RBP, RSP);
        emit_sub_rsp(&res.code, (unsigned int)frame);

        for (int i = 0; i < f->instr_count; i++) {
            IrInstr *ins = &f->instrs[i];
            switch (ins->op) {
                case IR_CONST:
                    emit_mov_ri64(&res.code, RAX, ins->imm);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_MOVE:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_PARAM:
                    if (ins->imm < 6) emit_mov_rr(&res.code, RAX, ARG_REGS[ins->imm]);
                    else emit_mov_ri64(&res.code, RAX, 0); /* 7個目以降の引数は本バックエンド未対応 */
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_ADD: case IR_SUB: case IR_AND: case IR_OR: case IR_XOR: {
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_load_slot(&res.code, RCX, ins->b);
                    if (ins->op == IR_ADD) emit_add(&res.code, RAX, RCX);
                    else if (ins->op == IR_SUB) emit_sub(&res.code, RAX, RCX);
                    else if (ins->op == IR_AND) emit_and(&res.code, RAX, RCX);
                    else if (ins->op == IR_OR)  emit_or(&res.code, RAX, RCX);
                    else emit_xor(&res.code, RAX, RCX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                }
                case IR_MUL:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_load_slot(&res.code, RCX, ins->b);
                    emit_imul(&res.code, RAX, RCX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_SDIV: case IR_SMOD:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_load_slot(&res.code, RCX, ins->b);
                    emit_cqo(&res.code);
                    emit_idiv(&res.code, RCX);
                    emit_store_slot(&res.code, ins->dst, ins->op == IR_SDIV ? RAX : RDX);
                    break;
                case IR_SHL: case IR_SAR:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_load_slot(&res.code, RCX, ins->b);
                    if (ins->op == IR_SHL) emit_shl_cl(&res.code, RAX); else emit_sar_cl(&res.code, RAX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_NEG:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_neg(&res.code, RAX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_NOT:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_not(&res.code, RAX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                case IR_CMP_EQ: case IR_CMP_NE: case IR_CMP_LT: case IR_CMP_LE: case IR_CMP_GT: case IR_CMP_GE: {
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_load_slot(&res.code, RCX, ins->b);
                    emit_cmp(&res.code, RAX, RCX);
                    unsigned char cc =
                        ins->op == IR_CMP_EQ ? 0x04 : ins->op == IR_CMP_NE ? 0x05 :
                        ins->op == IR_CMP_LT ? 0x0C : ins->op == IR_CMP_LE ? 0x0E :
                        ins->op == IR_CMP_GT ? 0x0F : 0x0D; /* GE */
                    emit_setcc(&res.code, cc);
                    emit_movzx_al(&res.code, RAX);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                }
                case IR_CALL: {
                    int nargs = ins->arg_count < 6 ? ins->arg_count : 6;
                    for (int a = 0; a < nargs; a++) emit_load_slot(&res.code, ARG_REGS[a], ins->args[a]);
                    emit_jmp_placeholder(&res.code, 0xE8, -1, &fixups, 1, ins->call_func, 0);
                    emit_store_slot(&res.code, ins->dst, RAX);
                    break;
                }
                case IR_RET:
                    if (ins->a >= 0) emit_load_slot(&res.code, RAX, ins->a);
                    else emit_mov_ri64(&res.code, RAX, 0);
                    emit_mov_rr(&res.code, RSP, RBP);
                    emit_pop(&res.code, RBP);
                    emit_ret(&res.code);
                    break;
                case IR_LABEL: {
                    LabelOffset lo; lo.label = (int)ins->imm; lo.offset = res.code.len;
                    dynarray_push(&labels, &lo);
                    break;
                }
                case IR_JUMP:
                    emit_jmp_placeholder(&res.code, 0xE9, -1, &local_fixups, 0, 0, (int)ins->imm);
                    break;
                case IR_JUMP_IF_ZERO:
                    emit_load_slot(&res.code, RAX, ins->a);
                    emit_test_rax_rax(&res.code);
                    emit_jmp_placeholder(&res.code, 0x0F, 0x84, &local_fixups, 0, 0, (int)ins->imm); /* JZ rel32 */
                    break;
            }
        }

        LabelOffset *lo_arr = (LabelOffset *)labels.data;
        Fixup *lf_arr = (Fixup *)local_fixups.data;
        for (int i = 0; i < local_fixups.count; i++) {
            size_t target = 0; int found = 0;
            for (int j = 0; j < labels.count; j++) if (lo_arr[j].label == lf_arr[i].target_label) { target = lo_arr[j].offset; found = 1; break; }
            if (!found) continue;
            unsigned int rel = (unsigned int)((long long)target - (long long)(lf_arr[i].patch_offset + 4));
            buf_u32_at(&res.code, lf_arr[i].patch_offset, rel);
        }
    }

    Fixup *fx_arr = (Fixup *)fixups.data;
    for (int i = 0; i < fixups.count; i++) {
        size_t target = res.func_offsets[fx_arr[i].target_func].offset;
        unsigned int rel = (unsigned int)((long long)target - (long long)(fx_arr[i].patch_offset + 4));
        buf_u32_at(&res.code, fx_arr[i].patch_offset, rel);
    }

    return res;
}
