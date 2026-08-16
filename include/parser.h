/* parser.h — Tenmu (tmc0) 再帰下降パーサー */
#ifndef TMC_PARSER_H
#define TMC_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer lexer;
    Token cur;
    Token peek_tok;
    int   has_peek;

    int   no_struct_literal; /* if/while/matchの条件式中で構造体リテラルの{}を禁止するフラグ */

    int   had_error;
    int   error_count;
    char  error_msg[320];
    int   error_line, error_col;
} Parser;

void      parser_init(Parser *p, const char *src, size_t len);
Program  *parser_parse_program(Parser *p);

#endif /* TMC_PARSER_H */
