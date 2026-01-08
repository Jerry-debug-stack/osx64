#ifndef OS_CPU_H 
#define OS_CPU_H

#include <stdint.h>
#include "const.h"

typedef struct
{
    
} CPU_ITEM;

typedef struct
{
    uint32_t total_num;
    uint32_t physic_apic_id[MAX_CPU_NUM];
    CPU_ITEM items[MAX_CPU_NUM];
} GLOBAL_CPU;

#endif
