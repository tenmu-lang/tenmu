/* elf.c — 最小のELF64実行ファイルを書き出す(静的リンク、リンカ不使用) */
#include "elf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BASE_ADDR 0x400000ULL
#define EHDR_SIZE 64
#define PHDR_SIZE 56

static void put_u16(unsigned char *p, unsigned short v) { p[0]=(unsigned char)(v&0xFF); p[1]=(unsigned char)((v>>8)&0xFF); }
static void put_u32(unsigned char *p, unsigned int v) {
    p[0]=(unsigned char)(v&0xFF); p[1]=(unsigned char)((v>>8)&0xFF);
    p[2]=(unsigned char)((v>>16)&0xFF); p[3]=(unsigned char)((v>>24)&0xFF);
}
static void put_u64(unsigned char *p, unsigned long long v) {
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)((v >> (8*i)) & 0xFF);
}

int elf_write_executable(const char *path, const unsigned char *code, size_t code_len, size_t main_offset) {
    /* _start スタブ: call main; mov edi, eax; mov eax, 60(SYS_exit); syscall */
    unsigned char stub[14];
    stub[0] = 0xE8; /* call rel32 (rel32は後で埋める) */
    /* stub[1..4] = rel32 placeholder */
    stub[5] = 0x89; stub[6] = 0xC7;                    /* mov edi, eax */
    stub[7] = 0xB8; put_u32(&stub[8], 60);              /* mov eax, 60 */
    stub[12] = 0x0F; stub[13] = 0x05;                    /* syscall */

    size_t header_size = EHDR_SIZE + PHDR_SIZE;
    size_t stub_offset = header_size;
    size_t stub_size = sizeof(stub);
    size_t code_offset = stub_offset + stub_size;
    size_t total_size = code_offset + code_len;

    unsigned long long entry_vaddr = BASE_ADDR + stub_offset;
    unsigned long long main_vaddr = BASE_ADDR + code_offset + main_offset;
    unsigned long long call_next_insn_vaddr = entry_vaddr + 5; /* call命令(5バイト)の直後 */
    unsigned int rel32 = (unsigned int)((long long)main_vaddr - (long long)call_next_insn_vaddr);
    put_u32(&stub[1], rel32);

    unsigned char *buf = malloc(total_size);
    if (!buf) return -1;
    memset(buf, 0, total_size);

    /* ELFヘッダ */
    unsigned char *eh = buf;
    eh[0]=0x7F; eh[1]='E'; eh[2]='L'; eh[3]='F';
    eh[4]=2; /* ELFCLASS64 */
    eh[5]=1; /* ELFDATA2LSB (little endian) */
    eh[6]=1; /* EV_CURRENT */
    eh[7]=0; /* ELFOSABI_SYSV */
    /* eh[8..15] = ABI version + padding, already 0 */
    put_u16(eh+16, 2);              /* e_type = ET_EXEC */
    put_u16(eh+18, 0x3E);            /* e_machine = EM_X86_64 */
    put_u32(eh+20, 1);                /* e_version */
    put_u64(eh+24, entry_vaddr);       /* e_entry */
    put_u64(eh+32, EHDR_SIZE);          /* e_phoff */
    put_u64(eh+40, 0);                   /* e_shoff */
    put_u32(eh+48, 0);                    /* e_flags */
    put_u16(eh+52, EHDR_SIZE);             /* e_ehsize */
    put_u16(eh+54, PHDR_SIZE);              /* e_phentsize */
    put_u16(eh+56, 1);                       /* e_phnum */
    put_u16(eh+58, 0);                        /* e_shentsize */
    put_u16(eh+60, 0);                         /* e_shnum */
    put_u16(eh+62, 0);                          /* e_shstrndx */

    /* プログラムヘッダ(PT_LOAD、ファイル全体を1セグメントとしてR+X属性でロード) */
    unsigned char *ph = buf + EHDR_SIZE;
    put_u32(ph+0, 1);            /* p_type = PT_LOAD */
    put_u32(ph+4, 5);             /* p_flags = PF_R|PF_X */
    put_u64(ph+8, 0);              /* p_offset */
    put_u64(ph+16, BASE_ADDR);      /* p_vaddr */
    put_u64(ph+24, BASE_ADDR);       /* p_paddr */
    put_u64(ph+32, total_size);       /* p_filesz */
    put_u64(ph+40, total_size);        /* p_memsz */
    put_u64(ph+48, 0x1000);              /* p_align */

    memcpy(buf + stub_offset, stub, stub_size);
    memcpy(buf + code_offset, code, code_len);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    size_t written = fwrite(buf, 1, total_size, f);
    fclose(f);
    free(buf);
    if (written != total_size) return -1;

    chmod(path, 0755);
    return 0;
}
