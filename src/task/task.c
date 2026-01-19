#include "task.h"
#include "const.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "machine/cpu.h"
#include "const.h"
#include "lib/io.h"

extern GLOBAL_CPU *cpus;

extern void asm_task_start_go_out(void);
extern void task_switch(pcb_t *old,pcb_t *new);
extern _Noreturn void asm_task_start(uint64_t rsp);

extern void init(void);
extern void idle(void);

task_manager_t task_manager;

static pcb_t *put_kernel_thread(char *name, void *addr, pcb_t *parent);
static uint32_t alloc_pid_and_add_to_all_list(pcb_t *new_task);
static void add_to_cpu_n_ready_list(pcb_t *task,uint32_t n);

void init_task(void)
{
    /* task manager */
    spin_list_init(&task_manager.all_list);
    task_manager.next_free_id = 0;
    spin_lock_init(&task_manager.id_lock);
    /* 初始化init进程 */
    pcb_t* task_init = put_kernel_thread("init",init,NULL);
    //
    /* 各个cpu的idle进程 */
    CPU_ITEM* item;
    for(uint32_t i = 0;i < cpus->total_num;i++){
        item = &cpus->items[i];
        spin_list_init(&item->ready_list);
        pcb_t *task_idle = put_kernel_thread("idle",idle,task_init);
        item->idle = task_idle;
        add_to_cpu_n_ready_list(task_idle,i);
    }
    add_to_cpu_n_ready_list(task_init,get_logic_cpu_id()); //将init进程先行加给bsp
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
static pcb_t *put_kernel_thread(char *name, void *addr, pcb_t *parent)
{
    pcb_t *new_task = kmalloc(DEFAULT_PCB_SIZE);
    memset(new_task, 0, DEFAULT_PCB_SIZE);
    INIT_LIST_HEAD(&new_task->all_list);
    INIT_LIST_HEAD(&new_task->child_list_item);
    INIT_LIST_HEAD(&new_task->other_list_item);
    INIT_LIST_HEAD(&new_task->ready_list_item);
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
    spin_list_add_tail(&task->ready_list_item,&cpus->items[n].ready_list);
}

void schedule(UNUSED uint8_t to_state){
    uint32_t intr = io_cli();
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *item = &cpus->items[id];
    spin_lock(&item->ready_list.lock);
    pcb_t *will_run,*before_run = item->now_running;
    if (item->ready_list.list.next != &item->ready_list.list){
        list_head_t* next_ = item->ready_list.list.next;
        list_del_init(next_);
        will_run = container_of(next_,pcb_t,ready_list_item);
    }else{
        ///@todo to_state!!!
        will_run = item->idle;
    }
    if (item->now_running != item->idle){
        list_add_tail(&item->now_running->ready_list_item,&item->ready_list.list);
    }
    ///@todo handle Cr3
    item->now_running = will_run;
    spin_unlock(&item->ready_list.lock);
    task_switch(before_run,will_run);
    io_set_intr(intr);
}

_Noreturn void cpu_task_start(void){
    uint32_t id = get_logic_cpu_id();
    cpus->items[id].now_running = cpus->items[id].idle;
    asm_task_start(cpus->items[id].idle->rsp);
}
