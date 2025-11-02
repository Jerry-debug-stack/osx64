#ifndef OS_CONST_H
#define OS_CONST_H
#include <stdint.h>

#define VIRTUAL_ADDR_0                  0xffff800000000000

#define easy_phy2linear(addr)           (void*)((uint64_t)(addr) + VIRTUAL_ADDR_0)
#define easy_linear2phy(addr)           (void*)((uint64_t)(addr) - VIRTUAL_ADDR_0)

#define halt()                          do{__asm__ __volatile__("cli;hlt");} while (1);

#endif
