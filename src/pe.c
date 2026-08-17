/* pe.c — 最小のPE32+実行ファイル(Windows x64)を書き出す。
   kernel32.dll!ExitProcess のみをインポートし、_startスタブから
   「call main; ecx=eax; call [ExitProcess IATスロット]」で終了する。 */
#include "pe.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_BASE   0x140000000ULL
#define SECT_ALIGN   0x1000u
#define FILE_ALIGN   0x200u
#define DOS_HDR_SIZE 64u
#define PE_HDRS_SIZE (4u + 20u + 240u + 2u * 40u) /* sig + COFF + Optional(16 dirs) + 2 section headers */

static unsigned int align_up(unsigned int v, unsigned int a) { return (v + a - 1) & ~(a - 1); }

static void w8(unsigned char *p, unsigned char v) { *p = v; }
static void w16(unsigned char *p, unsigned short v) { p[0]=(unsigned char)(v&0xFF); p[1]=(unsigned char)((v>>8)&0xFF); }
static void w32(unsigned char *p, unsigned int v) {
    p[0]=(unsigned char)(v&0xFF); p[1]=(unsigned char)((v>>8)&0xFF);
    p[2]=(unsigned char)((v>>16)&0xFF); p[3]=(unsigned char)((v>>24)&0xFF);
}
static void w64(unsigned char *p, unsigned long long v) { for (int i=0;i<8;i++) p[i]=(unsigned char)((v>>(8*i))&0xFF); }

