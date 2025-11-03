#ifndef OS_MM_H
#define OS_MM_H

#include <stdint.h>

void put_page_4k(uint64_t phy_addr,uint64_t vir_addr,uint64_t ptable_vir,uint8_t type);
void rm_page_4k(uint64_t vir_addr,uint64_t ptable_vir);
void put_page_2M(uint64_t phy_addr,uint64_t vir_addr,uint64_t ptable_vir);

uint64_t alloc_page_4k(void);
uint8_t decrease_reference_page_4k(uint64_t addr);
uint8_t add_reference_page_4k(uint64_t addr);

void* kmalloc(uint32_t size);
void kfree(void* vir_addr);

#endif
