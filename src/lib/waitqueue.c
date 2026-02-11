#include <stdint.h>
#include "const.h"
#include "lib/wait_queue.h"
#include "task.h"
#include "lib/io.h"

void sleep_on(wait_queue_t *wq)
{
    uint32_t intr = io_cli();
    pcb_t *current = get_current();
    spin_lock(&wq->lock);
    list_add_tail(&current->wait_list_item, &wq->list);
    current->state = TASK_STATE_SLEEP_NOT_INTR_ABLE;
    spin_unlock(&wq->lock);
    schedule();
    io_set_intr(intr);
}

void wake_up_all(wait_queue_t *wq)
{
    spin_lock(&wq->lock);
    while (!list_empty(&wq->list)) {
        list_head_t *pos = wq->list.next;
        list_del_init(pos);
        pcb_t *task = container_of(pos, pcb_t, wait_list_item);
        task->state = TASK_STATE_READY;
        put_to_ready_list_first(task);
    }
    spin_unlock(&wq->lock);
}

