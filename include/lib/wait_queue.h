#ifndef OS_WAIT_QUEUE_H
#define OS_WAIT_QUEUE_H

#include "lib/safelist.h"

typedef struct wait_queue {
    spin_lock_t lock;
    list_head_t list;   // 等待中的任务
} wait_queue_t;

static inline void wait_queue_init(wait_queue_t *wq)
{
    spin_lock_init(&wq->lock);
    INIT_LIST_HEAD(&wq->list);
}

void wake_up_all(wait_queue_t *wq);
void sleep_on(wait_queue_t *wq);

#endif