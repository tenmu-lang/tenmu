/* lexer.c — Tenmu (tmc0) レキサー実装 */
#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ===== 基本ヘルパ ===== */

static int peekn(Lexer *lx, size_t n) {
    if (lx->pos + n >= lx->len) return 0;
    return (unsigned char)lx->src[lx->pos + n];
}
static int peek(Lexer *lx)  { return peekn(lx, 0); }
static int peek2(Lexer *lx) { return peekn(lx, 1); }

/* 現在位置の1バイトを消費して返す。行/列を更新する唯一の場所。 */
static int advance(Lexer *lx) {
    if (lx->pos >= lx->len) return 0;
    int c = (unsigned char)lx->src[lx->pos++];
    if (c == '\n') { lx->line++; lx->col = 1; }
    else            { lx->col++; }
    return c;
}

static void lexer_error(Lexer *lx, const char *msg) {
    if (!lx->had_error) { /* 最初のエラーだけ記録する */
        lx->had_error = 1;
        lx->error_line = lx->line;
        lx->error_col = lx->col;
        snprintf(lx->error_msg, sizeof(lx->error_msg), "%s", msg);
    }
}

static int is_digit(int c)      { return c >= '0' && c <= '9'; }
static int is_hex_digit(int c)  { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hex_val(int c) {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}
static int is_ident_start(int c)    { return isalpha(c) || c == '_' || (unsigned char)c >= 0x80; }
static int is_ident_continue(int c) { return isalnum(c) || c == '_' || (unsigned char)c >= 0x80; }

/* 現在バイトからUTF-8の1コードポイントをデコードして消費する(char literal用) */
static long decode_utf8_and_advance(Lexer *lx) {
    unsigned char c0 = (unsigned char)advance(lx);
    if (c0 < 0x80) return c0;
    int extra; long cp;
    if      ((c0 & 0xE0) == 0xC0) { extra = 1; cp = c0 & 0x1F; }
    else if ((c0 & 0xF0) == 0xE0) { extra = 2; cp = c0 & 0x0F; }
    else if ((c0 & 0xF8) == 0xF0) { extra = 3; cp = c0 & 0x07; }
    else { lexer_error(lx, "invalid UTF-8 lead byte"); return -1; }
    for (int i = 0; i < extra; i++) {
        unsigned char cn = (unsigned char)advance(lx);
        if ((cn & 0xC0) != 0x80) { lexer_error(lx, "invalid UTF-8 continuation byte"); return -1; }
        cp = (cp << 6) | (cn & 0x3F);
    }
    return cp;
}

/* '\' の位置で呼ばれ、エスケープシーケンス全体(\の後ろ)を正しく消費する。
   デコードされたコードポイントを返す(不正なら-1、had_errorを立てる)。
   \x.. \u{...} のような可変長エスケープでも呼び出し側は追加の長さ計算をしなくてよい。 */
static long scan_escape(Lexer *lx) {
    advance(lx); /* consume '\' */
    int c = advance(lx);
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '0': return 0;
        case '#': return '#';
        case 'x': {
            int hi = advance(lx), lo = advance(lx);
            if (!is_hex_digit(hi) || !is_hex_digit(lo)) { lexer_error(lx, "invalid \\x escape"); return -1; }
            return (hex_val(hi) << 4) | hex_val(lo);
        }
        case 'u': {
            if (advance(lx) != '{') { lexer_error(lx, "expected '{' after \\u"); return -1; }
            long v = 0; int digits = 0;
            while (is_hex_digit(peek(lx)) && digits < 6) { v = v * 16 + hex_val(advance(lx)); digits++; }
            if (digits == 0 || peek(lx) != '}') { lexer_error(lx, "invalid \\u{...} escape"); return -1; }
            advance(lx);
            return v;
        }
        default:
            lexer_error(lx, "unknown escape sequence");
            return -1;
    }
}

static Token make_token(Lexer *lx, TokenKind kind, size_t start_pos, int start_line, int start_col) {
    Token t;
    t.kind = kind;
    t.start = lx->src + start_pos;
    t.len = lx->pos - start_pos;
    t.line = start_line;
    t.col = start_col;
    t.value.int_val = 0;
    return t;
}

