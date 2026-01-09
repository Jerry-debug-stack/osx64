#ifndef OS_CPU_H 
#define OS_CPU_H

#include <stdint.h>
#include "const.h"

typedef struct
{
    uint64_t *gdt_table;
    uint64_t *idt_table;
    uint32_t *gdt_ptr;
    uint32_t *idt_ptr;
    uint32_t *tss;
} CPU_ITEM;

typedef struct
{
    uint32_t total_num;
    uint32_t physic_apic_id[MAX_CPU_NUM];
    CPU_ITEM items[MAX_CPU_NUM];
} GLOBAL_CPU;

uint32_t get_logic_cpu_id(void);
uint32_t get_apic_id(void);

#endif
