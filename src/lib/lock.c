#include "lib/safelist.h"
#include "lib/io.h"
#include "task.h"

/* ====================== 自旋锁实现 ====================== */

extern bool multi_core_start;

/* 自旋锁初始化 */
void spin_lock_init(spinlock_t *lock)
{
    lock->lock = 0;
}

/* 获取自旋锁（忙等待） */
void spin_lock(spinlock_t *lock)
{
    if (multi_core_start)
        preempt_disable();
    while (atomic_compare_exchange((uint32_t*)&lock->lock,0,1) == 1) {
        __asm__ __volatile__("pause");
    }
    __sync_synchronize();
}

/* 释放自旋锁 */
void spin_unlock(spinlock_t *lock)
{
    /* 内存屏障 */
    __sync_synchronize();
    /* 原子释放 */
    __sync_lock_release(&lock->lock);
    if (multi_core_start)
        preempt_enable();
}

/* 尝试获取自旋锁（非阻塞） */
int spin_trylock(spinlock_t *lock)
{
    return !atomic_compare_exchange((uint32_t*)&lock->lock,0,1);
}

uint8_t spin_lock_irq_save(spinlock_t *lock){
    uint8_t intr = io_cli();
    if (multi_core_start)
        preempt_disable();
    while (atomic_compare_exchange((uint32_t*)&lock->lock,0,1) == 1) {
        __asm__ __volatile__("pause");
    }
    __sync_synchronize();
    return intr;
}

void spin_lock_irq_able(spinlock_t *lock){
    spin_lock(lock);
    if (multi_core_start)
        preempt_enable();
}

void spin_unlock_irq_able(spinlock_t *lock){
    if (multi_core_start)
        preempt_disable();
    spin_unlock(lock);
}

/* ====================== 互斥锁实现 ====================== */

/* 等待队列节点 */
typedef struct wait_queue_entry {
    pcb_t *task;  // 等待的任务
    list_head_t list;     // 链表节点
} wait_queue_entry_t;

/* 初始化互斥锁 */
void mutex_init(mutex_t *lock)
{
    lock->locked = 0;
    INIT_LIST_HEAD(&lock->wait_queue);
    spin_lock_init(&lock->queue_lock);
}

/* 获取互斥锁（可能睡眠） */
void mutex_lock(mutex_t *lock)
{
    spin_lock(&lock->queue_lock);
    /* 尝试快速获取 */
    if (__sync_bool_compare_and_swap(&lock->locked, 0, 1)) {
        spin_unlock(&lock->queue_lock);
        return;
    }
    
    /* 创建等待队列项 */
    wait_queue_entry_t wait;
    wait.task = get_current();  // 获取当前任务
    
    uint32_t intr = io_cli();
    pcb_t *current = get_current();
    list_add_tail(&wait.list, &lock->wait_queue);
    current->state = TASK_STATE_SLEEP_NOT_INTR_ABLE;
    __schedule_other_locked(&lock->queue_lock);
    io_set_intr(intr);
    
    /* 被唤醒后，重新尝试获取锁 */
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        schedule();
    }
}

/* 释放互斥锁 */
void mutex_unlock(mutex_t *lock)
{
    /* 检查等待队列 */
    spin_lock(&lock->queue_lock);
    lock->locked = 0;
    if (!list_empty(&lock->wait_queue)) {
        /* 唤醒第一个等待者 */
        struct wait_queue_entry *wait;
        wait = list_first_entry(&lock->wait_queue, 
                                struct wait_queue_entry, list);
        list_del(&wait->list);
        put_to_ready_list_first(wait->task);
    }
    spin_unlock(&lock->queue_lock);
}

/* 尝试获取互斥锁（非阻塞） */
int mutex_trylock(mutex_t *lock)
{
    spin_lock(&lock->queue_lock);
    int ret = __sync_bool_compare_and_swap(&lock->locked, 0, 1);
    spin_unlock(&lock->queue_lock);
    return ret;
}

/* ====================== 读写锁实现 ====================== */

/* 初始化读写锁 */
void rwlock_init(rwlock_t *lock)
{
    lock->readers = 0;
    lock->writer = 0;
    spin_lock_init(&lock->lock);
}

/* 获取读锁 */
void read_lock(rwlock_t *lock)
{
    for (;;) {
        spin_lock(&lock->lock);
        if (!lock->writer) {
            lock->readers++;
            spin_unlock(&lock->lock);
            break;
        }
        spin_unlock(&lock->lock);
        sys_yield();
    }
}

/* 释放读锁 */
void read_unlock(rwlock_t *lock)
{
    spin_lock(&lock->lock);
    lock->readers--;
    spin_unlock(&lock->lock);
}

/* 获取写锁 */
void write_lock(rwlock_t *lock)
{
    for (;;) {
        spin_lock(&lock->lock);
        if (!lock->writer && lock->readers == 0) {
            lock->writer = 1;
            spin_unlock(&lock->lock);
            break;
        }
        spin_unlock(&lock->lock);
        sys_yield();
    }
}

/* 释放写锁 */
void write_unlock(rwlock_t *lock)
{
    spin_lock(&lock->lock);
    lock->writer = 0;
    spin_unlock(&lock->lock);
}
