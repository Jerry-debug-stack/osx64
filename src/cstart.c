#include "const.h"
#include "multiboot.h"
#include <stdint.h>
#include "view/view.h"

MULTIBOOT_INFO* global_multiboot_info;

void init_mm(MULTIBOOT_INFO* info);
void init_view(MULTIBOOT_INFO* info);
void init_protect(uint8_t is_bsp);
void init_apic_bsp(void);
void init_apic_ap(void);
void init_time(void);
void init_keyboard(void);
void init_ap(void);
void init_acpi_madt(void);

#include "machine/cpu.h"

void ap_start(void){
    init_apic_ap();
    init_protect(0);
    low_printf("[AP Core] Core %d started!\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE,get_logic_cpu_id());
}

void cstart(MULTIBOOT_INFO* info)
{
    global_multiboot_info = easy_phy2linear((uint64_t)info & 0xffffffff);
    init_mm(global_multiboot_info);
    init_view(global_multiboot_info);
    init_acpi_madt();
    init_apic_bsp();
    low_print("[SYSTEM ] apic ready\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    init_protect(1);
    init_ap();
    init_time();
    init_keyboard();
    while(1);
    halt();
}
