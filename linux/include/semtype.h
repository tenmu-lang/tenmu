/* semtype.h — Stage 2 (意味解析) が扱う「解決済みの型」。
   ast.h の Type は構文木そのまま(パス名がまだ文字列)であるのに対し、
   SemType は名前解決を経て「実際に何を指すか」が確定した表現。 */
#ifndef TMC_SEMTYPE_H
#define TMC_SEMTYPE_H

#include "ast.h"

typedef enum {
    ST_I8, ST_I16, ST_I32, ST_I64, ST_I128, ST_ISIZE,
    ST_U8, ST_U16, ST_U32, ST_U64, ST_U128, ST_USIZE,
    ST_F16, ST_F32, ST_F64,
    ST_BOOL, ST_CHAR, ST_VOID, ST_NEVER,
    ST_STR, ST_STRING,
    ST_PTR, ST_REF, ST_ARRAY, ST_SLICE, ST_TUPLE, ST_TENSOR, ST_OPTIONAL, ST_FN,
    ST_STRUCT, ST_ENUM, ST_ERROR, ST_UNION, ST_TRAIT,
    ST_GENERIC_PARAM,   /* 宣言中のジェネリクス型引数(例: <T>のT)。他の同名GENERIC_PARAMとのみ一致する */
    ST_EXTERNAL,         /* importしたモジュールのメンバー等、実体不明の外部型。何にでも適合する */
    ST_UNKNOWN,           /* 推論前・エラー回復用。何にでも適合し、エラーの連鎖を防ぐ */
} SemTypeKind;

typedef struct SemType SemType;
struct SemType {
    SemTypeKind kind;
    SemType *inner;               /* PTR/REF/ARRAY/SLICE/OPTIONALの要素型 */
    int is_mut;
    SemType **items; int item_count; /* TUPLE要素 / FN引数型 */
    SemType *ret;                     /* FN戻り値型 */
    char *name;                        /* STRUCT/ENUM/ERROR/UNION/TRAIT/GENERIC_PARAMの名前 */
    Item *decl;                         /* 対応する宣言(フィールド/バリアント参照用、NULL可) */
};

SemType *semtype_primitive(SemTypeKind kind);
SemType *semtype_new(SemTypeKind kind);
SemType *semtype_unknown(void);
SemType *semtype_external(void);

/* a と b が「互換」であれば真を返す。UNKNOWN/EXTERNALはワイルドカードとして常に互換とみなす
   (実体が分からない/エラー済みの型からエラーが連鎖するのを防ぐため)。 */
int semtype_compatible(SemType *a, SemType *b);

/* 表示用の短い文字列を buf に書き込む(エラーメッセージ用)。 */
void semtype_format(SemType *t, char *buf, size_t bufsize);

#endif /* TMC_SEMTYPE_H */
