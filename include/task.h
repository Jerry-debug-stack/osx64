#ifndef OS_TASK_H
#define OS_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "lib/my_list.h"
#include "lib/safelist.h"
#include "lib/wait_queue.h"

#define TASK_MAGIC 0x13973264       // for PCB safety
#define NR_OPEN_DEFAULT 64

typedef struct task_manager{
    spin_list_head_t all_list;
    spinlock_t id_lock;
    uint64_t next_free_id;
} task_manager_t;

enum task_state{
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEP_INTERRUPTABLE,
    TASK_STATE_SLEEP_NOT_INTR_ABLE,
    TASK_ZOMBIE,
    TASK_DEAD
};

typedef struct pcb
{
    /* 直接通过与运算就可以解决问题 */
    uint64_t rsp;
    /* basic information */
    char *name;
    int pid;
    bool is_ker;
    uint64_t cr3;
    uint32_t cpuid;
    list_head_t all_list;
    /* parent relationship */
    struct pcb *parent;
    spin_list_head_t childs;
    /* scheduler needed */
    list_head_t ready_list_item;
    list_head_t other_list_item;
    list_head_t child_list_item;
    list_head_t wait_list_item;
    wait_queue_t wait_queue;
    int preempt_count;
    /* 定时器 */
    spin_list_head_t timers;
    uint32_t signal;
    uint32_t magic;
    /* 文件系统 */
    struct file *files[NR_OPEN_DEFAULT];
    struct dentry *cwd;
    
    enum task_state state;
    int exit_status;
} pcb_t;

/// @brief 保存上下文信息
typedef struct registers {
    uint64_t    ds,es,r15,r14,r13,r12,r11,r10,r9,r8,
                rsi,rdi,rbp,rdx,rcx,rbx,rax,
                rip,cs,rflags,rsp,ss;
} registers_t;

/// @brief task启动时的栈（或者是schedule时的栈）
typedef struct task_start {
    uint64_t rbx,rbp,r12,r13,r14,r15,ret;
} task_start_t;

extern pcb_t *pcb_of_init;

pcb_t *get_current(void);
void put_to_ready_list_first(pcb_t *task);

void kernel_thread_default(char *name, void *addr);
void kernel_thread_link_init(char *name, void *addr);

void schedule(void);
void __schedule_locked(uint8_t intr);
void __schedule_other_locked(spinlock_t *wq_lock);
void yield(void);
static inline void preempt_disable(void) {
    pcb_t *current = get_current();
    current->preempt_count++;
}
static inline void preempt_enable(void) {
    pcb_t *current = get_current();
    current->preempt_count--;
}

#endif