/* ends_statement: この種類のトークンの直後の改行は文の終わりとみなし、
   仮想セミコロン(TOK_NEWLINE)を発行すべきかどうか。Go方式のASIと同じ考え方。 */
static int ends_statement(TokenKind k) {
    switch (k) {
        case TOK_IDENT: case TOK_INT: case TOK_FLOAT: case TOK_CHAR:
        case TOK_STRING: case TOK_STR_INTERP_END:
        case TOK_RPAREN: case TOK_RBRACKET: case TOK_RBRACE:
        case TOK_TRUE: case TOK_FALSE: case TOK_NULL: case TOK_VOID:
        case TOK_SELF: case TOK_SELF_TYPE:
        case TOK_RETURN: case TOK_BREAK: case TOK_CONTINUE:
        case TOK_QUESTION:
            return 1;
        default:
            return 0;
    }
}

/* ===== 文字列本体スキャン =====
   カーソルが「文字列内容の先頭」(開き"の直後、または #{...} を閉じた直後)に
   あるときに呼ぶ。'"' で終わるか '#{' に出会うまで生テキストとして読み進める。 */
static Token scan_string_content(Lexer *lx, TokenKind kind_if_end, TokenKind kind_if_interp) {
    size_t start_pos = lx->pos;
    int start_line = lx->line, start_col = lx->col;

    for (;;) {
        int c = peek(lx);
        if (c == 0 && lx->pos >= lx->len) {
            lexer_error(lx, "unterminated string literal");
            return make_token(lx, kind_if_end, start_pos, start_line, start_col);
        }
        if (c == '"') {
            Token t = make_token(lx, kind_if_end, start_pos, start_line, start_col);
            advance(lx); /* consume closing " */
            return t;
        }
        if (c == '#' && peek2(lx) == '{') {
            Token t = make_token(lx, kind_if_interp, start_pos, start_line, start_col);
            advance(lx); advance(lx); /* consume '#{' */
            if (lx->interp_depth >= LEXER_MAX_INTERP_DEPTH) {
                lexer_error(lx, "string interpolation nested too deeply");
                return t;
            }
            lx->interp_stack[lx->interp_depth] = 0;
            lx->interp_depth++;
            return t;
        }
        if (c == '\\') {
            scan_escape(lx); /* 値は使わない。正しい長さだけ消費させる */
            continue;
        }
        advance(lx);
    }
}

static Token lex_raw_string(Lexer *lx) {
    size_t start_pos = lx->pos;
    int start_line = lx->line, start_col = lx->col;
    for (;;) {
        int c = peek(lx);
        if (c == 0 && lx->pos >= lx->len) { lexer_error(lx, "unterminated raw string literal"); break; }
        if (c == '"') break;
        advance(lx);
    }
    Token t = make_token(lx, TOK_STRING, start_pos, start_line, start_col);
    if (peek(lx) == '"') advance(lx);
    return t;
}

static Token lex_char(Lexer *lx) {
    size_t start_pos = lx->pos;
    int start_line = lx->line, start_col = lx->col;
    advance(lx); /* consume opening ' */
    long value;
    if (peek(lx) == '\'' || (peek(lx) == 0 && lx->pos >= lx->len)) {
        lexer_error(lx, "empty or unterminated char literal");
        value = -1;
    } else if (peek(lx) == '\\') {
        value = scan_escape(lx);
    } else {
        value = decode_utf8_and_advance(lx);
    }
    if (peek(lx) != '\'') {
        lexer_error(lx, "char literal must contain exactly one character");
    } else {
        advance(lx);
    }
    Token t = make_token(lx, TOK_CHAR, start_pos, start_line, start_col);
    t.value.int_val = value;
    return t;
}

