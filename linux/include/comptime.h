/* comptime.h — コンパイル時評価器(sandboxed interpreter)。
   const宣言の初期化式や comptime fn の呼び出しを実際に評価する。
   対応するのはリテラル・算術/比較/論理演算・if式・関数呼び出し(他のcomptime fn)・
   let束縛・return文・有界のfor/while/loopのみ。ホストI/Oや生ポインタ操作は
   一切行わない(§3.9で規定されたサンドボックス方針の通り)。 */
#ifndef TMC_COMPTIME_H
#define TMC_COMPTIME_H

#include "ast.h"
#include "symtab.h"

typedef enum { CV_INT, CV_FLOAT, CV_BOOL, CV_VOID, CV_ERROR } ComptimeValueKind;

typedef struct {
    ComptimeValueKind kind;
    union {
        long long i;
        double f;
        int b; /* 0/1 */
    } as;
    char error_msg[128]; /* kind == CV_ERROR の場合のみ有効 */
} ComptimeValue;

typedef struct {
    Scope *global;       /* トップレベル関数/定数を引くためのシンボルテーブル(checkerと共有) */
    int call_depth;
    int step_count;       /* 実行した式の総数。暴走防止用 */
    int had_error;

    /* return文の伝播: ブロック/if式のネストをまたいで関数呼び出し境界まで
       "戻り値が確定した"ことを伝えるための共有フラグ。式の戻り値をそのまま
       バケツリレーするだけでは、if文の中のreturnが「if式自体の値」に
       化けてしまい、それに続く文が実行され続けてしまう(既知の修正済みバグ)。 */
    int returning;
    ComptimeValue return_value;
} ComptimeEvalCtx;

#define COMPTIME_MAX_CALL_DEPTH 64
#define COMPTIME_MAX_STEPS 200000
#define COMPTIME_MAX_LOOP_ITERS 100000

void          comptime_init(ComptimeEvalCtx *ctx, Scope *global);
ComptimeValue comptime_eval_expr(ComptimeEvalCtx *ctx, Expr *e);
ComptimeValue comptime_call_fn(ComptimeEvalCtx *ctx, Item *fn, ComptimeValue *args, int arg_count);
void          comptime_format_value(ComptimeValue v, char *buf, size_t bufsize);

#endif /* TMC_COMPTIME_H */
