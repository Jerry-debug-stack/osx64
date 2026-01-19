#include <stdint.h>
#include "const.h"

#define RELOAD_TICKS (1193182 / 1000)

uint16_t ticks;

void set_handler(uint64_t irq, uint64_t addr);

void set_EOI(void);
void disable_irq(uint64_t irq);
void enable_irq(uint64_t irq);
uint32_t get_logic_cpu_id(void);

void timer_intr_soft(void);

void init_time(void)
{
    ticks = 0;
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x43), "al"(0x34) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x40), "al"((RELOAD_TICKS) & 0xFF) :);
    __asm__ __volatile__("movw %0,%%dx;outb %%al,%%dx;" ::"i"(0x40), "al"((RELOAD_TICKS) >> 8) :);
    set_handler(2, (uint64_t)timer_intr_soft);
    enable_irq(2);
}

void schedule(UNUSED uint8_t to_state);
void timer_intr_soft(void){
    set_EOI();
    schedule(0);
}

void timer_intr_soft_bsp(void){
    ticks++;
    set_EOI();
    schedule(0);
}

