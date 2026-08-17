/* pe.h — 最小のPE32+実行ファイル(Windows x64、コンソールサブシステム)を書き出す。
   ELFと違いWindowsには安定した生syscallが無いため、プロセス終了には
   kernel32.dll!ExitProcess をインポートして呼ぶ必要がある。そのための
   最小限のインポートテーブル(.idata)を自前で構築する。 */
#ifndef TMC_PE_H
#define TMC_PE_H

#include <stddef.h>

/* code は関数群の機械語(_startスタブは含まない、Windows x64 ABIで生成されたもの)。
   main_offset はcode内でのmain関数の開始オフセット。
   path にPE32+実行ファイル(.exe)を書き出す。成功で0、失敗で-1。 */
int pe_write_executable(const char *path, const unsigned char *code, size_t code_len, size_t main_offset);

#endif /* TMC_PE_H */
