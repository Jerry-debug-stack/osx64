#ifndef OS_SAFE_LIST_H
#define OS_SAFE_LIST_H

#include "lib/my_list.h"
#include <stdint.h>

/* ====================== 锁类型定义 ====================== */

/* 自旋锁（用于短临界区，不可中断） */
typedef struct {
    volatile uint32_t lock;
} spin_lock_t;

/* 简化自旋锁,可中断 */
typedef struct {
    volatile uint32_t lock;
} spin_lock_int_able_t;

/* 互斥锁（用于可能睡眠的长临界区） */
typedef struct {
    volatile int locked;
    list_head_t wait_queue;  // 等待队列
    spin_lock_t queue_lock;        // 等待队列锁
} mutex_t;

/* 读写锁（读多写少场景） */
typedef struct {
    volatile int readers;
    volatile int writer;
    spin_lock_t lock;
} rwlock_t;


/* 自旋锁操作 */
void spin_lock_init(spin_lock_t *lock);
void spin_lock(spin_lock_t *lock);
void spin_unlock(spin_lock_t *lock);
int spin_trylock(spin_lock_t *lock);

/* 可中断自旋锁操作 */
void spin_lock_int_able_init(spin_lock_int_able_t *lock);
void spin_lock_int_able(spin_lock_int_able_t *lock);
void spin_int_able_unlock(spin_lock_int_able_t *lock);
int spin_int_able_trylock(spin_lock_int_able_t *lock);

/* 互斥锁操作 */
void mutex_init(mutex_t *lock);
void mutex_lock(mutex_t *lock);
void mutex_unlock(mutex_t *lock);
int mutex_trylock(mutex_t *lock);

/* 读写锁操作 */
void rwlock_init(rwlock_t *lock);
void read_lock(rwlock_t *lock);
void read_unlock(rwlock_t *lock);
void write_lock(rwlock_t *lock);
void write_unlock(rwlock_t *lock);

/* 带自旋锁的链表（最常用） */
typedef struct spin_list_head {
    list_head_t list;
    spin_lock_t lock;
} spin_list_head_t;

/* 带互斥锁的链表 */
typedef struct mutex_list_head {
    list_head_t list;
    mutex_t lock;
} mutex_list_head_t;

/* 带读写锁的链表 */
typedef struct rw_list_head {
    list_head_t list;
    rwlock_t lock;
} rw_list_head_t;

/* ====================== 初始化宏 ====================== */

/* 初始化带自旋锁的链表 */
#define SPIN_LIST_HEAD_INIT(name) {          \
    .list = LIST_HEAD_INIT(name.list),       \
    .lock = { .lock = 0 }                    \
}

#define SPIN_LIST_HEAD(name) \
    spin_list_head_t name = SPIN_LIST_HEAD_INIT(name)

/* 初始化带互斥锁的链表 */
#define MUTEX_LIST_HEAD_INIT(name) {         \
    .list = LIST_HEAD_INIT(name.list),       \
    .lock = {                                \
        .locked = 0,                         \
        .wait_queue = LIST_HEAD_INIT(name.lock.wait_queue), \
        .queue_lock = { .lock = 0 }          \
    }                                        \
}

#define MUTEX_LIST_HEAD(name) \
    mutex_list_head_t name = MUTEX_LIST_HEAD_INIT(name)

/* ====================== 带锁操作API ====================== */

/* ========== 自旋锁链表操作 ========== */

/* 初始化自旋锁链表 */
static inline void spin_list_init(spin_list_head_t *head)
{
    INIT_LIST_HEAD(&head->list);
    spin_lock_init(&head->lock);
}

/* 带锁添加节点 */
static inline void spin_list_add(list_head_t *new_, 
                                 spin_list_head_t *head)
{
    spin_lock(&head->lock);
    list_add(new_, &head->list);
    spin_unlock(&head->lock);
}

static inline void spin_list_add_tail(list_head_t *new_, 
                                      spin_list_head_t *head)
{
    spin_lock(&head->lock);
    list_add_tail(new_, &head->list);
    spin_unlock(&head->lock);
}

/* 带锁删除节点 */
static inline void spin_list_del(list_head_t *entry, 
                                 spin_list_head_t *head)
{
    spin_lock(&head->lock);
    list_del(entry);
    spin_unlock(&head->lock);
}

static inline void spin_list_del_init(list_head_t *entry, 
                                      spin_list_head_t *head)
{
    spin_lock(&head->lock);
    list_del_init(entry);
    spin_unlock(&head->lock);
}

/* 带锁移动节点 */
static inline void spin_list_move(list_head_t *list, 
                                  spin_list_head_t *head)
{
    spin_lock(&head->lock);
    list_move(list, &head->list);
    spin_unlock(&head->lock);
}

