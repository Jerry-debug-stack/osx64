#ifndef OS_TIMER_H
#define OS_TIMER_H

#include "lib/my_list.h"
#include <stdint.h>
#include "task.h"
#include "lib/atomic.h"

enum timer_type_enum{
    TIMER_TASK_SING,
    TIMER_TASK_PERIODIC,
    TIMER_SYS_SING,
    TIMER_SYS_PERIODIC
};

typedef struct timer
{
    /* 时钟的下一次的ticks */
    uint64_t ticks;
    /* 周期性时钟的间隔 */
    uint32_t delta_ticks;
    enum timer_type_enum timer_type;
    list_head_t list_item;
    list_head_t in_task_item;
    pcb_t *task;
    uint32_t signal;
} timer_t;

// local_timer_timeout的返回取值约定：位标志
void mdelay(uint64_t ms);
void hpet_udelay(uint64_t us);

extern atomic_64_t unix_time;
static inline uint64_t get_time(void){
    return atomic_64_read(&unix_time);
}

#endif
