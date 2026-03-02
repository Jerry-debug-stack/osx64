#include "task.h"
#include "const.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "machine/cpu.h"
#include "const.h"
#include "lib/io.h"
#include "lib/timer.h"
#include "fs/fs.h"

extern GLOBAL_CPU *cpus;

extern void asm_task_start_go_out(void);
extern void task_switch_unlock(pcb_t *old,pcb_t *new,spinlock_t *lock, int *preempt_count);
extern void task_switch_double_unlock(pcb_t *old,pcb_t *new,spinlock_t *lock, int *preempt_count,spinlock_t *wq_lock);
extern _Noreturn void asm_task_start(uint64_t rsp);

extern void init(void);
extern void idle(void);

task_manager_t task_manager;
pcb_t *pcb_of_init;

static uint32_t alloc_pid_and_add_to_all_list(pcb_t *new_task);
static void add_to_cpu_n_ready_list(pcb_t *task,uint32_t n);
static void free_task(pcb_t *task);
static pcb_t *put_kernel_thread(char *name, void *addr, pcb_t *parent);
static pcb_t *kernel_thread(char *name, void *addr,pcb_t *parent,uint32_t n);

void init_task(void)
{
    /* task manager */
    spin_list_init(&task_manager.all_list);
    task_manager.next_free_id = 0;
    spin_lock_init(&task_manager.id_lock);
    /* 各个cpu的idle进程 */
    CPU_ITEM* item;
    for(uint32_t i = 0;i < cpus->total_num;i++){
        item = &cpus->items[i];
        spin_list_init(&item->ready_list);
        item->total_ready_num = 0;
        pcb_t *pcb_of_idle = put_kernel_thread("idle",idle,NULL);
        item->idle = pcb_of_idle;
        item->now_running = NULL;
    }
    /* 初始化init进程 */
    pcb_of_init = kernel_thread("init",init,NULL,-1);
}

static pcb_t *kernel_thread(char *name, void *addr,pcb_t *parent,uint32_t n){
    uint32_t target;
    if (n == (uint32_t)-1){
        target = get_logic_cpu_id();
    }else{
        target = n;
    }
    uint8_t intr = io_cli();
    pcb_t *ret = put_kernel_thread(name,addr,parent);
    add_to_cpu_n_ready_list(ret,target);
    io_set_intr(intr);
    return ret;
}

extern uint64_t *vir_ptable4;

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
    INIT_LIST_HEAD(&new_task->all_list);
    INIT_LIST_HEAD(&new_task->child_list_item);
    INIT_LIST_HEAD(&new_task->other_list_item);
    INIT_LIST_HEAD(&new_task->ready_list_item);
    INIT_LIST_HEAD(&new_task->wait_list_item);
    wait_queue_init(&new_task->wait_queue);
    spin_list_init(&new_task->timers);
    new_task->cr3 = (uint64_t)vir_ptable4;
    new_task->cpuid = 0;
    new_task->preempt_count = 0;
    /* pid */
    alloc_pid_and_add_to_all_list(new_task);
    /* parent */
    if (parent)
    {
        new_task->cwd = parent->cwd;
        getin_cwd(parent->cwd);
        new_task->parent = parent;
        spin_list_add_tail(&new_task->child_list_item, &parent->childs);
    }
    else
    {
        new_task->cwd = NULL;
        new_task->parent = NULL;
    }
    /* childs */
    spin_list_init(&new_task->childs);
    /* name */
    if (name)
    {
        uint32_t length = strlen((const char*)name);
        new_task->name = kmalloc(length + 1);
        strcpy(new_task->name, name);
    }
    else
    {
        new_task->name = NULL;
    }
    for (int i = 0; i < NR_OPEN_DEFAULT; i++){
        new_task->files[i] = NULL;
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
    task->cpuid = n;
    spin_list_head_t *tar_ready_list = &cpus->items[n].ready_list;
    spin_lock(&tar_ready_list->lock);
    list_add_tail(&task->ready_list_item,&tar_ready_list->list);
    cpus->items[n].total_ready_num++;
    spin_unlock(&tar_ready_list->lock);
}

static inline void switch_cr3_if_needed(pcb_t *will_run)
{
    uint64_t current_cr3;
    uint64_t new_cr3 = (uint64_t)easy_linear2phy(will_run->cr3);
    __asm__ __volatile__ (
        "mov %%cr3, %0"
        : "=r"(current_cr3)
        :
        : "memory"
    );
    if (current_cr3 == new_cr3)
        return;
    __asm__ __volatile__ (
        "mov %0, %%cr3"
        :
        : "r"(new_cr3)
        : "memory"
    );
}

void __schedule_locked(uint8_t intr){
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *item = &cpus->items[id];
    pcb_t *will_run,*before_run = item->now_running;
    if (item->ready_list.list.next != &item->ready_list.list){
        list_head_t* next_ = item->ready_list.list.next;
        list_del_init(next_);
        will_run = container_of(next_,pcb_t,ready_list_item);
        item->total_ready_num--;
    }else{
        will_run = item->idle;
    }
    switch_cr3_if_needed(will_run);
    will_run->cpuid = id;
    item->now_running = will_run;
    will_run->state = TASK_STATE_RUNNING;
    item->tss->rsp0 = (uint64_t)will_run + DEFAULT_PCB_SIZE;
    task_switch_unlock(before_run,will_run,&item->ready_list.lock,&before_run->preempt_count);
    io_set_intr(intr);
}

