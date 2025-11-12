#include "const.h"
#include "multiboot.h"
#include <stdint.h>
#include "view/view.h"

MULTIBOOT_INFO* global_multiboot_info;

void init_mm(MULTIBOOT_INFO* info);
void init_view(MULTIBOOT_INFO* info);
void init_protect(void);
void init_apic(void);
void init_time(void);

void cstart(MULTIBOOT_INFO* info)
{
    global_multiboot_info = easy_phy2linear((uint64_t)info & 0xffffffff);
    init_mm(global_multiboot_info);
    init_view(global_multiboot_info);
    init_protect();
    init_apic();
    low_print("[SYSTEM]apic ready",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    init_time();
    __asm__ __volatile__("sti");
    while(1);
    halt();
}
