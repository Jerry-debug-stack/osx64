#include "const.h"
#include "multiboot.h"
#include <stdint.h>
#include "view/view.h"
#include "machine/cpu.h"

MULTIBOOT_INFO* global_multiboot_info;

void init_mm(MULTIBOOT_INFO* info);
void parse_cmd_line(MULTIBOOT_INFO *info);
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
void read_partitions(void);
void init_fpu_sse(void);
void real_time_init(void);

_Noreturn void cpu_task_start(void);

bool multi_core_start = false;
bool tty_ready = false;

void enable_irq(uint64_t irq);
_Noreturn void ap_start(void){
    init_apic_ap();
    init_protect(0);
    wb_printf("[AP Core] Core %d started!\n",get_logic_cpu_id());
    init_fpu_sse();
    enable_irq(2);
    cpu_task_start();
}

_Noreturn void cstart(MULTIBOOT_INFO* info)
{
    global_multiboot_info = easy_phy2linear((uint64_t)info & 0xffffffff);
    init_mm(global_multiboot_info);
    init_view(global_multiboot_info);
    parse_cmd_line(info);
    init_acpi_madt();
    init_apic_bsp();
    wb_printf("[SYSTEM ] apic ready\n");
    init_protect(1);
    init_task();
    init_time();
    init_keyboard();
    wb_printf("[SYSTEM ] task ready\n");
    init_fpu_sse();
    multi_core_start = true;
    init_ap();
    real_time_init();
    cpu_task_start();
}

void ahci_kernel_thread(void);
void display_server(UNUSED void *arg);

void mount_root(void);
void pty_init(void);
void uhci_kernel_thread(void);
void uhci_initial_scan(void);
void ehci_kernel_thread(void);
void ehci_initial_scan(void);

void init(void){
    wb_printf("[SYSTEM ] enter init progress!\n");
    
    init_fs_mem();
    enumerate_pcie_devices();

    kernel_thread_link_init("ahci",ahci_kernel_thread,NULL);
    kernel_thread_link_init("uhci",uhci_kernel_thread,NULL);
    kernel_thread_link_init("ehci",ehci_kernel_thread,NULL);
    
    ehci_initial_scan();
    uhci_initial_scan();

    read_partitions();
    mount_root();
    pty_init();
    
    kernel_thread_link_init("display",display_server,NULL);

    while (1)
    {
        sys_waitpid(-1,NULL);
    }
}

void idle(void){
    while(1){
        __asm__ __volatile__("hlt");
    }
}
