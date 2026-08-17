/* elf.h — 最小のELF64実行ファイル(静的、リンカ不使用、Linux x86-64)を書き出す。
   codegenが生成した機械語コード列をそのまま1つのPT_LOADセグメントとして
   埋め込む。エントリポイントは自前の_startスタブ: mainを呼び、その戻り値を
   exit(2)のステータスとして使う(SYS_exit直接呼び出し、libc不要)。 */
#ifndef TMC_ELF_H
#define TMC_ELF_H

#include <stddef.h>

/* code は関数群の機械語(_startスタブは含まない)。main_offset はcode内での
   main関数の開始オフセット。path に完全なELF実行ファイルを書き出し、
   実行可能パーミッションを付与する。成功で0、失敗で-1。 */
int elf_write_executable(const char *path, const unsigned char *code, size_t code_len, size_t main_offset);

#endif /* TMC_ELF_H */
