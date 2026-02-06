#include "task.h"
#include "const.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "machine/cpu.h"
#include "const.h"
#include "lib/io.h"
#include "lib/timer.h"

extern GLOBAL_CPU *cpus;

extern void asm_task_start_go_out(void);
extern void task_switch(pcb_t *old,pcb_t *new);
extern _Noreturn void asm_task_start(uint64_t rsp);

extern void init(void);
extern void idle(void);

task_manager_t task_manager;
pcb_t *pcb_of_init;

static uint32_t alloc_pid_and_add_to_all_list(pcb_t *new_task);
static void add_to_cpu_n_ready_list(pcb_t *task,uint32_t n);
static void free_task(pcb_t *task);

void test_wait_basic(void);

void init_task(void)
{
    /* task manager */
    spin_list_init(&task_manager.all_list);
    task_manager.next_free_id = 0;
    spin_lock_init(&task_manager.id_lock);
    /* 初始化init进程 */
    pcb_of_init = put_kernel_thread("init",init,NULL);
    //
    /* 各个cpu的idle进程 */
    CPU_ITEM* item;
    for(uint32_t i = 0;i < cpus->total_num;i++){
        item = &cpus->items[i];
        spin_list_init(&item->ready_list);
        item->total_ready_num = 0;
        pcb_t *pcb_of_idle = put_kernel_thread("idle",idle,pcb_of_init);
        item->idle = pcb_of_idle;
    }
    add_to_cpu_n_ready_list(pcb_of_init,get_logic_cpu_id()); //将init进程先行加给bsp
}

/**
 * @brief
 * @param name
 * @param addr
 * @param parent
 * @return
 * @note 事实上,在put时只有填入parent=nowrunning(当前进程),
 * 才是合法的,否则存在内存不安全问题,
 * parent不是自己,则可能随时退出,进而可能访问无效内存(new_task->parent)
 * parent选项的存在是为了可能的疑难问题
 * @todo 加上对args的support
 */
pcb_t *put_kernel_thread(char *name, void *addr, pcb_t *parent)
{
    pcb_t *new_task = kmalloc(DEFAULT_PCB_SIZE);
    memset(new_task, 0, DEFAULT_PCB_SIZE);
    INIT_LIST_HEAD(&new_task->all_list);
    INIT_LIST_HEAD(&new_task->child_list_item);
    INIT_LIST_HEAD(&new_task->other_list_item);
    INIT_LIST_HEAD(&new_task->ready_list_item);
    INIT_LIST_HEAD(&new_task->wait_list_item);
    wait_queue_init(&new_task->wait_queue);
    spin_list_init(&new_task->timers);
    /* pid */
    alloc_pid_and_add_to_all_list(new_task);
    /* parent */
    if (parent)
    {
        new_task->parent = parent;
        spin_list_add_tail(&new_task->child_list_item, &parent->childs);
    }
    else
    {
        new_task->parent = NULL;
    }
    /* childs */
    spin_list_init(&new_task->childs);
    /* name */
    if (name)
    {
        uint32_t length = strlen(name);
        new_task->name = kmalloc(length + 1);
        strcpy(new_task->name, name);
    }
    else
    {
        new_task->name = NULL;
    }
    new_task->is_ker = true;
    new_task->magic = TASK_MAGIC;
    new_task->signal = 0;
    new_task->state = TASK_STATE_READY;
    /* start up 栈空间 */
    registers_t *reg = (void *)((uint64_t)new_task + DEFAULT_PCB_SIZE - sizeof(registers_t));
    task_start_t *task_start = (void *)((uint64_t)new_task + DEFAULT_PCB_SIZE - (sizeof(registers_t) + sizeof(task_start_t)));
    memset(task_start, 0, sizeof(registers_t) + sizeof(task_start_t));
    
    task_start->ret = (uint64_t)asm_task_start_go_out;
    reg->cs = SELECTOR_KERNEL_CS;
    reg->ds = reg->es = reg->ss = SELECTOR_KERNEL_DS;
    reg->rip = (uint64_t)addr;
    reg->rsp = (uint64_t)new_task + DEFAULT_PCB_SIZE;
    reg->rflags = 0x202;
    
    new_task->rsp = (uint64_t)task_start;
    return new_task;
}

static uint32_t alloc_pid_and_add_to_all_list(pcb_t *new_task)
{
    spin_lock(&task_manager.id_lock);
    new_task->pid = task_manager.next_free_id;
    task_manager.next_free_id++;
    if (task_manager.next_free_id == (uint64_t)-1L){
        halt();
    }
    spin_list_add_tail(&new_task->all_list, &task_manager.all_list);
    spin_unlock(&task_manager.id_lock);
    return 0;
}

