# start_linux.s — LLVMが生成したオブジェクトをリンクする際の最小エントリポイント。
# main()を正しい関数呼び出しとして呼び、その戻り値でexitする
# (直接mainへジャンプするとスタックにリターンアドレスが無く、main末尾のretで
#  プロセススタートアップ時の生スタック内容へ飛んでしまいクラッシュする)。
    .global _start
    .text
_start:
    xor %ebp, %ebp
    and $-16, %rsp
    call main
    mov %eax, %edi
    mov $60, %eax      # SYS_exit
    syscall
