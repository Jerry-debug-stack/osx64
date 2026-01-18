#include "lib/safelist.h"
#include "lib/io.h"

/* ====================== 自旋锁实现 ====================== */

/* 自旋锁初始化 */
void spin_lock_init(spinlock_t *lock)
{
    lock->lock = 0;
}

/* 获取自旋锁（忙等待） */
void spin_lock(spinlock_t *lock)
{
    uint8_t intr = io_cli();
    while (atomic_compare_exchange((uint32_t*)&lock->lock,0,1) == 1) {
        __asm__ __volatile__("pause");
    }
    io_set_intr(intr);
    __sync_synchronize();
}

/* 释放自旋锁 */
void spin_unlock(spinlock_t *lock)
{
    /* 内存屏障 */
    __sync_synchronize();
    /* 原子释放 */
    __sync_lock_release(&lock->lock);
}

/* 尝试获取自旋锁（非阻塞） */
int spin_trylock(spinlock_t *lock)
{
    return !atomic_compare_exchange((uint32_t*)&lock->lock,0,1);
}

/* ====================== 互斥锁实现 ====================== */

/* 等待队列节点 */
typedef struct wait_queue_entry {
    struct task_struct *task;  // 等待的任务
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
    /* 尝试快速获取 */
    if (__sync_bool_compare_and_swap(&lock->locked, 0, 1)) {
        return;
    }
    
    /* 创建等待队列项 */
    wait_queue_entry_t wait;
    //wait.task = current_task();  // 获取当前任务
    
    /* 加入等待队列 */
    spin_lock(&lock->queue_lock);
    
    if (lock->locked) {
        /* 添加到等待队列尾部 */
        list_add_tail(&wait.list, &lock->wait_queue);
        
        ///@todo sleep!
        /* 设置任务状态为睡眠 */
        //task_set_state(wait.task, TASK_SLEEPING);
        
        /* 释放队列锁并调度其他任务 */
        spin_unlock(&lock->queue_lock);
        //schedule();
        
        /* 被唤醒后，重新尝试获取锁 */
        while (__sync_lock_test_and_set(&lock->locked, 1)) {
            //schedule();
        }
    } else {
        /* 在这段时间内锁被释放了，直接获取 */
        lock->locked = 1;
        spin_unlock(&lock->queue_lock);
    }
}

/* 释放互斥锁 */
void mutex_unlock(mutex_t *lock)
{
    /* 清除锁标志 */
    __sync_lock_release(&lock->locked);
    
    /* 检查等待队列 */
    spin_lock(&lock->queue_lock);
    if (!list_empty(&lock->wait_queue)) {
        /* 唤醒第一个等待者 */
        struct wait_queue_entry *wait;
        wait = list_first_entry(&lock->wait_queue, 
                                struct wait_queue_entry, list);
        list_del(&wait->list);
        
        /* 唤醒任务 */
        ///@todo wake!!!
        //task_set_state(wait->task, TASK_RUNNING);
        //schedule_task(wait->task);
    }
    spin_unlock(&lock->queue_lock);
}

/* 尝试获取互斥锁（非阻塞） */
int mutex_trylock(mutex_t *lock)
{
    return __sync_bool_compare_and_swap(&lock->locked, 0, 1);
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
        //schedule();  // 让出CPU
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
        //schedule();  // 让出CPU
    }
}

/* 释放写锁 */
void write_unlock(rwlock_t *lock)
{
    spin_lock(&lock->lock);
    lock->writer = 0;
    spin_unlock(&lock->lock);
}