static void add_to_cpu_n_ready_list(pcb_t *task,uint32_t n){
    if (n >= cpus->total_num){
        halt();
    }
    spin_list_head_t *tar_ready_list = &cpus->items[n].ready_list;
    spin_lock(&tar_ready_list->lock);
    list_add_tail(&task->ready_list_item,&tar_ready_list->list);
    cpus->items[n].total_ready_num++;
    spin_unlock(&tar_ready_list->lock);
}

void schedule(enum task_state to_state){
    uint32_t intr = io_cli();
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *item = &cpus->items[id];
    spin_lock(&item->ready_list.lock);
    pcb_t *will_run,*before_run = item->now_running;
    if (item->ready_list.list.next != &item->ready_list.list){
        list_head_t* next_ = item->ready_list.list.next;
        list_del_init(next_);
        will_run = container_of(next_,pcb_t,ready_list_item);
        item->total_ready_num--;
    }else{
        ///@todo to_state!!!
        will_run = item->idle;
    }
    if (item->now_running != item->idle){
        if (to_state == TASK_STATE_READY){
            list_add_tail(&item->now_running->ready_list_item,&item->ready_list.list);
            item->total_ready_num++;
        }
        item->now_running->state = to_state;
    }
    ///@todo handle Cr3
    
    item->now_running = will_run;
    will_run->state = TASK_STATE_RUNNING;
    spin_unlock(&item->ready_list.lock);
    task_switch(before_run,will_run);
    io_set_intr(intr);
}

static inline _Noreturn void schedule_zombie(){
    schedule(TASK_ZOMBIE);
    __builtin_unreachable();
}

_Noreturn void cpu_task_start(void){
    uint32_t id = get_logic_cpu_id();
    cpus->items[id].now_running = cpus->items[id].idle;
    asm_task_start(cpus->items[id].idle->rsp);
}

_Noreturn void sys_exit(int exit_status){
    /* 关中断,获取锁,去掉所有时钟 */
    uint8_t intr = io_cli();
    uint32_t cpu_id = get_logic_cpu_id();
    CPU_ITEM *this_cpu = &cpus->items[cpu_id];
    pcb_t *task = this_cpu->now_running;
    list_head_t *pos;
    list_head_t *n;

    spin_lock(&this_cpu->timer_list.lock);
    list_for_each_safe(pos,n,&task->timers.list){
        timer_t *task_timer = container_of(pos,timer_t,in_task_item);
        list_del(&task_timer->in_task_item);
        kfree(task_timer);
    }
    spin_unlock(&this_cpu->timer_list.lock);
    io_set_intr(intr);
    
    /* 移交子进程 */
    spin_lock(&task->childs.lock);
    spin_lock(&pcb_of_init->childs.lock);
    pcb_t *child;
    list_for_each_entry(child, &task->childs.list, child_list_item) {
        child->parent = pcb_of_init;
    }
    list_splice_init(&task->childs.list,&pcb_of_init->childs.list);
    spin_unlock(&pcb_of_init->childs.lock);
    spin_unlock(&task->childs.lock);

    /* 设置退出代码 */
    task->exit_status = exit_status;
    wake_up_all(&task->parent->wait_queue);
    schedule_zombie();
}

int sys_waitpid(int pid, int *status)
{
    uint32_t cpu_id = get_logic_cpu_id();
    CPU_ITEM *this_cpu = &cpus->items[cpu_id];
    pcb_t *now_pcb = this_cpu->now_running;
    while (1) {
        int found_child = 0;
        spin_lock(&now_pcb->childs.lock);
        pcb_t *child;
        list_for_each_entry(child, &now_pcb->childs.list, child_list_item) {
            if (pid == -1 || child->pid == pid) {
                found_child = 1;
                if (child->state == TASK_ZOMBIE) {
                    int exit_code = child->exit_status;
                    list_del(&child->child_list_item);
                    spin_unlock(&now_pcb->childs.lock);
                    if (status)
                        *status = exit_code;
                    free_task(child);
                    return child->pid;
                }
            }
        }
        spin_unlock(&now_pcb->childs.lock);
        if (!found_child)
            return -1;

        sleep_on(&now_pcb->wait_queue);
    }
}

pcb_t *get_current(void){
    uint32_t cpu_id = get_logic_cpu_id();
    CPU_ITEM *this_cpu = &cpus->items[cpu_id];
    return this_cpu->now_running;
}

void put_to_ready_list(pcb_t *task){
    uint8_t intr = io_cli();
    uint32_t cpu_id = get_logic_cpu_id();
    CPU_ITEM *this_cpu = &cpus->items[cpu_id];
    spin_lock(&this_cpu->ready_list.lock);
    list_add(&task->ready_list_item,&this_cpu->ready_list.list);
    spin_unlock(&this_cpu->ready_list.lock);
    io_set_intr(intr);
}

static void free_task(pcb_t *task){
    if (!task->is_ker){
    }
    kfree(task);
}