/* underscore区切り・0x/0b/0o接頭辞を許すため、strtoll/strtodへ渡す前に整形する */
static long long parse_int_text(const char *text, size_t len) {
    char buf[160];
    size_t n = 0, i = 0;
    int base = 10;
    if (len >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; i = 2; }
    else if (len >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) { base = 2; i = 2; }
    else if (len >= 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) { base = 8; i = 2; }
    for (; i < len && n < sizeof(buf) - 1; i++) {
        if (text[i] == '_') continue;
        buf[n++] = text[i];
    }
    buf[n] = '\0';
    return strtoll(buf, NULL, base);
}

static double parse_float_text(const char *text, size_t len) {
    char buf[160];
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(buf) - 1; i++) {
        if (text[i] == '_') continue;
        buf[n++] = text[i];
    }
    buf[n] = '\0';
    return strtod(buf, NULL);
}

static Token lex_number(Lexer *lx) {
    size_t start_pos = lx->pos;
    int start_line = lx->line, start_col = lx->col;
    int is_float = 0;

    if (peek(lx) == '0' && (peek2(lx) == 'x' || peek2(lx) == 'X')) {
        advance(lx); advance(lx);
        while (is_hex_digit(peek(lx)) || peek(lx) == '_') advance(lx);
    } else if (peek(lx) == '0' && (peek2(lx) == 'b' || peek2(lx) == 'B')) {
        advance(lx); advance(lx);
        while (peek(lx) == '0' || peek(lx) == '1' || peek(lx) == '_') advance(lx);
    } else if (peek(lx) == '0' && (peek2(lx) == 'o' || peek2(lx) == 'O')) {
        advance(lx); advance(lx);
        while ((peek(lx) >= '0' && peek(lx) <= '7') || peek(lx) == '_') advance(lx);
    } else {
        while (is_digit(peek(lx)) || peek(lx) == '_') advance(lx);
        if (peek(lx) == '.' && is_digit(peek2(lx))) {
            is_float = 1;
            advance(lx);
            while (is_digit(peek(lx)) || peek(lx) == '_') advance(lx);
        }
        if (peek(lx) == 'e' || peek(lx) == 'E') {
            size_t save_pos = lx->pos; int save_line = lx->line, save_col = lx->col;
            advance(lx);
            if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
            if (is_digit(peek(lx))) {
                is_float = 1;
                while (is_digit(peek(lx)) || peek(lx) == '_') advance(lx);
            } else {
                lx->pos = save_pos; lx->line = save_line; lx->col = save_col;
            }
        }
    }

    Token t = make_token(lx, is_float ? TOK_FLOAT : TOK_INT, start_pos, start_line, start_col);
    if (is_float) t.value.float_val = parse_float_text(t.start, t.len);
    else          t.value.int_val   = parse_int_text(t.start, t.len);
    return t;
}

typedef struct { const char *word; TokenKind kind; } Keyword;
static const Keyword KEYWORDS[] = {
    {"module", TOK_MODULE}, {"import", TOK_IMPORT}, {"pub", TOK_PUB}, {"fn", TOK_FN},
    {"let", TOK_LET}, {"mut", TOK_MUT}, {"const", TOK_CONST}, {"comptime", TOK_COMPTIME},
    {"struct", TOK_STRUCT}, {"enum", TOK_ENUM}, {"union", TOK_UNION}, {"trait", TOK_TRAIT}, {"impl", TOK_IMPL},
    {"for", TOK_FOR}, {"in", TOK_IN}, {"while", TOK_WHILE}, {"loop", TOK_LOOP},
    {"if", TOK_IF}, {"else", TOK_ELSE}, {"match", TOK_MATCH},
    {"return", TOK_RETURN}, {"break", TOK_BREAK}, {"continue", TOK_CONTINUE},
    {"defer", TOK_DEFER}, {"unsafe", TOK_UNSAFE}, {"extern", TOK_EXTERN},
    {"async", TOK_ASYNC}, {"await", TOK_AWAIT}, {"type", TOK_TYPE}, {"as", TOK_AS}, {"where", TOK_WHERE},
    {"self", TOK_SELF}, {"Self", TOK_SELF_TYPE},
    {"true", TOK_TRUE}, {"false", TOK_FALSE}, {"null", TOK_NULL}, {"void", TOK_VOID},
    {"error", TOK_ERROR}, {"try", TOK_TRY}, {"catch", TOK_CATCH},
    {NULL, TOK_ILLEGAL},
};

