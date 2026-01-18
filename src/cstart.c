#include "const.h"
#include "multiboot.h"
#include <stdint.h>
#include "view/view.h"
#include "machine/cpu.h"

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
void init_task(void);
void cpu_task_start(void);

void ap_start(void){
    init_apic_ap();
    init_protect(0);
    low_printf("[AP Core] Core %d started!\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE,get_logic_cpu_id());
    cpu_task_start();
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
    init_task();
    init_time();
    //init_keyboard();
    low_print("[SYSTEM ] task ready\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    init_ap();
    cpu_task_start();
}

void init(void){
    low_print("[SYSTEM ] enter init progress!\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    while (1)
    {
        halt();
    }
}

void idle(void){
    uint32_t id = get_logic_cpu_id();
    while(1){
        if (id){
            __asm__ __volatile__("nop");
        }
        __asm__ __volatile__("hlt");
    }
}
