#ifndef OS_BITMAP_H
#define OS_BITMAP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t *data;   // 位图存储区
    size_t    nbits;  // 总bit数
    size_t    nwords; // uint64_t 数量
} bitmap_t;

static inline void bitmap_init(bitmap_t *bm,
                               uint64_t *buffer,
                               size_t nbits)
{
    bm->data   = buffer;
    bm->nbits  = nbits;
    bm->nwords = (nbits + 63) >> 6;

    // 清零
    for (size_t i = 0; i < bm->nwords; i++)
        bm->data[i] = 0;
}

static inline void bitmap_set(bitmap_t *bm, size_t idx)
{
    bm->data[idx >> 6] |= (1ULL << (idx & 63));
}

static inline void bitmap_clear(bitmap_t *bm, size_t idx)
{
    bm->data[idx >> 6] &= ~(1ULL << (idx & 63));
}

static inline int bitmap_test(bitmap_t *bm, size_t idx)
{
    return (bm->data[idx >> 6] >> (idx & 63)) & 1ULL;
}

static inline size_t bitmap_find_zero(bitmap_t *bm)
{
    for (size_t i = 0; i < bm->nwords; i++)
    {
        if (~bm->data[i])  // 说明存在0 bit
        {
            uint64_t inv = ~bm->data[i];
            size_t bit = __builtin_ctzll(inv);
            size_t idx = (i << 6) + bit;

            if (idx < bm->nbits)
                return idx;
        }
    }

    return (size_t)-1L;  // 无空闲
}

#endif