static Token lex_ident_or_keyword(Lexer *lx) {
    size_t start_pos = lx->pos;
    int start_line = lx->line, start_col = lx->col;
    while (is_ident_continue(peek(lx))) advance(lx);
    size_t len = lx->pos - start_pos;
    const char *text = lx->src + start_pos;
    for (const Keyword *k = KEYWORDS; k->word; k++) {
        size_t klen = strlen(k->word);
        if (klen == len && memcmp(k->word, text, len) == 0) {
            return make_token(lx, k->kind, start_pos, start_line, start_col);
        }
    }
    return make_token(lx, TOK_IDENT, start_pos, start_line, start_col);
}

static void skip_block_comment(Lexer *lx) {
    int depth = 1;
    advance(lx); advance(lx); /* consume the two-char block-comment opener */
    while (depth > 0) {
        if (lx->pos >= lx->len) { lexer_error(lx, "unterminated block comment"); return; }
        if (peek(lx) == '/' && peek2(lx) == '*') { advance(lx); advance(lx); depth++; }
        else if (peek(lx) == '*' && peek2(lx) == '/') { advance(lx); advance(lx); depth--; }
        else advance(lx);
    }
}

void lexer_init(Lexer *lx, const char *src, size_t len) {
    memset(lx, 0, sizeof(*lx));
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
    lx->has_prev = 0;
}

static Token finish(Lexer *lx, Token t) {
    lx->prev_kind = t.kind;
    lx->has_prev = 1;
    return t;
}

