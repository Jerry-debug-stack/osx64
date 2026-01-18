#ifndef OS_IO_H
#define OS_IO_H

#include <stdint.h>

static inline uint32_t atomic_compare_exchange(uint32_t* dest, uint32_t expected, uint32_t new_value)
{
    uint32_t result;
    __asm__ __volatile__(
        "lock cmpxchgl %[new_value], %[dest]\n"
        : "=a"(result)
        : [dest] "m"(*dest),
        "a"(expected),
        [new_value] "r"(new_value)
        : "memory"
    );
    return result;
}

static inline void atomic_inc(uint32_t *ptr) {
    __asm__ __volatile__ ("lock incl %0": "+m"(*ptr)::"memory");
}

static inline void atomic_dec(uint32_t *ptr) {
    __asm__ __volatile__ ("lock decl %0": "+m"(*ptr)::"memory");
}

static inline uint8_t io_cli(void)
{
    uint32_t ret;
    __asm__ __volatile__("pushfq;pop %%rax;cli;" : "=rax"(ret)::);
    return ((ret & (1 << 9)) != 0);
}

static inline uint8_t io_sti(void)
{
    uint32_t ret;
    __asm__ __volatile__("pushfq;pop %%rax;sti;" : "=rax"(ret)::);
    return ((ret & (1 << 9)) != 0);
}

static inline void io_set_intr(uint8_t intr)
{
    if (intr)
        __asm__ __volatile__("sti");
    else
        __asm__ __volatile__("cli");
}

static inline uint8_t io_inbyte(unsigned short Port)
{
    uint8_t ret;
    __asm__ __volatile__("inb %%dx,%%al;" : "=a"(ret) : "d"(Port) :);
    return ret;
}

static inline void io_outbyte(uint16_t port, uint8_t byte)
{
    __asm__ __volatile__("outb %%al,%%dx;" ::"d"(port), "a"(byte));
}

static inline uint16_t io_inword(uint16_t port)
{
    uint16_t ret;
    __asm__ __volatile__("inw %%dx,%%ax;" : "=a"(ret) : "d"(port) :);
    return ret;
}

static inline void io_outword(uint16_t port, uint16_t word)
{
    __asm__ __volatile__("outw %%ax,%%dx;" ::"d"(port), "a"(word));
}

static inline uint32_t io_indword(uint16_t port)
{
    uint32_t ret;
    __asm__ __volatile__("inl %%dx,%%eax;" : "=a"(ret) : "d"(port) :);
    return ret;
}

static inline void io_outdword(uint16_t port, uint32_t dword)
{
    __asm__ __volatile__("outl %%eax,%%dx;" ::"d"(port), "a"(dword));
}

#endif