void __schedule_other_locked(spinlock_t *wq_lock){
    uint8_t intr = io_cli();
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *item = &cpus->items[id];
    pcb_t *will_run,*before_run = item->now_running;
    spin_lock(&item->ready_list.lock);
    if (item->ready_list.list.next != &item->ready_list.list){
        list_head_t* next_ = item->ready_list.list.next;
        list_del_init(next_);
        will_run = container_of(next_,pcb_t,ready_list_item);
        item->total_ready_num--;
    }else{
        will_run = item->idle;
    }
    switch_cr3_if_needed(will_run);
    will_run->cpuid = id;
    item->now_running = will_run;
    will_run->state = TASK_STATE_RUNNING;
    item->tss->rsp0 = (uint64_t)will_run + DEFAULT_PCB_SIZE;
    task_switch_double_unlock(before_run,will_run,&item->ready_list.lock,&before_run->preempt_count,wq_lock);
    io_set_intr(intr);
}

void schedule(void){
    uint8_t intr = io_cli();
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *cpu = &cpus->items[id];
    spin_lock(&cpu->ready_list.lock);
    __schedule_locked(intr);
}

static inline _Noreturn void schedule_zombie(){
    get_current()->state = TASK_ZOMBIE;
    schedule();
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

void put_to_ready_list_first(pcb_t *task){
    uint32_t cpuid = task->cpuid;
    CPU_ITEM *cpu = &cpus->items[cpuid];
    task->state = TASK_STATE_READY;
    spin_lock(&cpu->ready_list.lock);
    list_add(&task->ready_list_item,&cpu->ready_list.list);
    cpu->total_ready_num++;
    spin_unlock(&cpu->ready_list.lock);
}

static void free_task(pcb_t *task){
    if (!task->is_ker){
    }
    kfree(task);
}

void yield(void){
    uint8_t intr = io_cli();
    uint32_t id = get_logic_cpu_id();
    CPU_ITEM *cpu = &cpus->items[id];
    pcb_t *current = cpu->now_running;
    spin_lock(&cpu->ready_list.lock);
    current->state = TASK_STATE_READY;
    list_add_tail(&current->ready_list_item,&cpus->items[id].ready_list.list);
    cpu->total_ready_num++;
    __schedule_locked(intr);
}

int init_cwd_for_started_tasks(struct dentry *root){
    list_head_t *pos;
    int ret = 0;
    spin_lock(&task_manager.all_list.lock);
    list_for_each(pos,&task_manager.all_list.list){
        pcb_t *item = container_of(pos,pcb_t,all_list);
        item->cwd = root;
        ret++;
    }
    spin_unlock(&task_manager.all_list.lock);
    return ret;
}

void kernel_thread_default(char *name, void *addr){
    kernel_thread(name,addr,get_current(),0);
}

void kernel_thread_link_init(char *name, void *addr){
    kernel_thread(name,addr,pcb_of_init,0);
}

#include "lib/elf.h"
_Noreturn void asm_execv_out(uint64_t a,uint64_t b,uint64_t c,uint64_t d);

int sys_execv_end(const char* path, char* const argv[])
{
    if (!path)
        return -1;
    int i = 0;
    int len = 0;
    while (argv[i] != 0) {
        len += strlen(argv[i]) + 1;
        i++;
    }
    if (i > 31 || (len + (i << 3) >= 3072)){
        return -5;
    }
    pcb_t *current = get_current();
    int fd = sys_open(path,O_RDONLY,0);
    if (fd < 0){
        return -4;
    }
    elf64_header_t* header = elf_file_executable(fd);
    if (!header) {
        sys_close(fd);
        return -3;
    }

    char* temp = kmalloc(4096);
    char* pos = temp + 4096;
    char** new_argv = (char**)(pos - len - i - 8);
    i = 0;
    while (argv[i] != 0) {
        len = strlen(argv[i]) + 1;
        pos -= len;
        memcpy(pos, argv[i], len);
        new_argv[i] = (char*)(((uint64_t)pos) - ((uint64_t)temp)
            + (VIRTUAL_ADDR_USER_HIGHEST - 4096));
        i++;
    }
    new_argv[i] = (void*)0;

    uint8_t intr = io_cli();
    if (current->cr3 != (uint64_t)vir_ptable4){
        uint64_t old_cr3 = current->cr3;
        current->cr3 = (uint64_t)vir_ptable4;
        io_set_intr(intr);
        free_ptable_and_mem(old_cr3);
    }else{
        io_set_intr(intr);
    }
    uint64_t cr3 = (uint64_t)kmalloc(4096);
    memset((void*)cr3, 0, 2048);
    memcpy((void*)(cr3 + 2048), (void*)((uint64_t)vir_ptable4 + 2048), 2048);
    uint64_t phy_cr3 = (uint64_t)easy_linear2phy(cr3);
    
    intr = io_cli();
    current->cr3 = cr3;
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(phy_cr3) : "memory");
    put_page_4k((uint64_t)easy_linear2phy(temp),VIRTUAL_ADDR_USER_HIGHEST - 4096,cr3,1);
    io_set_intr(intr);

    elf_file_copy(fd, header, cr3);
    sys_close(fd);

    /* 清理除了std以外的所有文件 */
    for (uint32_t i = 3; i < NR_OPEN_DEFAULT; i++){
        sys_close(i);
    }
    __asm__ __volatile__("cli");
    current->is_ker = false;
    
    asm_execv_out(
        (uint64_t)current + DEFAULT_PCB_SIZE - sizeof(registers_t),
        ((uint64_t)new_argv) - (uint64_t)temp + (VIRTUAL_ADDR_USER_HIGHEST - 4096) - 8,
        header->elf_entry,
        (uint64_t)new_argv
    );
}
