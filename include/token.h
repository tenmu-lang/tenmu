/* token.h — Tenmu (tmc0) トークン定義 */
#ifndef TMC_TOKEN_H
#define TMC_TOKEN_H

#include <stddef.h>

typedef enum {
    /* リテラル */
    TOK_INT, TOK_FLOAT, TOK_CHAR,
    TOK_STRING,               /* 補間を含まない通常の文字列全体 */
    TOK_STR_INTERP_START,     /* "abc#{  の "abc" 部分 */
    TOK_STR_INTERP_MID,       /* }def#{  の "def" 部分 */
    TOK_STR_INTERP_END,       /* }ghi"   の "ghi" 部分 */
    TOK_IDENT,

    /* キーワード */
    TOK_MODULE, TOK_IMPORT, TOK_PUB, TOK_FN, TOK_LET, TOK_MUT, TOK_CONST, TOK_COMPTIME,
    TOK_STRUCT, TOK_ENUM, TOK_UNION, TOK_TRAIT, TOK_IMPL,
    TOK_FOR, TOK_IN, TOK_WHILE, TOK_LOOP, TOK_IF, TOK_ELSE, TOK_MATCH,
    TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_DEFER, TOK_UNSAFE, TOK_EXTERN,
    TOK_ASYNC, TOK_AWAIT, TOK_TYPE, TOK_AS, TOK_WHERE, TOK_SELF, TOK_SELF_TYPE,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_VOID, TOK_ERROR, TOK_TRY, TOK_CATCH,

    /* 括弧類 */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,

    /* 区切り記号 */
    TOK_COMMA, TOK_DOT, TOK_DOTDOT, TOK_COLON, TOK_COLONCOLON, TOK_SEMI,
    TOK_ARROW,      /* -> */
    TOK_FATARROW,   /* => */
    TOK_HASHBRACKET,/* #[ */

    /* 演算子 */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_AT,
    TOK_AMP, TOK_AMPAMP, TOK_PIPE, TOK_PIPEPIPE, TOK_CARET, TOK_BANG,
    TOK_EQ, TOK_EQEQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_SHL, TOK_SHR,
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ,
    TOK_QUESTION,

    TOK_NEWLINE,   /* 文終端になりうる改行(パーサーがTOK_SEMIと同様に扱う) */
    TOK_EOF,
    TOK_ILLEGAL,
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *start;   /* ソースバッファ内へのポインタ(所有しない) */
    size_t      len;
    int         line;
    int         col;
    union {
        long long   int_val;
        double      float_val;
    } value;
} Token;

const char *token_kind_name(TokenKind kind);

#endif /* TMC_TOKEN_H */
