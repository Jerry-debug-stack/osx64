#ifndef OS_IO_H
#define OS_IO_H

#include <stdint.h>
#include "const.h"

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

static inline void atomic_inc_uin32(uint32_t *ptr) {
    __asm__ __volatile__ ("lock incl %0": "+m"(*ptr)::"memory");
}

static inline void atomic_dec_uin32(uint32_t *ptr) {
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

static inline void io_delay(void) {
    // 读一个无用端口
    UNUSED volatile uint8_t a = io_inbyte(0x80);
}

static inline void cpuid(uint64_t leaf, uint64_t *rax, uint64_t *rbx, uint64_t *rcx, uint64_t *rdx) {
    __asm__ volatile ("cpuid"
        : "=a" (*rax), "=b" (*rbx), "=c" (*rcx), "=d" (*rdx)
        : "a" (leaf), "c" (0)
    );
}

static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r" (val));
    return val;
}

static inline void write_cr0(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr0" : : "r" (val));
}

static inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr4, %0" : "=r" (val));
    return val;
}

static inline void write_cr4(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr4" : : "r" (val));
}

// 刷新单个缓存行 (通常是 64 字节)
static inline void clflush(volatile void *p) {
    __asm__ __volatile__("clflush (%0)" : : "r"(p) : "memory");
}

#include <stddef.h>

// 刷新一段内存区域
static inline void clflush_range(void *start, size_t size) {
    if (!start) return;
    
    uintptr_t addr = (uintptr_t)start;
    uintptr_t end = addr + size;
    
    // x86 缓存行通常是 64 字节，对齐到 64 字节边界开始刷新
    addr &= ~(64 - 1);
    
    for (; addr < end; addr += 64) {
        clflush((void *)addr);
    }
    
    // 执行完 clflush 后，建议加一个 mfence 确保所有刷入操作完成
    __asm__ __volatile__("mfence" : : : "memory");
}

#endif