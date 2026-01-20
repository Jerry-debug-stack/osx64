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
extern void schedule(UNUSED uint8_t to_state);

uint64_t ticks;

void timer_intr_soft(void);
static inline void local_timer_timeout(CPU_ITEM *cpu);
static void add_timer(enum timer_type_enum timer_type,uint64_t first_ticks,uint32_t delta_ticks,pcb_t *task,uint32_t signal);

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
    local_timer_timeout(cpu);
    /* step 2 分析负载均衡 */

    /* step 3 考虑是否需要调度 */
    /* 这里保留作以后处理 */
    __asm__ __volatile__("sti");
    /* 下半段 */
    __asm__ __volatile__("cli");
    /* 结束段 */
    cpu->time_intr_reenter--;
    if (need_schedule){
        schedule(0);
    }
    /* 判断信号递送 */

}

void timer_intr_soft_bsp(void){
    ticks++;
    timer_intr_soft();
}

static void append_to_task_timer_list(spin_list_head_t *timer_list,timer_t *timer);
static void append_to_cpu_timer_list(spin_list_head_t *timer_list,timer_t *timer);

static inline void local_timer_timeout(CPU_ITEM *cpu){
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
            // 需要约定
            kfree(timer);
        }else{
            // 需要约定
            timer->ticks += timer->delta_ticks;
            append_to_cpu_timer_list(&cpu->timer_list,timer);
        }
    }
    spin_unlock(&cpu->ready_list.lock);
    spin_unlock(&cpu->timer_list.lock);
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
