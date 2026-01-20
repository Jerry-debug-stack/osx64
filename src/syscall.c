#include "const.h"
#include <stdint.h>

extern uint64_t ticks;
uint64_t sys_get_ticks(void){
    return ticks;
}

void *syscall_table[MAX_SYSCALL_NUM] = {
    sys_get_ticks
};
