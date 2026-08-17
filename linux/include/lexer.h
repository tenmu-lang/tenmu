/* lexer.h — Tenmu (tmc0) レキサー */
#ifndef TMC_LEXER_H
#define TMC_LEXER_H

#include "token.h"

#define LEXER_MAX_INTERP_DEPTH 16

typedef struct {
    const char *src;       /* NUL終端されたソース全体 */
    size_t      pos;       /* 現在の読み取り位置(バイト) */
    size_t      len;
    int         line;      /* 1始まり */
    int         col;       /* 1始まり */

    TokenKind   prev_kind; /* 直前に返したトークンの種類(改行の自動セミコロン挿入判定用) */
    int         has_prev;

    /* 文字列補間 "..#{ expr }.." のネストを扱うスタック。
       interp_stack[i] は i番目の #{ の中で見ている '{' の深さ(閉じ判定用)。 */
    int interp_stack[LEXER_MAX_INTERP_DEPTH];
    int interp_depth;

    int  had_error;
    char error_msg[256];
    int  error_line;
    int  error_col;
} Lexer;

void  lexer_init(Lexer *lx, const char *src, size_t len);
Token lexer_next(Lexer *lx);

#endif /* TMC_LEXER_H */
