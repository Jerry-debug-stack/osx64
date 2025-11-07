#ifndef OS_CONST_H
#define OS_CONST_H
#include <stdint.h>

#define VIRTUAL_ADDR_0 0xffff800000000000

#define easy_phy2linear(addr) (void*)((uint64_t)(addr) + VIRTUAL_ADDR_0)
#define easy_linear2phy(addr) (void*)((uint64_t)(addr) - VIRTUAL_ADDR_0)

#define halt()                           \
    do {                                 \
        __asm__ __volatile__("cli;hlt"); \
    } while (1);

#define SELECTOR_KERNEL_CS      (0x1<<3)
#define SELECTOR_KERNEL_DS      (0x2<<3)

#define SYSCALL_INTERRUPT_VECTOR 0x80
#define KERNEL_SCHEDULE_VECTOR 0x81

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#endif
