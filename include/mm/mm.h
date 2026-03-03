#ifndef OS_MM_H
#define OS_MM_H

#include <stdint.h>

void put_page_4k(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir, uint8_t type);
void __put_page_4k_locked(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir, uint8_t type);
int exist_page_4k(uint64_t vir_addr, uint64_t ptable_vir);
void rm_page_4k(uint64_t vir_addr, uint64_t ptable_vir);
void put_page_2M(uint64_t phy_addr, uint64_t vir_addr, uint64_t ptable_vir);

void free_ptable_and_mem(uint64_t pml4_vir);
void copy_pagetable_and_mem(uint64_t dest,uint64_t source);

uint64_t alloc_page_4k(void);
uint8_t decrease_reference_page_4k(uint64_t addr);
uint8_t add_reference_page_4k(uint64_t addr);

void* kmalloc(uint32_t size);
void kfree(void* vir_addr);

#endif
