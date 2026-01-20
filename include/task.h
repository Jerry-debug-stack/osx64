#ifndef OS_TASK_H
#define OS_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "lib/my_list.h"
#include "lib/safelist.h"

#define TASK_MAGIC 0x13973264       // for PCB safety

typedef struct task_manager{
    spin_list_head_t all_list;
    spin_lock_t id_lock;
    uint64_t next_free_id;
} task_manager_t;

enum task_state{
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEP_INTERRUPTABLE,
    TASK_STATE_SLEEP_NOT_INTR_ABLE,
    TASK_ZOMBIE,
    TASK_DEAD,
};

typedef struct pcb
{
    /* 直接通过与运算就可以解决问题 */
    uint64_t rsp;
    /* basic information */
    char *name;
    uint64_t pid;
    bool is_ker;
    list_head_t all_list;
    /* parent relationship */
    struct pcb *parent;
    spin_list_head_t childs;
    /* scheduler needed */
    list_head_t ready_list_item;
    list_head_t other_list_item;
    list_head_t child_list_item;
    /* 定时器 */
    spin_list_head_t timers;
    uint32_t signal;
    uint32_t magic;
    enum task_state state;
} pcb_t;

/// @brief 保存上下文信息
typedef struct registers {
    uint64_t    ds,es,r15,r14,r13,r12,r11,r10,r9,r8,
                rsi,rdi,rbp,rdx,rcx,rbx,rax,
                rip,cs,rflags,rsp,ss;
} registers_t;

/// @brief task启动时的栈（或者是schedule时的栈）
typedef struct task_start {
    uint64_t rbx,rbp,r13,r14,r15,ret;
} task_start_t;

#endif
