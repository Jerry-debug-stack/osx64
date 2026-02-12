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
void init_fs_mem(void);
void enumerate_pcie_devices(void);
void put_ahci_thread(void);
void read_partitions(void);

_Noreturn void cpu_task_start(void);

bool multi_core_start = false;

void enable_irq(uint64_t irq);
_Noreturn void ap_start(void){
    init_apic_ap();
    init_protect(0);
    wb_printf("[AP Core] Core %d started!\n",get_logic_cpu_id());
    enable_irq(2);
    cpu_task_start();
}

_Noreturn void cstart(MULTIBOOT_INFO* info)
{
    global_multiboot_info = easy_phy2linear((uint64_t)info & 0xffffffff);
    init_mm(global_multiboot_info);
    init_view(global_multiboot_info);
    init_acpi_madt();
    init_apic_bsp();
    color_print("[SYSTEM ] apic ready\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    init_protect(1);
    init_task();
    init_time();
    //init_keyboard();
    color_print("[SYSTEM ] task ready\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    multi_core_start = true;
    init_ap();
    cpu_task_start();
}

void init(void){
    color_print("[SYSTEM ] enter init progress!\n",VIEW_COLOR_BLACK,VIEW_COLOR_WHITE);
    
    init_fs_mem();
    enumerate_pcie_devices();
    put_ahci_thread();
    read_partitions();

    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}

void idle(void){
    while(1){
        __asm__ __volatile__("hlt");
    }
}