int pe_write_executable(const char *path, const unsigned char *code, size_t code_len, size_t main_offset) {
    /* ===== _start スタブ (Windows x64 ABI) =====
       sub rsp, 40           ; シャドウスペース32B + mainへのcall前アラインメント調整8B
       call main             ; rel32 (後で埋める)
       mov ecx, eax          ; ExitProcessの第1引数(戻り値=終了コード)
       call [rip+disp32]     ; IATのExitProcessスロットを経由した間接呼び出し(後で埋める)
       (呼び出しから戻ることは無い想定だが、安全のためhlt相当は入れずret) */
    unsigned char stub[7 + 5 + 2 + 6];
    size_t o = 0;
    /* sub rsp, 40 : REX.W 81 /5 id */
    stub[o++]=0x48; stub[o++]=0x81; stub[o++]=0xEC; w32(&stub[o], 40); o+=4;
    size_t call_main_patch = o + 1;
    stub[o++]=0xE8; w32(&stub[o], 0); o+=4;              /* call main (rel32 placeholder) */
    stub[o++]=0x89; stub[o++]=0xC1;                        /* mov ecx, eax */
    size_t call_iat_patch = o + 2;
    stub[o++]=0xFF; stub[o++]=0x15; w32(&stub[o], 0); o+=4; /* call qword ptr [rip+disp32] (placeholder) */
    size_t stub_size = o;

    /* ===== .idata の内容(インポートディレクトリ, ILT, IAT, Hint/Name, DLL名) ===== */
    const char *dll_name = "KERNEL32.dll";
    const char *fn_name = "ExitProcess";
    size_t dll_name_len = strlen(dll_name) + 1; if (dll_name_len % 2) dll_name_len++;
    size_t hintname_len = 2 + strlen(fn_name) + 1; if (hintname_len % 2) hintname_len++;

    size_t idir_off = 0;
    size_t idir_size = 20 * 2; /* 実エントリ1つ + ヌム終端 */
    size_t ilt_off = idir_off + idir_size;
    size_t ilt_size = 8 * 2;
    size_t iat_off = ilt_off + ilt_size;
    size_t iat_size = 8 * 2;
    size_t hintname_off = iat_off + iat_size;
    size_t dllname_off = hintname_off + hintname_len;
    size_t idata_size = dllname_off + dll_name_len;

    /* ===== セクションのRVA/ファイルオフセットを確定 ===== */
    unsigned int size_of_headers = align_up(DOS_HDR_SIZE + PE_HDRS_SIZE, FILE_ALIGN);
    unsigned int text_rva = align_up(size_of_headers, SECT_ALIGN);
    unsigned int text_vsize = (unsigned int)(stub_size + code_len);
    unsigned int text_raw_size = align_up(text_vsize, FILE_ALIGN);
    unsigned int text_file_off = size_of_headers; /* 最初のセクションのraw dataはヘッダ直後(整列済み)から */

    unsigned int idata_rva = align_up(text_rva + text_vsize, SECT_ALIGN);
    unsigned int idata_vsize = (unsigned int)idata_size;
    unsigned int idata_raw_size = align_up(idata_vsize, FILE_ALIGN);
    unsigned int idata_file_off = text_file_off + text_raw_size;

    unsigned int size_of_image = align_up(idata_rva + idata_vsize, SECT_ALIGN);
    unsigned int total_file_size = idata_file_off + idata_raw_size;

    unsigned int entry_rva = text_rva; /* _start はtextセクションの先頭 */
    unsigned int main_rva = text_rva + (unsigned int)stub_size + (unsigned int)main_offset;

    unsigned int ilt_rva = idata_rva + (unsigned int)ilt_off;
    unsigned int iat_rva = idata_rva + (unsigned int)iat_off;
    unsigned int hintname_rva = idata_rva + (unsigned int)hintname_off;
    unsigned int dllname_rva = idata_rva + (unsigned int)dllname_off;
    unsigned int idir_rva = idata_rva + (unsigned int)idir_off;

    /* call main の rel32 を確定(スタブはtextセクション先頭に配置される) */
    {
        unsigned long long call_main_next = (unsigned long long)entry_rva + (call_main_patch + 4);
        unsigned int rel = (unsigned int)((long long)main_rva - (long long)call_main_next);
        w32(&stub[call_main_patch], rel);
    }
    /* call [rip+disp32] の disp32 を確定(IATの1番目のスロット = ExitProcess) */
    {
        unsigned long long call_iat_next = (unsigned long long)entry_rva + (call_iat_patch + 4);
        unsigned int rel = (unsigned int)((long long)iat_rva - (long long)call_iat_next);
        w32(&stub[call_iat_patch], rel);
    }

    unsigned char *buf = xmalloc(total_file_size);
    memset(buf, 0, total_file_size);

    /* ---- DOS header ---- */
    buf[0]='M'; buf[1]='Z';
    w32(buf + 0x3C, DOS_HDR_SIZE); /* e_lfanew: PEヘッダはDOSヘッダ直後 */

    unsigned char *pe = buf + DOS_HDR_SIZE;
    /* PE signature */
    pe[0]='P'; pe[1]='E'; pe[2]=0; pe[3]=0;
    unsigned char *coff = pe + 4;
    w16(coff + 0, 0x8664);      /* Machine = AMD64 */
    w16(coff + 2, 2);            /* NumberOfSections */
    w32(coff + 4, 0);             /* TimeDateStamp */
    w32(coff + 8, 0);              /* PointerToSymbolTable */
    w32(coff + 12, 0);              /* NumberOfSymbols */
    w16(coff + 16, 240);             /* SizeOfOptionalHeader */
    w16(coff + 18, 0x0022);           /* Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE */

    unsigned char *opt = coff + 20;
    w16(opt + 0, 0x20B);          /* Magic = PE32+ */
    w8(opt + 2, 1); w8(opt + 3, 0); /* Linker version */
    w32(opt + 4, text_raw_size);      /* SizeOfCode */
    w32(opt + 8, idata_raw_size);      /* SizeOfInitializedData */
    w32(opt + 12, 0);                   /* SizeOfUninitializedData */
    w32(opt + 16, entry_rva);            /* AddressOfEntryPoint */
    w32(opt + 20, text_rva);              /* BaseOfCode */
    w64(opt + 24, IMAGE_BASE);
    w32(opt + 32, SECT_ALIGN);
    w32(opt + 36, FILE_ALIGN);
    w16(opt + 40, 6); w16(opt + 42, 0);   /* MajorOSVersion / Minor */
    w16(opt + 44, 0); w16(opt + 46, 0);    /* Image version */
    w16(opt + 48, 6); w16(opt + 50, 0);     /* Subsystem version */
    w32(opt + 52, 0);                        /* Win32VersionValue */
    w32(opt + 56, size_of_image);
    w32(opt + 60, size_of_headers);
    w32(opt + 64, 0);                          /* CheckSum */
    w16(opt + 68, 3);                           /* Subsystem = WINDOWS_CUI */
    w16(opt + 70, 0);                            /* DllCharacteristics */
    w64(opt + 72, 0x100000);                      /* SizeOfStackReserve */
    w64(opt + 80, 0x1000);                         /* SizeOfStackCommit */
    w64(opt + 88, 0x100000);                        /* SizeOfHeapReserve */
    w64(opt + 96, 0x1000);                           /* SizeOfHeapCommit */
    w32(opt + 104, 0);                                /* LoaderFlags */
    w32(opt + 108, 16);                                /* NumberOfRvaAndSizes */
    unsigned char *datadirs = opt + 112;
    /* DataDirectory[1] = Import Table */
    w32(datadirs + 1*8 + 0, idir_rva);
    w32(datadirs + 1*8 + 4, (unsigned int)idir_size);
    /* 他のディレクトリは全て0のまま(未使用) */

    unsigned char *sect = opt + 240;
    /* .text */
    memcpy(sect, ".text\0\0\0", 8);
    w32(sect + 8, text_vsize);
    w32(sect + 12, text_rva);
    w32(sect + 16, text_raw_size);
    w32(sect + 20, text_file_off);
    w32(sect + 24, 0); w32(sect + 28, 0); w16(sect + 32, 0); w16(sect + 34, 0);
    w32(sect + 36, 0x60000020u); /* CODE | MEM_EXECUTE | MEM_READ */
    sect += 40;
    /* .idata */
    memcpy(sect, ".idata\0\0", 8);
    w32(sect + 8, idata_vsize);
    w32(sect + 12, idata_rva);
    w32(sect + 16, idata_raw_size);
    w32(sect + 20, idata_file_off);
    w32(sect + 24, 0); w32(sect + 28, 0); w16(sect + 32, 0); w16(sect + 34, 0);
    w32(sect + 36, 0xC0000040u); /* INITIALIZED_DATA | MEM_READ | MEM_WRITE */

    /* ---- .text 内容: スタブ + 関数コード ---- */
    unsigned char *text_dst = buf + text_file_off;
    memcpy(text_dst, stub, stub_size);
    memcpy(text_dst + stub_size, code, code_len);

    /* ---- .idata 内容 ---- */
    unsigned char *id = buf + idata_file_off;
    /* Import Directory Table: エントリ1つ(KERNEL32.dll) */
    w32(id + idir_off + 0, ilt_rva);       /* OriginalFirstThunk */
    w32(id + idir_off + 4, 0);               /* TimeDateStamp */
    w32(id + idir_off + 8, 0);                /* ForwarderChain */
    w32(id + idir_off + 12, dllname_rva);      /* Name */
    w32(id + idir_off + 16, iat_rva);           /* FirstThunk */
    /* [idir_off+20..+40) はゼロ終端エントリ(memsetで既に0) */

    /* ILT / IAT: どちらも同じ内容(Hint/Nameを指すRVA)で初期化する。
       ローダがIATだけを実アドレスへ書き換える。 */
    w64(id + ilt_off + 0, hintname_rva);
    w64(id + ilt_off + 8, 0); /* 終端 */
    w64(id + iat_off + 0, hintname_rva);
    w64(id + iat_off + 8, 0); /* 終端 */

    /* Hint/Name */
    w16(id + hintname_off, 0);
    memcpy(id + hintname_off + 2, fn_name, strlen(fn_name) + 1);

    /* DLL名 */
    memcpy(id + dllname_off, dll_name, strlen(dll_name) + 1);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    size_t written = fwrite(buf, 1, total_file_size, f);
    fclose(f);
    free(buf);
    return written == total_file_size ? 0 : -1;
}
