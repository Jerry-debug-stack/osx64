#ifndef OS_CONST_H
#define OS_CONST_H
#include <stdint.h>

#define VIRTUAL_ADDR_0 0xffff800000000000
#define PHYSIC_ADDR_AP_CODE_DATA       0x10000

#define easy_phy2linear(addr) (void*)((uint64_t)(addr) + VIRTUAL_ADDR_0)
#define easy_linear2phy(addr) (void*)((uint64_t)(addr) - VIRTUAL_ADDR_0)

#define halt()                           \
    do {                                 \
        __asm__ __volatile__("cli;hlt"); \
    } while (1);

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); })

#define SELECTOR_KERNEL_CS (0x1 << 3)
#define SELECTOR_KERNEL_DS (0x2 << 3)

#define SYSCALL_INTERRUPT_VECTOR 0x80
#define KERNEL_SCHEDULE_VECTOR 0x81

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#define MAX_CPU_NUM                     32
#define DEFAULT_PCB_SIZE                8*1024

#endif
