/* ast.h — Tenmu (tmc0) AST定義
   方針: 個々のノード種別ごとに深くネストした共用体を作る代わりに、
   種別(kind)ごとに意味が決まる少数の汎用フィールド(a/b/c, list, name等)を
   共有する「プラグマティックな」ノード表現を使う。フィールドの意味は
   各kindの定義コメントに明記する。AST用メモリは明示的にfreeしない
   (プロセス終了まで生存する短命コンパイラという前提の意図的な単純化)。 */
#ifndef TMC_AST_H
#define TMC_AST_H

#include <stddef.h>

typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Item Item;

/* ===== 型 ===== */
typedef enum {
    TY_PATH,     /* 名前(+ジェネリクス引数、+ ".seg" のパス) 例: i32, Vec<T>, http.Request */
    TY_PTR,      /* *T / *mut T */
    TY_REF,      /* &T / &mut T */
    TY_ARRAY,    /* [N]T (size!=NULL) または []T スライス (size==NULL) */
    TY_TUPLE,    /* (T1, T2, ...) */
    TY_TENSOR,   /* Tensor<T, [dims...]> */
    TY_OPTIONAL, /* ?T */
    TY_NEVER,    /* ! */
    TY_FN,       /* fn(T1,T2) -> T3 */
} TypeKind;

struct Type {
    TypeKind kind;
    int line, col;

    /* TY_PATH */
    char **segments; int segment_count;     /* 例: ["http","Request"] */
    Type **generic_args; int generic_arg_count;

    /* TY_PTR / TY_REF / TY_ARRAY(要素型) / TY_OPTIONAL(中身) */
    Type *inner;
    int is_mut;

    /* TY_ARRAY: サイズ式(スライスならNULL) */
    Expr *array_size;

    /* TY_TUPLE / TY_FN(パラメータ型) */
    Type **items; int item_count;

    /* TY_TENSOR: 要素型はinner、次元式のリスト */
    Expr **dims; int dim_count;

    /* TY_FN: 戻り値型 */
    Type *ret;
};

/* ===== 式 ===== */
typedef enum {
    EX_INT, EX_FLOAT, EX_STRING, EX_STRING_INTERP, EX_CHAR,
    EX_TRUE, EX_FALSE, EX_NULL, EX_VOID,
    EX_IDENT, EX_SELF,
    EX_BINARY, EX_UNARY, EX_ASSIGN, EX_CAST,
    EX_CALL, EX_INDEX, EX_FIELD, EX_TRY_POSTFIX,
    EX_PAREN, EX_BLOCK, EX_IF, EX_MATCH, EX_CLOSURE, EX_UNSAFE, EX_TRY_CATCH,
    EX_STRUCT_LITERAL, EX_ARRAY_LITERAL, EX_TUPLE_LITERAL,
    EX_FOR, EX_WHILE, EX_LOOP,
    /* EX_FOR:   str=ループ変数名 a=イテレート対象式 b=本体(EX_BLOCK)
       EX_WHILE: a=条件 b=本体(EX_BLOCK)
       EX_LOOP:  a=本体(EX_BLOCK) */
    EX_NAMED_ARG,
    /* EX_NAMED_ARG: 呼び出し引数 "label: value" 。str=ラベル名 b=値 */
} ExprKind;

/* 文字列補間の1パーツ: リテラル文字列の断片、またはパーツの間に挟まる式 */
typedef struct {
    const char *text; size_t text_len;  /* リテラル部分(常に有効。式パーツはtext_len=0) */
    Expr *expr;                          /* NULLならリテラル部分のみ */
} InterpPart;

typedef struct { char *name; Expr *value; } StructFieldInit;   /* Foo { name: value } */
typedef struct { Expr *pattern; Expr *body; } MatchArm;         /* パターンは簡略化しExprとして表現 */
typedef struct { char *name; Type *type; } ClosureParam;

struct Expr {
    ExprKind kind;
    int line, col;

    long long int_val;
    double float_val;
    const char *str; size_t str_len;   /* 識別子名/演算子スペル/文字列内容など */