Token lexer_next(Lexer *lx) {
    for (;;) {
        /* --- 補間の中で } を見たら閉じ判定 --- */
        if (lx->interp_depth > 0 && peek(lx) == '}') {
            int top = lx->interp_stack[lx->interp_depth - 1];
            if (top == 0) {
                size_t p = lx->pos; int l = lx->line, c = lx->col;
                advance(lx); /* consume '}' */
                lx->interp_depth--;
                Token t = scan_string_content(lx, TOK_STR_INTERP_END, TOK_STR_INTERP_MID);
                (void)p; (void)l; (void)c;
                return finish(lx, t);
            }
        }

        if (lx->pos >= lx->len) {
            if (lx->has_prev && ends_statement(lx->prev_kind)) {
                Token t = make_token(lx, TOK_NEWLINE, lx->pos, lx->line, lx->col);
                return finish(lx, t);
            }
            Token t = make_token(lx, TOK_EOF, lx->pos, lx->line, lx->col);
            return finish(lx, t);
        }

        int c = peek(lx);

        if (c == '\n') {
            advance(lx);
            if (lx->has_prev && ends_statement(lx->prev_kind)) {
                Token t; t.kind = TOK_NEWLINE; t.start = lx->src + lx->pos; t.len = 0;
                t.line = lx->line - 1; t.col = 0; t.value.int_val = 0;
                return finish(lx, t);
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') { advance(lx); continue; }

        if (c == '/' && peek2(lx) == '/') {
            while (peek(lx) != '\n' && !(lx->pos >= lx->len)) advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '*') { skip_block_comment(lx); continue; }

        size_t start_pos = lx->pos;
        int start_line = lx->line, start_col = lx->col;

        if (is_digit(c)) return finish(lx, lex_number(lx));

        if ((c == 'r' || c == 'b') && peek2(lx) == '"') {
            int is_raw = (c == 'r');
            advance(lx); /* consume prefix letter */
            advance(lx); /* consume opening " */
            if (is_raw) return finish(lx, lex_raw_string(lx));
            return finish(lx, scan_string_content(lx, TOK_STRING, TOK_STR_INTERP_START));
        }

        if (is_ident_start(c)) return finish(lx, lex_ident_or_keyword(lx));

        if (c == '"') { advance(lx); return finish(lx, scan_string_content(lx, TOK_STRING, TOK_STR_INTERP_START)); }
        if (c == '\'') return finish(lx, lex_char(lx));

        /* --- 記号・演算子 (最長一致) --- */
        advance(lx);
        switch (c) {
            case '(': return finish(lx, make_token(lx, TOK_LPAREN, start_pos, start_line, start_col));
            case ')': return finish(lx, make_token(lx, TOK_RPAREN, start_pos, start_line, start_col));
            case '{':
                if (lx->interp_depth > 0) lx->interp_stack[lx->interp_depth - 1]++;
                return finish(lx, make_token(lx, TOK_LBRACE, start_pos, start_line, start_col));
            case '}':
                if (lx->interp_depth > 0 && lx->interp_stack[lx->interp_depth - 1] > 0)
                    lx->interp_stack[lx->interp_depth - 1]--;
                return finish(lx, make_token(lx, TOK_RBRACE, start_pos, start_line, start_col));
            case '[': return finish(lx, make_token(lx, TOK_LBRACKET, start_pos, start_line, start_col));
            case ']': return finish(lx, make_token(lx, TOK_RBRACKET, start_pos, start_line, start_col));
            case ',': return finish(lx, make_token(lx, TOK_COMMA, start_pos, start_line, start_col));
            case ';': return finish(lx, make_token(lx, TOK_SEMI, start_pos, start_line, start_col));
            case '@': return finish(lx, make_token(lx, TOK_AT, start_pos, start_line, start_col));
            case '%': return finish(lx, make_token(lx, TOK_PERCENT, start_pos, start_line, start_col));
            case '?': return finish(lx, make_token(lx, TOK_QUESTION, start_pos, start_line, start_col));
            case ':': return finish(lx, make_token(lx, TOK_COLON, start_pos, start_line, start_col));
            case '.':
                if (peek(lx) == '.') { advance(lx); return finish(lx, make_token(lx, TOK_DOTDOT, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_DOT, start_pos, start_line, start_col));
            case '-':
                if (peek(lx) == '>') { advance(lx); return finish(lx, make_token(lx, TOK_ARROW, start_pos, start_line, start_col)); }
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_MINUSEQ, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_MINUS, start_pos, start_line, start_col));
            case '+':
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_PLUSEQ, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_PLUS, start_pos, start_line, start_col));
            case '*':
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_STAREQ, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_STAR, start_pos, start_line, start_col));
            case '/':
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_SLASHEQ, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_SLASH, start_pos, start_line, start_col));
            case '=':
                if (peek(lx) == '>') { advance(lx); return finish(lx, make_token(lx, TOK_FATARROW, start_pos, start_line, start_col)); }
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_EQEQ, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_EQ, start_pos, start_line, start_col));
            case '!':
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_NE, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_BANG, start_pos, start_line, start_col));
            case '<':
                if (peek(lx) == '<') { advance(lx); return finish(lx, make_token(lx, TOK_SHL, start_pos, start_line, start_col)); }
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_LE, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_LT, start_pos, start_line, start_col));
            case '>':
                if (peek(lx) == '>') { advance(lx); return finish(lx, make_token(lx, TOK_SHR, start_pos, start_line, start_col)); }
                if (peek(lx) == '=') { advance(lx); return finish(lx, make_token(lx, TOK_GE, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_GT, start_pos, start_line, start_col));
            case '&':
                if (peek(lx) == '&') { advance(lx); return finish(lx, make_token(lx, TOK_AMPAMP, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_AMP, start_pos, start_line, start_col));
            case '|':
                if (peek(lx) == '|') { advance(lx); return finish(lx, make_token(lx, TOK_PIPEPIPE, start_pos, start_line, start_col)); }
                return finish(lx, make_token(lx, TOK_PIPE, start_pos, start_line, start_col));
            case '^': return finish(lx, make_token(lx, TOK_CARET, start_pos, start_line, start_col));
            case '#':
                if (peek(lx) == '[') { advance(lx); return finish(lx, make_token(lx, TOK_HASHBRACKET, start_pos, start_line, start_col)); }
                lexer_error(lx, "unexpected '#' (expected '#[')");
                return finish(lx, make_token(lx, TOK_ILLEGAL, start_pos, start_line, start_col));
            default:
                lexer_error(lx, "unexpected character");
                return finish(lx, make_token(lx, TOK_ILLEGAL, start_pos, start_line, start_col));
        }
    }
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_INT: return "INT"; case TOK_FLOAT: return "FLOAT"; case TOK_CHAR: return "CHAR";
        case TOK_STRING: return "STRING";
        case TOK_STR_INTERP_START: return "STR_INTERP_START";
        case TOK_STR_INTERP_MID: return "STR_INTERP_MID";
        case TOK_STR_INTERP_END: return "STR_INTERP_END";
        case TOK_IDENT: return "IDENT";
        case TOK_MODULE: return "module"; case TOK_IMPORT: return "import"; case TOK_PUB: return "pub";
        case TOK_FN: return "fn"; case TOK_LET: return "let"; case TOK_MUT: return "mut";
        case TOK_CONST: return "const"; case TOK_COMPTIME: return "comptime";
        case TOK_STRUCT: return "struct"; case TOK_ENUM: return "enum"; case TOK_UNION: return "union";
        case TOK_TRAIT: return "trait"; case TOK_IMPL: return "impl";
        case TOK_FOR: return "for"; case TOK_IN: return "in"; case TOK_WHILE: return "while";
        case TOK_LOOP: return "loop"; case TOK_IF: return "if"; case TOK_ELSE: return "else"; case TOK_MATCH: return "match";
        case TOK_RETURN: return "return"; case TOK_BREAK: return "break"; case TOK_CONTINUE: return "continue";
        case TOK_DEFER: return "defer"; case TOK_UNSAFE: return "unsafe"; case TOK_EXTERN: return "extern";
        case TOK_ASYNC: return "async"; case TOK_AWAIT: return "await"; case TOK_TYPE: return "type";
        case TOK_AS: return "as"; case TOK_WHERE: return "where"; case TOK_SELF: return "self"; case TOK_SELF_TYPE: return "Self";
        case TOK_TRUE: return "true"; case TOK_FALSE: return "false"; case TOK_NULL: return "null"; case TOK_VOID: return "void";
        case TOK_ERROR: return "error"; case TOK_TRY: return "try"; case TOK_CATCH: return "catch";
        case TOK_LPAREN: return "("; case TOK_RPAREN: return ")"; case TOK_LBRACE: return "{"; case TOK_RBRACE: return "}";
        case TOK_LBRACKET: return "["; case TOK_RBRACKET: return "]";
        case TOK_COMMA: return ","; case TOK_DOT: return "."; case TOK_DOTDOT: return "..";
        case TOK_COLON: return ":"; case TOK_COLONCOLON: return "::"; case TOK_SEMI: return ";";
        case TOK_ARROW: return "->"; case TOK_FATARROW: return "=>"; case TOK_HASHBRACKET: return "#[";
        case TOK_PLUS: return "+"; case TOK_MINUS: return "-"; case TOK_STAR: return "*"; case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%"; case TOK_AT: return "@";
        case TOK_AMP: return "&"; case TOK_AMPAMP: return "&&"; case TOK_PIPE: return "|"; case TOK_PIPEPIPE: return "||";
        case TOK_CARET: return "^"; case TOK_BANG: return "!";
        case TOK_EQ: return "="; case TOK_EQEQ: return "=="; case TOK_NE: return "!=";
        case TOK_LT: return "<"; case TOK_LE: return "<="; case TOK_GT: return ">"; case TOK_GE: return ">=";
        case TOK_SHL: return "<<"; case TOK_SHR: return ">>";
        case TOK_PLUSEQ: return "+="; case TOK_MINUSEQ: return "-="; case TOK_STAREQ: return "*="; case TOK_SLASHEQ: return "/=";
        case TOK_QUESTION: return "?";
        case TOK_NEWLINE: return "NEWLINE"; case TOK_EOF: return "EOF"; case TOK_ILLEGAL: return "ILLEGAL";
        default: return "?";
    }
}
