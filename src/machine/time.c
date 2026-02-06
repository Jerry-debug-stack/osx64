#include <stdint.h>
#include "const.h"
#include "lib/timer.h"
#include "machine/cpu.h"
#include "mm/mm.h"

#define RELOAD_TICKS (1193182 / 1000)

extern GLOBAL_CPU *cpus;

extern void set_handler(uint64_t irq, uint64_t addr);
extern void set_EOI(void);
extern void disable_irq(uint64_t irq);
extern void enable_irq(uint64_t irq);
extern uint32_t get_logic_cpu_id(void);
extern void schedule(enum task_state to_state);

uint64_t ticks;

void timer_intr_soft(void);
static inline uint32_t local_timer_timeout(CPU_ITEM *cpu);
static void add_timer(enum timer_type_enum timer_type,uint64_t first_ticks,uint32_t delta_ticks,pcb_t *task,uint32_t signal);
static void load_balance(CPU_ITEM *now_cpu);

void init_time(void)
{
    ticks = 0;
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x43), "al"(0x34) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x40), "al"((RELOAD_TICKS) & 0xFF) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x40), "al"((RELOAD_TICKS) >> 8) :);
    for (uint32_t i = 0; i < cpus->total_num; i++)
    {
        CPU_ITEM* cpu = &cpus->items[i];
        cpu->time_intr_reenter = 0;
        spin_list_init(&cpu->timer_list);
    }
    
    /* 设置AP核对中断的处理程序 */
    set_handler(2, (uint64_t)timer_intr_soft);
    enable_irq(2);
}


void timer_intr_soft(void){
    /* 进入时关着中断 */
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *cpu = &cpus->items[id];
    set_EOI();
    if (cpu->time_intr_reenter){
        return;
    }
    cpu->time_intr_reenter++;
    /* 调度请求标志 */
    bool need_schedule = true;
    /* step 1 处理本地时钟 */ 
    uint32_t signal = local_timer_timeout(cpu);
    /* step 2 分析负载均衡 */
    if ((ticks + id) & 64){
        load_balance(cpu);
    }
    /* step 3 考虑是否需要调度 */
    /* 这里保留作以后处理 */
    __asm__ __volatile__("sti");
    /* 下半段 */
    __asm__ __volatile__("cli");
    /* 结束段 */
    cpu->time_intr_reenter--;
    if (need_schedule){
        schedule(TASK_STATE_READY);
    }
    /* 判断信号递送 */

}

void timer_intr_soft_bsp(void){
    ticks++;
    timer_intr_soft();
}

static void append_to_task_timer_list(spin_list_head_t *timer_list,timer_t *timer);
static void append_to_cpu_timer_list(spin_list_head_t *timer_list,timer_t *timer);

/**
 * @note 返回值的约定需要看include/lib/timer.h
 */
static inline uint32_t local_timer_timeout(CPU_ITEM *cpu){
    uint32_t flags = 0;
    spin_lock(&cpu->ready_list.lock);
    spin_lock(&cpu->timer_list.lock);
    while (!list_empty(&cpu->timer_list.list))
    {
        list_head_t *next_list_item = cpu->timer_list.list.next;
        timer_t *timer = container_of(next_list_item,timer_t,list_item);
        if (timer->ticks > ticks){
            break;
        }
        list_del(next_list_item);
        list_del(&timer->in_task_item);
        if (timer->timer_type == TIMER_TASK_PERIODIC){
            timer->task->signal |= timer->signal;
            timer->ticks += timer->delta_ticks;
            append_to_cpu_timer_list(&cpu->timer_list,timer);
            append_to_task_timer_list(&timer->task->timers,timer);
        }else if (timer->timer_type == TIMER_TASK_SING)
        {
            timer->task->signal |= timer->signal;
            kfree(timer);
        }else if (timer->timer_type == TIMER_SYS_SING)
        {
            flags |= timer->signal;
            kfree(timer);
        }else{
            flags |= timer->signal;
            timer->ticks += timer->delta_ticks;
            append_to_cpu_timer_list(&cpu->timer_list,timer);
        }
    }
    spin_unlock(&cpu->ready_list.lock);
    spin_unlock(&cpu->timer_list.lock);
    return flags;
}

static void append_to_task_timer_list(spin_list_head_t *timer_list,timer_t *timer){
    list_head_t *target = timer_list->list.prev;
    list_head_t *pos;
    list_for_each(pos,&timer_list->list){
        timer_t *tmp = container_of(pos,timer_t,in_task_item);
        if (tmp->ticks > timer->ticks){
            target = tmp->in_task_item.prev;
            break;
        }
    }
    list_add(&timer->in_task_item,target);
}

static void append_to_cpu_timer_list(spin_list_head_t *timer_list,timer_t *timer){
    list_head_t *target = timer_list->list.prev;
    list_head_t *pos;
    list_for_each(pos,&timer_list->list){
        timer_t *tmp = container_of(pos,timer_t,list_item);
        if (tmp->ticks > timer->ticks){
            target = tmp->list_item.prev;
            break;
        }
    }
    list_add(&timer->list_item,target);
}