    Expr *a, *b, *c;
    /* EX_BINARY:  a=左辺 b=右辺 str=演算子
       EX_UNARY:   a=オペランド str=演算子
       EX_ASSIGN:  a=左辺 b=右辺 str=演算子("=","+=",...)
       EX_CAST:    a=対象式 type=キャスト先
       EX_CALL:    a=呼び出し対象 list=引数(Expr*)
       EX_INDEX:   a=対象 b=添字
       EX_FIELD:   a=対象 str=フィールド/メソッド名 list=メソッド引数(NULLなら単純フィールドアクセス)
       EX_TRY_POSTFIX: a=対象("expr?")
       EX_PAREN:   a=中身
       EX_IF:      a=条件 b=then節(EX_BLOCK) c=else節(NULL可。EX_BLOCKまたはネストしたEX_IF)
       EX_UNSAFE:  a=中身(EX_BLOCK)
       EX_TRY_CATCH: a=tryのブロック str=catch変数名 b=catchのブロック
       EX_CLOSURE: a=本体(式またはEX_BLOCK) params/param_count=引数 type=戻り値型注釈(NULL可)
       EX_STRUCT_LITERAL: str=構造体名 field_inits/field_init_count
       EX_MATCH:   a=対象 arms/arm_count */
    Expr **list; int list_count;                 /* 呼び出し引数、配列/タプルリテラル要素、ブロックの文はStmt側で保持 */
    Stmt **stmts; int stmt_count;                 /* EX_BLOCK: 文のリスト */
    ClosureParam *params; int param_count;        /* EX_CLOSURE */
    Type *type;                                    /* EX_CAST/EX_CLOSURE戻り値型 */
    StructFieldInit *field_inits; int field_init_count; /* EX_STRUCT_LITERAL */
    MatchArm *arms; int arm_count;                 /* EX_MATCH */
    InterpPart *interp_parts; int interp_part_count; /* EX_STRING_INTERP */
};

/* ===== 文 ===== */
typedef enum { ST_LET, ST_EXPR, ST_RETURN, ST_BREAK, ST_CONTINUE, ST_DEFER, ST_ITEM } StmtKind;

struct Stmt {
    StmtKind kind;
    int line, col;
    char *name;          /* ST_LET (単純束縛): 変数名 */
    char **names; int name_count; /* ST_LET (タプル分解 let (a,b)=...): 変数名の並び。使う場合はnameはNULL */
    int is_mut;           /* ST_LET */
    Type *type_ann;        /* ST_LET: 型注釈(NULL可、タプル分解では現状非対応) */
    Expr *expr;              /* ST_LET(初期化式) / ST_EXPR / ST_RETURN(NULL可) / ST_DEFER */
    Item *item;                /* ST_ITEM */
};

/* ===== 宣言(アイテム) ===== */
typedef enum { IT_MODULE, IT_IMPORT, IT_FN, IT_STRUCT, IT_ENUM, IT_UNION, IT_TRAIT, IT_IMPL, IT_ERROR } ItemKind;

typedef struct { char *name; Type *type; Expr *default_value; } Param;
typedef struct { char *name; int is_const; Type *const_type; Type **bounds; int bound_count; } GenericParam;
typedef struct { char *name; Type *type; int is_pub; } Field;
typedef struct {
    char *name;
    Type **tuple_types; int tuple_type_count;   /* Variant(T1, T2) 形式 */
    Field *struct_fields; int struct_field_count; /* Variant { a: T, ... } 形式 */
} Variant;
typedef struct { char *name; char **args; int arg_count; } Attribute;

struct Item {
    ItemKind kind;
    int line, col;
    int is_pub;
    char *name;

    Attribute *attrs; int attr_count;

    /* IT_MODULE / IT_IMPORT */
    char **path_segments; int path_segment_count;
    char *alias;

    /* IT_FN */
    GenericParam *generics; int generic_count;
    Param *params; int param_count;
    Type *return_type;
    Expr *body;              /* NULLなら宣言のみ(externのプロトタイプ等) */
    int is_comptime, is_async, is_extern;
    char *extern_abi;

    /* IT_STRUCT */
    Field *fields; int field_count;

    /* IT_ENUM / IT_ERROR */
    Variant *variants; int variant_count;

    /* IT_TRAIT / IT_IMPL: メソッドは IT_FN の Item として保持 */
    Item **methods; int method_count;
    Type *impl_type;
    Type *impl_trait_type;   /* impl Trait for Type の Trait 部分。無ければNULL */
};

typedef struct { Item **items; int item_count; } Program;

#endif /* TMC_AST_H */