/* 带锁判断是否为空 */
static inline int spin_list_empty(spin_list_head_t *head)
{
    int empty;
    spin_lock(&head->lock);
    empty = list_empty(&head->list);
    spin_unlock(&head->lock);
    return empty;
}

/* 尝试获取锁并操作（非阻塞） */
static inline int spin_list_try_add(list_head_t *new_,
                                    spin_list_head_t *head)
{
    if (spin_trylock(&head->lock)) {
        list_add(new_, &head->list);
        spin_unlock(&head->lock);
        return 1;
    }
    return 0;
}

/* ====================== 安全遍历宏 ====================== */

/* ========== 自旋锁安全遍历 ========== */

/* 遍历整个链表时持有锁（适合短链表） */
#define spin_list_for_each(pos, head) \
    for (spin_lock(&(head)->lock), \
         pos = (head)->list.next; \
         pos != &(head)->list; \
         pos = pos->next)

/* 安全遍历，自动解锁 */
#define spin_list_for_each_safe(pos, n, head) \
    for (spin_lock(&(head)->lock), \
         pos = (head)->list.next, n = pos->next; \
         pos != &(head)->list; \
         pos = n, n = pos->next)

/* 遍历后自动解锁 */
#define spin_list_for_each_unlocked(pos, head) \
    for (spin_lock(&(head)->lock), \
         pos = (head)->list.next; \
         ({ int __cond = (pos != &(head)->list); \
            if (!__cond) spin_unlock(&(head)->lock); \
            __cond; }); \
         pos = pos->next)

/* 遍历entry并持有锁 */
#define spin_list_for_each_entry(pos, head, member) \
    for (spin_lock(&(head)->lock), \
         pos = container_of((head)->list.next, typeof(*pos), member); \
         &pos->member != &(head)->list; \
         pos = container_of(pos->member.next, typeof(*pos), member))

/* 安全遍历entry（允许在循环内删除） */
#define spin_list_for_each_entry_safe(pos, n, head, member) \
    for (spin_lock(&(head)->lock), \
         pos = container_of((head)->list.next, typeof(*pos), member), \
         n = container_of(pos->member.next, typeof(*pos), member); \
         &pos->member != &(head)->list; \
         pos = n, n = container_of(n->member.next, typeof(*n), member))

/* 遍历后自动解锁的版本 */
#define spin_list_for_each_entry_unlocked(pos, head, member) \
    for (spin_lock(&(head)->lock), \
         pos = container_of((head)->list.next, typeof(*pos), member); \
         ({ int __cond = (&pos->member != &(head)->list); \
            if (!__cond) spin_unlock(&(head)->lock); \
            __cond; }); \
         pos = container_of(pos->member.next, typeof(*pos), member))

/* ========== 读写锁安全遍历 ========== */

/* 读遍历（允许多个读者） */
#define rw_list_for_each_entry_read(pos, head, member) \
    for (read_lock(&(head)->lock), \
         pos = container_of((head)->list.next, typeof(*pos), member); \
         ({ int __cond = (&pos->member != &(head)->list); \
            if (!__cond) read_unlock(&(head)->lock); \
            __cond; }); \
         pos = container_of(pos->member.next, typeof(*pos), member))

/* 写遍历（独占访问） */
#define rw_list_for_each_entry_write(pos, head, member) \
    for (write_lock(&(head)->lock), \
         pos = container_of((head)->list.next, typeof(*pos), member); \
         ({ int __cond = (&pos->member != &(head)->list); \
            if (!__cond) write_unlock(&(head)->lock); \
            __cond; }); \
         pos = container_of(pos->member.next, typeof(*pos), member))

/* ====================== 条件操作 ====================== */

/* 条件添加：只有在条件满足时才添加 */
static inline int spin_list_add_if(list_head_t *new_,
                                   spin_list_head_t *head,
                                   int (*condition)(void *),
                                   void *arg)
{
    int ret = 0;
    spin_lock(&head->lock);
    if (condition(arg)) {
        list_add(new_, &head->list);
        ret = 1;
    }
    spin_unlock(&head->lock);
    return ret;
}

/* 查找并删除符合条件的节点 */
static inline int spin_list_remove_if(spin_list_head_t *head,
                                      int (*condition)(void *, list_head_t *),
                                      void *arg)
{
    int removed = 0;
    list_head_t *pos, *n;
    
    spin_lock(&head->lock);
    list_for_each_safe(pos, n, &head->list) {
        if (condition(arg, pos)) {
            list_del(pos);
            removed++;
        }
    }
    spin_unlock(&head->lock);
    return removed;
}


#endif
