/* util.h — 共通ユーティリティ: OOMチェック付きアロケータと汎用可変長配列 */
#ifndef TMC_UTIL_H
#define TMC_UTIL_H

#include <stddef.h>

void  *xmalloc(size_t size);
void  *xrealloc(void *p, size_t size);
char  *xstrndup(const char *s, size_t n);

/* 型を問わない可変長配列。要素はmemcpyで格納するため、
   ポインタや小さなPOD構造体の配列として使う(要素自身がmallocされたポインタなら
   dynarray自体は所有権を持たず、単に「ポインタの配列」を保持するだけになる)。 */
typedef struct {
    void  *data;
    size_t elem_size;
    int    count;
    int    cap;
} DynArray;

void  dynarray_init(DynArray *a, size_t elem_size);
void  dynarray_push(DynArray *a, const void *elem);
/* dynarray_take: 内部バッファの所有権を呼び出し側に渡し、countを返す。
   Item配列などをAST構造体へ格納する際、以後dynarray_push等は呼ばないこと。 */
void *dynarray_take(DynArray *a, int *out_count);

#endif /* TMC_UTIL_H */
