#ifndef OS_ATOMIC_H
#define OS_ATOMIC_H

typedef struct {
    volatile int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

/* 读取 */
static inline int atomic_read(const atomic_t *v)
{
    return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST);
}

/* 写入 */
static inline void atomic_set(atomic_t *v, int i)
{
    __atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

/* 加 */
static inline void atomic_add(int i, atomic_t *v)
{
    __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}

/* 减 */
static inline void atomic_sub(int i, atomic_t *v)
{
    __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST);
}

/* ++ 并返回新值 */
static inline int atomic_inc_return(atomic_t *v)
{
    return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

/* -- 并返回新值 */
static inline int atomic_dec_return(atomic_t *v)
{
    return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

/* ++ */
static inline void atomic_inc(atomic_t *v)
{
    atomic_add(1, v);
}

/* -- */
static inline void atomic_dec(atomic_t *v)
{
    atomic_sub(1, v);
}

/* 减并测试是否为 0 */
static inline int atomic_dec_and_test(atomic_t *v)
{
    return atomic_dec_return(v) == 0;
}

/* 加并测试是否为 0 */
static inline int atomic_add_and_test(int i, atomic_t *v)
{
    return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST) == 0;
}

/* 比较并交换 */
static inline int atomic_cmpxchg(atomic_t *v, int old, int new_)
{
    __atomic_compare_exchange_n(
        &v->counter,
        &old,
        new_,
        0,
        __ATOMIC_SEQ_CST,
        __ATOMIC_SEQ_CST);
    return old;
}

/* xchg */
static inline int atomic_xchg(atomic_t *v, int new_)
{
    return __atomic_exchange_n(&v->counter, new_, __ATOMIC_SEQ_CST);
}

#endif