static void add_timer(enum timer_type_enum timer_type,uint64_t first_ticks,uint32_t delta_ticks,pcb_t *task,uint32_t signal){
    timer_t *timer = kmalloc(sizeof(timer_t));
    timer->timer_type = timer_type;
    timer->ticks = first_ticks + ticks;
    timer->delta_ticks = delta_ticks;
    timer->task = task;
    timer->signal = signal;
    if (timer_type == TIMER_TASK_PERIODIC || timer_type == TIMER_TASK_SING){
        append_to_task_timer_list(&task->timers,timer);
    }
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *cpu = &cpus->items[id];
    append_to_cpu_timer_list(&cpu->timer_list,timer);
}

/**
 * @brief 用于在CPU之间迁移ready进程
 * @note 这只是对于ready列表中的进程
 * @warning 必须要关中断执行,而且需要持有两个CPU的进程列表锁和定时器列表锁
 */
static void task_timer_travel(pcb_t *task,CPU_ITEM *from_cpu,CPU_ITEM *to_cpu){
    spin_list_head_t *to_cpu_timer_list = &to_cpu->timer_list;
    list_head_t *pos;
    list_for_each(pos,&task->timers.list){
        timer_t *now_timer = container_of(pos,timer_t,in_task_item);
        list_del_init(&now_timer->list_item);
        append_to_cpu_timer_list(to_cpu_timer_list,now_timer);
    }
    list_del_init(&task->ready_list_item);
    list_add_tail(&task->ready_list_item,&to_cpu->ready_list.list);
    from_cpu->total_ready_num--;
    to_cpu->total_ready_num++;
}

static inline void alloc_load_balance_lock(CPU_ITEM *cpu1,CPU_ITEM *cpu2){
    if ((uint64_t)cpu1 < (uint64_t)cpu2){
        spin_lock(&cpu1->ready_list.lock);
        spin_lock(&cpu1->timer_list.lock);
        spin_lock(&cpu2->ready_list.lock);
        spin_lock(&cpu2->timer_list.lock);
    }else{
        spin_lock(&cpu2->ready_list.lock);
        spin_lock(&cpu2->timer_list.lock);
        spin_lock(&cpu1->ready_list.lock);
        spin_lock(&cpu1->timer_list.lock);
    }
}

static inline void unlock_load_balance_lock(CPU_ITEM *cpu1,CPU_ITEM *cpu2){
    if ((uint64_t)cpu1 < (uint64_t)cpu2){
        spin_unlock(&cpu1->ready_list.lock);
        spin_unlock(&cpu1->timer_list.lock);
        spin_unlock(&cpu2->ready_list.lock);
        spin_unlock(&cpu2->timer_list.lock);
    }else{
        spin_unlock(&cpu2->ready_list.lock);
        spin_unlock(&cpu2->timer_list.lock);
        spin_unlock(&cpu1->ready_list.lock);
        spin_unlock(&cpu1->timer_list.lock);
    }
}

static void tasks_travel(CPU_ITEM *from_cpu,CPU_ITEM *to_cpu){
    if (!from_cpu || !to_cpu || from_cpu == to_cpu) return;
    alloc_load_balance_lock(from_cpu,to_cpu);
    uint32_t from_load = from_cpu->total_ready_num;
    uint32_t to_load = to_cpu->total_ready_num;
    if ((from_load <= to_load)|| (((from_load-to_load) >> 1) < MULTI_CORE_BALANCE_DELTA) ){
        /* 这里直接将粒度设为 MULTI_CORE_BALANCE_DELTA,认为差异小于它将没有迁移必要 */
        goto end;
    }
    list_head_t *cpu_task_list = &from_cpu->ready_list.list;
    for (uint32_t i = 0; i < MULTI_CORE_BALANCE_DELTA; i++)
    {
        pcb_t *task = container_of(cpu_task_list->prev,pcb_t,ready_list_item);
        task_timer_travel(task,from_cpu,to_cpu);
    }
end:
    unlock_load_balance_lock(from_cpu,to_cpu);
}

static void load_balance(CPU_ITEM *now_cpu){
    uint32_t busiest_cpu = -1;
    uint32_t max_load = 0;
    uint32_t min_load = UINT32_MAX;
    uint32_t min_cpu = -1;
    uint32_t now_load = now_cpu->total_ready_num;
    for (uint32_t i = 0; i < cpus->total_num; i++) {
        CPU_ITEM *cpu = &cpus->items[i];
        uint32_t nr = cpu->total_ready_num;
        if (nr > max_load) {
            max_load = nr;
            busiest_cpu = i;
        }
        if (nr < min_load) {
            min_load = nr;
            min_cpu = i;
        }
    }
    if (max_load - min_load < MULTI_CORE_BALANCE_DELTA){
        return;
    }
    if (now_load == max_load){
        tasks_travel(now_cpu,&cpus->items[min_cpu]);
    }else if (now_load == min_load){
        tasks_travel(&cpus->items[busiest_cpu],now_cpu);
    }
}
